import { spawn, ChildProcess } from 'child_process';
import { WebSocket } from 'ws';
import path from 'path';
import fs from 'fs';
import { parseUciInfo } from './uci-parser';

interface EngineSession {
  id: string;
  process: ChildProcess;
  ws: WebSocket;
  createdAt: number;
  lastActivityAt: number;
  idleTimer: NodeJS.Timeout;
  sessionTimer: NodeJS.Timeout;
  currentDepth: number;
  pvs: any[];
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
      currentDepth: 0,
      pvs: []
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
              engineProcess.stdin?.write(`setoption name OwnBook value true\n`);
              engineProcess.stdin?.write(`setoption name BookDepth value 30\n`);
              engineProcess.stdin?.write(`setoption name MultiPV value 3\n`);
              engineProcess.stdin?.write('isready\n');
            } else if (trimmed === 'readyok') {
              isReady = true;
              ws.send(JSON.stringify({ type: 'ready' }));
            }
          } else {
            // Parse engine output
            if (trimmed.startsWith('info ')) {
              const parsedInfo = parseUciInfo(trimmed);
              if (parsedInfo && parsedInfo.depth) {
                // When starting a new depth, clear previous PVs
                if (parsedInfo.depth !== session.currentDepth) {
                    session.currentDepth = parsedInfo.depth;
                    session.pvs = [];
                }
                const multipv = parsedInfo.multipv || 1;
                const existingIdx = session.pvs.findIndex(p => p.multipv === multipv);
                
                const pvEntry = {
                    multipv,
                    score: parsedInfo.score,
                    mate: parsedInfo.mate,
                    pv: parsedInfo.pv
                };
                
                if (existingIdx >= 0) {
                    session.pvs[existingIdx] = pvEntry;
                } else {
                    session.pvs.push(pvEntry);
                }
                
                session.pvs.sort((a, b) => a.multipv - b.multipv);

                ws.send(JSON.stringify({ 
                    type: 'info', 
                    depth: session.currentDepth,
                    nodes: parsedInfo.nodes,
                    time: parsedInfo.time,
                    pvs: session.pvs
                }));
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
      // Use `position fen` when a custom FEN is provided (e.g. Analysis Mode);
      // fall back to `position startpos` for normal games.
      const isStartPos = !data.fen || data.fen === 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
      const positionCmd = isStartPos
        ? `position startpos${movesStr}\n`
        : `position fen ${data.fen}${movesStr}\n`;
      session.process.stdin.write(positionCmd);
      
      let goCommand = 'go movetime 3000\n'; // default standard
      if (data.difficulty === 'blitz') {
          goCommand = 'go movetime 1000\n';
      } else if (data.difficulty === 'deep') {
          goCommand = 'go depth 8\n';
      } else if (data.difficulty === 'standard') {
          goCommand = 'go movetime 3000\n';
      }

      session.process.stdin.write(goCommand);
    } else if (data.type === 'position') {
      // Sync engine position without triggering a search (Analysis Mode FEN load)
      const movesStr = data.moves && data.moves.length > 0 ? ` moves ${data.moves.join(' ')}` : '';
      const isStartPos = !data.fen || data.fen === 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
      const positionCmd = isStartPos
        ? `position startpos${movesStr}\n`
        : `position fen ${data.fen}${movesStr}\n`;
      session.process.stdin.write(positionCmd);
    } else if (data.type === 'analyze') {
      const movesStr = data.moves && data.moves.length > 0 ? ` moves ${data.moves.join(' ')}` : '';
      const isStartPos = !data.fen || data.fen === 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
      const positionCmd = isStartPos
        ? `position startpos${movesStr}\n`
        : `position fen ${data.fen}${movesStr}\n`;
      session.process.stdin.write(positionCmd);
      session.process.stdin.write('go depth 30\n');
    } else if (data.type === 'newgame') {
        session.process.stdin.write(`ucinewgame\n`);
        session.process.stdin.write(`isready\n`);
    } else if (data.type === 'stop') {
        session.process.stdin.write(`stop\n`);
    } else if (data.type === 'setoption') {
        session.process.stdin.write(`setoption name ${data.name} value ${data.value}\n`);
    }
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
