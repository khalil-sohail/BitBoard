import { spawn, ChildProcess } from 'child_process';
import { WebSocket } from 'ws';
import path from 'path';
import fs from 'fs';

interface EngineSession {
  id: string;
  process: ChildProcess;
  ws: WebSocket;
  createdAt: number;
  lastActivityAt: number;
  idleTimer: NodeJS.Timeout;
  sessionTimer: NodeJS.Timeout;
}

const MAX_CONCURRENT = 10;
const IDLE_TIMEOUT_MS = 2 * 60_000;
const SESSION_MAX_MS = 30 * 60_000;
const HASH_SIZE_MB = 32;

class EnginePoolManager {
  private active = new Map<string, EngineSession>();
  private waitQueue: Array<{ ws: WebSocket; id: string }> = [];
  private enginePath: string;

  constructor() {
    this.enginePath = process.env.ENGINE_PATH || path.join(process.cwd(), '../engine/chess-engine');
  }

  public handleConnection(ws: WebSocket, id: string) {
    if (this.active.size < MAX_CONCURRENT) {
      this.spawnEngine(ws, id);
    } else {
      if (this.waitQueue.length >= 20) {
        ws.send(JSON.stringify({ type: 'error', message: 'Server is full. Try again later.' }));
        ws.close();
        return;
      }
      this.waitQueue.push({ ws, id });
      this.updateQueuePositions();
    }
  }

  private updateQueuePositions() {
    this.waitQueue.forEach((item, index) => {
      item.ws.send(JSON.stringify({ type: 'queued', position: index + 1 }));
    });
  }

  private spawnEngine(ws: WebSocket, id: string) {
    if (!fs.existsSync(this.enginePath)) {
        ws.send(JSON.stringify({ type: 'error', message: `Engine binary not found at ${this.enginePath}` }));
        console.error(`Engine binary not found at ${this.enginePath}`);
        this.terminateSession(id, 'error');
        return;
    }

    const engineProcess = spawn(this.enginePath, ['--mode=gui']);

    const session: EngineSession = {
      id,
      process: engineProcess,
      ws,
      createdAt: Date.now(),
      lastActivityAt: Date.now(),
      idleTimer: setTimeout(() => this.terminateSession(id, 'idle'), IDLE_TIMEOUT_MS),
      sessionTimer: setTimeout(() => this.terminateSession(id, 'session_expired'), SESSION_MAX_MS),
    };

    this.active.set(id, session);

    // Setup Engine Initialization
    if (engineProcess.stdin && engineProcess.stdout) {
      engineProcess.stdin.write('uci\n');
      
      let isReady = false;

      engineProcess.stdout.on('data', (data) => {
        const lines = data.toString().split('\n');
        for (const line of lines) {
          const trimmed = line.trim();
          if (!trimmed) continue;
          
          console.log(`[Engine ${id}] ${trimmed}`);

          this.updateActivity(id);

          if (!isReady) {
            if (trimmed === 'uciok') {
              engineProcess.stdin?.write(`setoption name Hash value ${HASH_SIZE_MB}\n`);
              engineProcess.stdin?.write('isready\n');
            } else if (trimmed === 'readyok') {
              isReady = true;
              ws.send(JSON.stringify({ type: 'ready' }));
            }
          } else {
            // Parse engine output
            if (trimmed.startsWith('info ')) {
              const parsedInfo = this.parseInfo(trimmed);
              if (parsedInfo) {
                ws.send(JSON.stringify({ type: 'info', ...parsedInfo }));
              }
            } else if (trimmed.startsWith('bestmove ')) {
              const parts = trimmed.split(' ');
              if (parts.length >= 2) {
                ws.send(JSON.stringify({ type: 'bestmove', move: parts[1] }));
              }
            } else if (trimmed === 'readyok') {
              ws.send(JSON.stringify({ type: 'ready' }));
            }
          }
        }
      });

      engineProcess.on('error', (err) => {
        console.error(`Engine error (Session ${id}):`, err);
        ws.send(JSON.stringify({ type: 'error', message: 'Engine process error' }));
        this.terminateSession(id, 'error');
      });

      engineProcess.on('exit', (code) => {
        console.log(`Engine exited (Session ${id}) with code ${code}`);
        if (this.active.has(id)) {
            ws.send(JSON.stringify({ type: 'error', message: 'Engine crashed unexpectedly' }));
            this.terminateSession(id, 'error');
        }
      });
    } else {
        ws.send(JSON.stringify({ type: 'error', message: 'Failed to start engine' }));
        this.terminateSession(id, 'error');
    }

    ws.on('message', (message) => {
        try {
            const data = JSON.parse(message.toString());
            this.handleClientMessage(id, data);
        } catch (e) {
            console.error('Invalid message from client:', e);
        }
    });

    ws.on('close', () => {
      this.terminateSession(id, 'disconnected');
    });
  }

  private handleClientMessage(id: string, data: any) {
    const session = this.active.get(id);
    if (!session || !session.process.stdin) return;

    this.updateActivity(id);

    if (data.type === 'move') {
      const movesStr = data.moves && data.moves.length > 0 ? ` moves ${data.moves.join(' ')}` : '';
      session.process.stdin.write(`position startpos${movesStr}\n`);
      
      let goCommand = 'go movetime 3000\n'; // default standard
      if (data.difficulty === 'blitz') {
          goCommand = 'go movetime 1000\n';
      } else if (data.difficulty === 'deep') {
          goCommand = 'go depth 8\n';
      } else if (data.difficulty === 'standard') {
          goCommand = 'go movetime 3000\n';
      }

      session.process.stdin.write(goCommand);
    } else if (data.type === 'newgame') {
        session.process.stdin.write(`ucinewgame\n`);
        session.process.stdin.write(`isready\n`);
    } else if (data.type === 'stop') {
        session.process.stdin.write(`stop\n`);
    }
  }

  private parseInfo(line: string) {
    const result: any = {};
    const parts = line.split(' ');
    
    for (let i = 0; i < parts.length; i++) {
        if (parts[i] === 'depth' && i + 1 < parts.length) {
            result.depth = parseInt(parts[i+1], 10);
        } else if (parts[i] === 'score' && i + 2 < parts.length && parts[i+1] === 'cp') {
            result.score = parseInt(parts[i+2], 10);
        } else if (parts[i] === 'nodes' && i + 1 < parts.length) {
            result.nodes = parseInt(parts[i+1], 10);
        } else if (parts[i] === 'time' && i + 1 < parts.length) {
            result.time = parseInt(parts[i+1], 10);
        } else if (parts[i] === 'pv' && i + 1 < parts.length) {
            result.pv = parts.slice(i + 1);
            break; // pv is usually at the end
        }
    }
    
    // Only return if we have depth and score
    if (result.depth !== undefined && result.score !== undefined) {
        return result;
    }
    return null;
  }

  private updateActivity(id: string) {
    const session = this.active.get(id);
    if (session) {
      session.lastActivityAt = Date.now();
      clearTimeout(session.idleTimer);
      session.idleTimer = setTimeout(() => this.terminateSession(id, 'idle'), IDLE_TIMEOUT_MS);
    }
  }

  private terminateSession(id: string, reason: string) {
    const session = this.active.get(id);
    if (!session) {
        // Might be in waitQueue
        const qIndex = this.waitQueue.findIndex(item => item.id === id);
        if (qIndex !== -1) {
            this.waitQueue.splice(qIndex, 1);
            this.updateQueuePositions();
        }
        return;
    }

    console.log(`Terminating session ${id} (Reason: ${reason})`);
    
    clearTimeout(session.idleTimer);
    clearTimeout(session.sessionTimer);

    if (reason === 'session_expired' || reason === 'idle') {
        if (session.ws.readyState === WebSocket.OPEN) {
            session.ws.send(JSON.stringify({ type: 'session_expired', reason }));
            session.ws.close();
        }
    }

    if (session.process.stdin) {
      session.process.stdin.write('quit\n');
    }
    
    // Graceful shutdown
    session.process.kill('SIGTERM');
    setTimeout(() => {
        if (!session.process.killed) {
            session.process.kill('SIGKILL');
        }
    }, 3000);

    this.active.delete(id);
    this.dequeueNext();
  }

  private dequeueNext() {
    if (this.waitQueue.length > 0 && this.active.size < MAX_CONCURRENT) {
      const next = this.waitQueue.shift();
      if (next) {
        this.spawnEngine(next.ws, next.id);
        this.updateQueuePositions();
      }
    }
  }
}

export const enginePool = new EnginePoolManager();
