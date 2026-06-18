import { spawn, ChildProcess } from 'child_process';
import { WebSocket } from 'ws';
import path from 'path';
import fs from 'fs';
import { parseUciInfo, parseBestMove } from './uci-parser';

// ── Ponder state machine ──────────────────────────────────────────────────────

type PonderPhase =
  | { phase: 'idle' }
  | { phase: 'thinking'; searchMode: 'tc' | 'analysis'; wtime: number; btime: number; winc: number; binc: number; depth?: number }
  | {
      phase: 'pondering';
      ponderPly: number;        // ply count at the moment pondering starts
      ponderMove: string;       // the move the engine predicted the opponent plays
      baseFen: string;          // the startFen of the current game
      baseMoves: string[];      // full moves list up to (and including) the engine's reply
      awaitingStopAck: boolean; // true after we sent 'stop', waiting for dummy bestmove
      pendingUserMove: string;  // user's actual move — buffered until stop-ack arrives
      pendingWtime: number;
      pendingBtime: number;
      pendingWinc: number;
      pendingBinc: number;
      pendingDepth?: number;
      pendingMultiPv: number;
    };

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
  // Position tracking (shadow of what the engine has been told)
  startFen: string;
  uciMoves: string[];
  ponderState: PonderPhase;
  lineBuffer: string;
}

const MAX_CONCURRENT = 10;
const IDLE_TIMEOUT_MS = 2 * 60_000;
const SESSION_MAX_MS = 2 * 60 * 60_000;
const HASH_SIZE_MB = 32;

// When a time-control game is active we force MultiPV=1 so that pondering
// is meaningful (we ponder the single best continuation).
const TC_MULTIPV = 1;
const ANALYSIS_MULTIPV = 3;

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
      pvs: [],
      startFen: 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',
      uciMoves: [],
      ponderState: { phase: 'idle' },
      lineBuffer: '',
    };

    this.active.set(id, session);

    if (engineProcess.stdin && engineProcess.stdout) {
      engineProcess.stderr?.on('data', () => {});
      engineProcess.stdin.write('uci\n');

      let isReady = false;

      engineProcess.stdout.on('data', (data) => {
        session.lineBuffer += data.toString();
        const lines = session.lineBuffer.split('\n');
        session.lineBuffer = lines.pop() || '';

        for (const line of lines) {
          const trimmed = line.trim();
          if (!trimmed) continue;

          if (process.env.DEBUG === 'true') {
            console.log(`[Engine ${id}] ${trimmed}`);
          }
          this.updateActivity(id);

          if (!isReady) {
            // ── Initialisation handshake ─────────────────────────────────
            if (trimmed === 'uciok') {
              engineProcess.stdin?.write(`setoption name Hash value ${HASH_SIZE_MB}\n`);
              engineProcess.stdin?.write(`setoption name OwnBook value true\n`);
              engineProcess.stdin?.write(`setoption name BookDepth value 30\n`);
              engineProcess.stdin?.write(`setoption name MultiPV value ${ANALYSIS_MULTIPV}\n`);
              engineProcess.stdin?.write('isready\n');
            } else if (trimmed === 'readyok') {
              isReady = true;
              ws.send(JSON.stringify({ type: 'ready' }));
            }
          } else {
            // ── Normal message dispatch ──────────────────────────────────
            if (trimmed.startsWith('info ')) {
              this.handleInfoLine(id, trimmed);
            } else if (trimmed.startsWith('bestmove ')) {
              this.handleBestMoveLine(id, trimmed);
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

  // ── Engine stdout handlers ──────────────────────────────────────────────────

  private handleInfoLine(id: string, trimmed: string) {
    const session = this.active.get(id);
    if (!session) return;

    // Suppress info lines while we're waiting for the stop-ack dummy bestmove
    const ps = session.ponderState;
    if (ps.phase === 'pondering' && ps.awaitingStopAck) return;

    const parsedInfo = parseUciInfo(trimmed);
    if (parsedInfo && parsedInfo.depth) {
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
        pv: parsedInfo.pv,
      };

      if (existingIdx >= 0) {
        session.pvs[existingIdx] = pvEntry;
      } else {
        session.pvs.push(pvEntry);
      }
      session.pvs.sort((a, b) => a.multipv - b.multipv);

      session.ws.send(JSON.stringify({
        type: 'info',
        depth: session.currentDepth,
        nodes: parsedInfo.nodes,
        time: parsedInfo.time,
        pvs: session.pvs,
      }));
    }
  }

  private handleBestMoveLine(id: string, trimmed: string) {
    const session = this.active.get(id);
    if (!session) return;

    const parsed = parseBestMove(trimmed);
    if (!parsed) return;

    const ps = session.ponderState;

    // ── Stop-ack discard ─────────────────────────────────────────────────
    // We sent 'stop' to the ponder search; this dummy bestmove is the engine
    // confirming the ponder search is dead.  Issue the real timed search now.
    if (ps.phase === 'pondering' && ps.awaitingStopAck) {
      if (ps.pendingUserMove === '') {
        session.ponderState = { phase: 'idle' };
        return;
      }
      console.log(`[Ponder ${id}] Stop-ack received — issuing real 'go' command`);
      const { pendingUserMove, pendingWtime, pendingBtime, pendingWinc, pendingBinc, pendingDepth, pendingMultiPv,
              baseFen, baseMoves } = ps;

      // Build the real position (opponent played pendingUserMove)
      const realMoves = [...baseMoves, pendingUserMove];
      session.process.stdin?.write(`setoption name MultiPV value ${pendingMultiPv}\n`);
      this.sendPositionAndGo(session, baseFen, realMoves, pendingWtime, pendingBtime, pendingWinc, pendingBinc, pendingDepth);
      return;
    }

    // ── Normal thinking result ────────────────────────────────────────────
    if (ps.phase === 'thinking') {
      const { bestMove, ponderMove } = parsed;

      // Send best move to the frontend
      session.ws.send(JSON.stringify({ type: 'bestmove', move: bestMove }));
      console.log(`[Engine ${id}] bestmove=${bestMove} ponder=${ponderMove ?? 'none'}`);

      // Start pondering if we have a ponder move
      if (ponderMove && session.process.stdin && ps.searchMode === 'tc') {
        const ponderMoves = [...session.uciMoves, bestMove];
        const isStartPos = session.startFen === 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
        const posCmd = isStartPos
          ? `position startpos moves ${ponderMoves.join(' ')}\n`
          : `position fen ${session.startFen} moves ${ponderMoves.join(' ')}\n`;

        console.log(`[Ponder ${id}] Starting ponder on move: ${ponderMove}`);
        session.process.stdin.write(posCmd);
        session.process.stdin.write('go ponder\n');

        session.ponderState = {
          phase: 'pondering',
          ponderPly: ponderMoves.length,
          ponderMove,
          baseFen: session.startFen,
          baseMoves: ponderMoves,
          awaitingStopAck: false,
          pendingUserMove: '',
          pendingWtime: 0,
          pendingBtime: 0,
          pendingWinc: 0,
          pendingBinc: 0,
          pendingMultiPv: 1,
        };
      } else {
        session.ponderState = { phase: 'idle' };
      }
      return;
    }

    // ── Idle / unexpected bestmove — just forward it ─────────────────────
    if (parsed.bestMove && parsed.bestMove !== '0000') {
      session.ws.send(JSON.stringify({ type: 'bestmove', move: parsed.bestMove }));
    }
    session.ponderState = { phase: 'idle' };
  }

  // ── Client message handler ─────────────────────────────────────────────────

  private handleClientMessage(id: string, data: any) {
    const session = this.active.get(id);
    if (!session || !session.process.stdin) return;

    this.updateActivity(id);

    // ── move (time-controlled game) ────────────────────────────────────────
    if (data.type === 'move') {
      const wtime: number = data.wtime ?? 0;
      const btime: number = data.btime ?? 0;
      const winc:  number = data.winc  ?? 0;
      const binc:  number = data.binc  ?? 0;
      const depth: number | undefined = data.depth;
      const targetMultiPv: number = data.multiPv ?? 1;
      const hasTC = wtime > 0 || btime > 0;

      // Update position shadow
      let startFen = session.startFen;
      if (!startFen || (data.moves && data.moves.length === 0)) {
        startFen = data.fen || 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
      }
      session.startFen = startFen;
      session.uciMoves = data.moves ?? [];

      const ps = session.ponderState;

      if (ps.phase === 'pondering') {
        // The user has made a move while the engine is pondering.
        // Determine the last move the user played (last element of data.moves).
        const userMove = data.moves?.length > 0
          ? data.moves[data.moves.length - 1]
          : null;
        const incomingPly = data.moves?.length ?? 0;

        if (!userMove) {
          // Can't determine user's move — fall back to stop + resync
          this.stopPonderAndGo(session, startFen, data.moves, wtime, btime, winc, binc, depth);
          return;
        }

        if (incomingPly > ps.ponderPly && userMove === ps.ponderMove) {
          // ── Ponder HIT ─────────────────────────────────────────────────
          // The opponent played exactly the move we were pondering.
          // Stop the ponder search, then issue a timed go on the same position.
          console.log(`[Ponder ${id}] HIT — user played predicted ${userMove}`);
          this.stopPonderAndGo(session, ps.baseFen, ps.baseMoves.concat(userMove), wtime, btime, winc, binc, depth, targetMultiPv);
        } else if (incomingPly > ps.ponderPly) {
          // ── Ponder MISS ────────────────────────────────────────────────
          // Opponent played a different move — stop and resync.
          console.log(`[Ponder ${id}] MISS — expected ${ps.ponderMove}, got ${userMove}`);
          this.stopPonderAndGo(session, startFen, data.moves, wtime, btime, winc, binc, depth, targetMultiPv);
        } else {
          // ── Ponder RESYNC ──────────────────────────────────────────────
          // Same or fewer moves than when pondering started.
          // Do not treat this as a HIT.
          // Stop/resync safely.
          console.log(`[Ponder ${id}] RESYNC — incomingPly=${incomingPly} <= ponderPly=${ps.ponderPly}`);
          this.stopPonderAndGo(session, startFen, data.moves, wtime, btime, winc, binc, depth, targetMultiPv);
        }
        return;
      }

      // Not pondering — issue position + go directly.
      session.process.stdin.write(`setoption name MultiPV value ${targetMultiPv}\n`);
      this.sendPositionAndGo(session, startFen, data.moves, wtime, btime, winc, binc, depth);
      session.ponderState = { phase: 'thinking', searchMode: 'tc', wtime, btime, winc, binc, depth };
      return;
    }

    // ── position (Analysis Mode FEN load, no search) ───────────────────────
    if (data.type === 'position') {
      this.stopPonderSilently(session);
      let startFen = session.startFen;
      if (!startFen || (data.moves && data.moves.length === 0)) {
        startFen = data.fen || 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
      }
      const movesStr = data.moves?.length > 0 ? ` moves ${data.moves.join(' ')}` : '';
      const isStartPos = startFen === 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
      const positionCmd = isStartPos
        ? `position startpos${movesStr}\n`
        : `position fen ${startFen}${movesStr}\n`;
      session.process.stdin.write(positionCmd);
      session.startFen = startFen;
      session.uciMoves = data.moves ?? [];
      return;
    }

    // ── analyze (Analysis Mode deep search) ───────────────────────────────
    if (data.type === 'analyze') {
      const targetMultiPv = data.multiPv ?? ANALYSIS_MULTIPV;
      this.stopPonderSilently(session);
      // Restore MultiPV for analysis
      session.process.stdin.write(`setoption name MultiPV value ${targetMultiPv}\n`);
      let startFen = session.startFen;
      if (!startFen || (data.moves && data.moves.length === 0)) {
        startFen = data.fen || 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
      }
      const movesStr = data.moves?.length > 0 ? ` moves ${data.moves.join(' ')}` : '';
      const isStartPos = startFen === 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
      const positionCmd = isStartPos
        ? `position startpos${movesStr}\n`
        : `position fen ${startFen}${movesStr}\n`;
      session.process.stdin.write(positionCmd);
      const targetDepth = data.depth || 30;
      session.process.stdin.write(`go depth ${targetDepth}\n`);
      session.startFen = startFen;
      session.uciMoves = data.moves ?? [];
      session.ponderState = {
        phase: 'thinking',
        searchMode: 'analysis',
        wtime: 0,
        btime: 0,
        winc: 0,
        binc: 0,
        depth: targetDepth,
      };
      return;
    }

    // ── newgame ────────────────────────────────────────────────────────────
    if (data.type === 'newgame') {
      this.stopPonderSilently(session);
      // Restore analysis MultiPV default between games
      session.process.stdin.write(`setoption name MultiPV value ${ANALYSIS_MULTIPV}\n`);
      session.process.stdin.write('ucinewgame\n');
      session.process.stdin.write('isready\n');
      session.startFen   = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
      session.uciMoves = [];
      session.ponderState  = { phase: 'idle' };
      return;
    }

    // ── stop ──────────────────────────────────────────────────────────────
    if (data.type === 'stop') {
      this.stopPonderSilently(session);
      session.process.stdin.write('stop\n');
      return;
    }

    // ── releaseSession ─────────────────────────────────────────────────────
    if (data.type === 'releaseSession') {
      this.stopPonderSilently(session);
      session.process.stdin.write('stop\n');
      this.terminateSession(id, 'released');
      return;
    }

    // ── setoption ─────────────────────────────────────────────────────────
    if (data.type === 'setoption') {
      session.process.stdin.write(`setoption name ${data.name} value ${data.value}\n`);
      return;
    }
  }

  // ── Ponder helpers ─────────────────────────────────────────────────────────

  /**
   * Stop the ponder search, buffer the pending user move + TC, and wait for
   * the dummy bestmove stop-ack before issuing the real go command.
   */
  private stopPonderAndGo(
    session: EngineSession,
    startFen: string,
    moves: string[],
    wtime: number,
    btime: number,
    winc: number,
    binc: number,
    depth?: number,
    targetMultiPv: number = 1,
  ) {
    if (!session.process.stdin) return;

    const ps = session.ponderState;
    const userMove = moves.length > 0 ? moves[moves.length - 1] : '';

    if (ps.phase === 'pondering') {
      // Mark that we're waiting for the stop-ack dummy bestmove
      session.ponderState = {
        ...ps,
        awaitingStopAck: true,
        pendingUserMove: userMove,
        pendingWtime: wtime,
        pendingBtime: btime,
        pendingWinc: winc,
        pendingBinc: binc,
        pendingDepth: depth,
        pendingMultiPv: targetMultiPv,
        baseFen:   startFen,
        baseMoves: moves,
      };
      session.process.stdin.write('stop\n');
    } else {
      // Not pondering — issue immediately
      session.process.stdin.write(`setoption name MultiPV value ${targetMultiPv}\n`);
      this.sendPositionAndGo(session, startFen, moves, wtime, btime, winc, binc, depth);
      session.ponderState = { phase: 'thinking', searchMode: 'tc', wtime, btime, winc, binc, depth };
    }
  }

  /**
   * Stop a ponder search without transitioning to thinking (used on newgame,
   * analysis switch, etc.).  The dummy bestmove will be swallowed by
   * awaitingStopAck without triggering a new go.
   */
  private stopPonderSilently(session: EngineSession) {
    if (!session.process.stdin) return;
    const ps = session.ponderState;
    if (ps.phase === 'pondering') {
      session.ponderState = {
        ...ps,
        awaitingStopAck: true,
        pendingUserMove: '',
        pendingWtime: 0,
        pendingBtime: 0,
        pendingWinc: 0,
        pendingBinc: 0,
        pendingMultiPv: 1,
      };
      session.process.stdin.write('stop\n');
    } else if (ps.phase === 'thinking') {
      session.process.stdin.write('stop\n');
      session.ponderState = { phase: 'idle' };
    }
  }

  /**
   * Send a position + timed go command. Updates ponderState to 'thinking'.
   */
  private sendPositionAndGo(
    session: EngineSession,
    startFen: string,
    moves: string[],
    wtime: number,
    btime: number,
    winc: number,
    binc: number,
    depth?: number,
  ) {
    if (!session.process.stdin) return;

    const movesStr  = moves.length > 0 ? ` moves ${moves.join(' ')}` : '';
    const isStartPos = startFen === 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
    const posCmd = isStartPos
      ? `position startpos${movesStr}\n`
      : `position fen ${startFen}${movesStr}\n`;

    session.process.stdin.write(posCmd);

    const hasTC = wtime > 0 || btime > 0;
    let goCmd: string;
    if (depth !== undefined) {
      // Training Mode: ignore clock entirely, enforce strict depth
      goCmd = `go depth ${depth}\n`;
    } else if (hasTC) {
      goCmd = `go wtime ${wtime} btime ${btime} winc ${winc} binc ${binc}\n`;
    } else {
      // Fallback: fixed movetime for difficulty-based games
      goCmd = 'go movetime 3000\n';
    }

    session.process.stdin.write(goCmd);
    session.ponderState = { phase: 'thinking', searchMode: 'tc', wtime, btime, winc, binc };
    session.startFen = startFen;
    session.uciMoves = moves;
  }

  // ── Session lifecycle ──────────────────────────────────────────────────────

  private updateActivity(id: string) {
    const session = this.active.get(id);
    if (session) {
      session.lastActivityAt = Date.now();
      clearTimeout(session.idleTimer);
      session.idleTimer = setTimeout(() => this.terminateSession(id, 'idle'), IDLE_TIMEOUT_MS);
      clearTimeout(session.sessionTimer);
      session.sessionTimer = setTimeout(() => this.terminateSession(id, 'session_expired'), SESSION_MAX_MS);
    }
  }

  private terminateSession(id: string, reason: string) {
    const session = this.active.get(id);
    if (!session) {
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

    if (reason === 'session_expired' || reason === 'idle' || reason === 'released') {
      if (session.ws.readyState === WebSocket.OPEN) {
        if (reason === 'released') {
          session.ws.send(JSON.stringify({ type: 'released' }));
        } else {
          session.ws.send(JSON.stringify({ type: 'session_expired', reason }));
        }
        session.ws.close();
      }
    }

    if (session.process.stdin) {
      session.process.stdin.write('quit\n');
    }

    session.process.kill('SIGTERM');
    setTimeout(() => {
      if (session.process.exitCode === null && session.process.signalCode === null) {
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
