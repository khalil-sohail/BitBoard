import { spawn, ChildProcess, SpawnOptions } from 'child_process';
import { RawData, WebSocket } from 'ws';
import path from 'path';
import fs from 'fs';
import { parseUciInfo, parseBestMove } from './uci-parser';
import {
  ANALYSIS_MULTIPV,
  ClientMessage,
  DEFAULT_START_FEN,
  buildGoCommand,
  buildPositionCommand,
  buildSetOptionCommand,
  parseClientMessage,
  writeUciCommand,
} from './engine-protocol';
import type { EngineOptionName } from './engine-protocol';
import { getDifficultyProfile } from './engine-difficulty';

interface SessionPv {
  multipv: number;
  score?: number;
  mate?: number;
  pv?: string[];
}

// ── Ponder state machine ──────────────────────────────────────────────────────

type PonderPhase =
  | { phase: 'idle' }
  | { phase: 'thinking'; requestId: number; searchMode: 'tc' | 'analysis'; wtime: number; btime: number; winc: number; binc: number; depth?: number; movetimeMs?: number }
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
      pendingMovetimeMs?: number;
      pendingMultiPv: number;
      pendingRequestId?: number;
    };

type ActiveSearchState = 'running' | 'stopping';

interface ActiveSearch {
  requestId: number;
  generation: number;
  state: ActiveSearchState;
  searchMode: 'tc' | 'analysis';
}

type PendingOperation =
  | {
      type: 'search';
      requestId: number;
      searchMode: 'tc' | 'analysis';
      startFen: string;
      moves: string[];
      wtime: number;
      btime: number;
      winc: number;
      binc: number;
      depth?: number;
      movetimeMs?: number;
      multiPv: number;
    }
  | { type: 'newgame' }
  | { type: 'position'; startFen: string; moves: string[] }
  | { type: 'setoption'; name: EngineOptionName; value: number | boolean };

interface EngineSession {
  id: string;
  process: ChildProcess;
  ws: WebSocket;
  createdAt: number;
  lastActivityAt: number;
  idleTimer: NodeJS.Timeout;
  sessionTimer: NodeJS.Timeout;
  currentDepth: number;
  pvs: SessionPv[];
  // Position tracking (shadow of what the engine has been told)
  startFen: string;
  uciMoves: string[];
  ponderState: PonderPhase;
  activeSearch: ActiveSearch | null;
  pendingOperation: PendingOperation | null;
  searchGeneration: number;
  lineBuffer: string;
}

interface QueuedClient {
  id: string;
  ws: WebSocket;
  enqueuedAt: number;
  removeCloseListener: () => void;
}

interface EnginePoolOptions {
  maxConcurrent?: number;
  enginePath?: string;
  idleTimeoutMs?: number;
  sessionMaxMs?: number;
  spawnEngineProcess?: (enginePath: string, args: string[], options?: SpawnOptions) => ChildProcess;
}

const DEFAULT_MAX_CONCURRENT = 10;
const IDLE_TIMEOUT_MS = 2 * 60_000;
const SESSION_MAX_MS = 2 * 60 * 60_000;
const HASH_SIZE_MB = 32;

export class EnginePoolManager {
  private active = new Map<string, EngineSession>();
  private waitQueue: QueuedClient[] = [];
  private maxConcurrent: number;
  private enginePathOverride?: string;
  private idleTimeoutMs: number;
  private sessionMaxMs: number;
  private spawnEngineProcess: (enginePath: string, args: string[], options?: SpawnOptions) => ChildProcess;

  public constructor(options: EnginePoolOptions = {}) {
    this.maxConcurrent = options.maxConcurrent ?? DEFAULT_MAX_CONCURRENT;
    this.enginePathOverride = options.enginePath;
    this.idleTimeoutMs = options.idleTimeoutMs ?? IDLE_TIMEOUT_MS;
    this.sessionMaxMs = options.sessionMaxMs ?? SESSION_MAX_MS;
    this.spawnEngineProcess = options.spawnEngineProcess ?? ((enginePath, args, spawnOptions) => (
      spawn(enginePath, args, spawnOptions ?? {})
    ));
  }

  public handleConnection(ws: WebSocket, id: string) {
    if (!this.isSocketOpen(ws)) {
      return;
    }

    if (this.active.size < this.maxConcurrent) {
      this.spawnEngine(ws, id);
    } else {
      if (this.waitQueue.length >= 20) {
        this.sendIfOpen(ws, JSON.stringify({ type: 'error', message: 'Server is full. Try again later.' }));
        ws.close();
        return;
      }
      this.enqueueClient(ws, id);
    }
  }

  public getDebugState() {
    return {
      activeIds: Array.from(this.active.keys()),
      queuedIds: this.waitQueue.map(item => item.id),
    };
  }

  private enqueueClient(ws: WebSocket, id: string) {
    if (!this.isSocketOpen(ws) || this.waitQueue.some(item => item.id === id)) {
      return;
    }

    const handleQueuedClose = () => {
      this.removeQueuedClient(id);
    };

    ws.once('close', handleQueuedClose);

    const entry: QueuedClient = {
      id,
      ws,
      enqueuedAt: Date.now(),
      removeCloseListener: () => {
        ws.off('close', handleQueuedClose);
      },
    };

    this.waitQueue.push(entry);

    if (!this.isSocketOpen(ws)) {
      this.removeQueuedClient(id);
      return;
    }

    this.updateQueuePositions();
  }

  private removeQueuedClient(id: string, updatePositions = true): boolean {
    const qIndex = this.waitQueue.findIndex(item => item.id === id);
    if (qIndex === -1) {
      return false;
    }

    const [entry] = this.waitQueue.splice(qIndex, 1);
    entry.removeCloseListener();

    if (updatePositions) {
      this.updateQueuePositions();
    }

    return true;
  }

  private pruneClosedQueuedClients(): boolean {
    let changed = false;

    for (let index = this.waitQueue.length - 1; index >= 0; index -= 1) {
      const entry = this.waitQueue[index];
      if (!this.isSocketOpen(entry.ws)) {
        this.waitQueue.splice(index, 1);
        entry.removeCloseListener();
        changed = true;
      }
    }

    return changed;
  }

  private updateQueuePositions() {
    this.pruneClosedQueuedClients();

    const failedIds: string[] = [];

    this.waitQueue.forEach((item, index) => {
      const sent = this.sendIfOpen(item.ws, JSON.stringify({ type: 'queued', position: index + 1 }));
      if (!sent) {
        failedIds.push(item.id);
      }
    });

    if (failedIds.length > 0) {
      failedIds.forEach(id => this.removeQueuedClient(id, false));
      this.updateQueuePositions();
    }
  }

  private resolveEnginePath() {
    if (this.enginePathOverride) {
      return this.enginePathOverride;
    }

    return process.env.ENGINE_PATH || path.join(process.cwd(), '../engine/chess-engine');
  }

  private spawnEngine(ws: WebSocket, id: string) {
    if (!this.isSocketOpen(ws)) {
      return;
    }

    const enginePath = this.resolveEnginePath();

    if (!fs.existsSync(enginePath)) {
      this.sendIfOpen(ws, JSON.stringify({ type: 'error', message: `Engine binary not found at ${enginePath}` }));
      console.error(`Engine binary not found at ${enginePath}`);
      this.terminateSession(id, 'error');
      return;
    }

    const engineProcess = this.spawnEngineProcess(enginePath, ['--mode=gui'], {
      cwd: path.dirname(enginePath),
    });

    const session: EngineSession = {
      id,
      process: engineProcess,
      ws,
      createdAt: Date.now(),
      lastActivityAt: Date.now(),
      idleTimer: setTimeout(() => this.terminateSession(id, 'idle'), this.idleTimeoutMs),
      sessionTimer: setTimeout(() => this.terminateSession(id, 'session_expired'), this.sessionMaxMs),
      currentDepth: 0,
      pvs: [],
      startFen: DEFAULT_START_FEN,
      uciMoves: [],
      ponderState: { phase: 'idle' },
      activeSearch: null,
      pendingOperation: null,
      searchGeneration: 0,
      lineBuffer: '',
    };

    this.active.set(id, session);

    if (engineProcess.stdin && engineProcess.stdout) {
      engineProcess.stderr?.on('data', () => {});
      writeUciCommand(engineProcess.stdin, 'uci');

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
              writeUciCommand(engineProcess.stdin, buildSetOptionCommand('Hash', HASH_SIZE_MB));
              writeUciCommand(engineProcess.stdin, buildSetOptionCommand('OwnBook', true));
              writeUciCommand(engineProcess.stdin, buildSetOptionCommand('BookDepth', 30));
              writeUciCommand(engineProcess.stdin, buildSetOptionCommand('MultiPV', ANALYSIS_MULTIPV));
              writeUciCommand(engineProcess.stdin, 'isready');
            } else if (trimmed === 'readyok') {
              isReady = true;
              this.sendIfOpen(ws, JSON.stringify({ type: 'ready' }));
            }
          } else {
            // ── Normal message dispatch ──────────────────────────────────
            if (trimmed.startsWith('info ')) {
              this.handleInfoLine(id, trimmed);
            } else if (trimmed.startsWith('bestmove ')) {
              this.handleBestMoveLine(id, trimmed);
            } else if (trimmed === 'readyok') {
              this.sendIfOpen(ws, JSON.stringify({ type: 'ready' }));
            }
          }
        }
      });

      engineProcess.on('error', (err) => {
        console.error(`Engine error (Session ${id}):`, err);
        this.sendIfOpen(ws, JSON.stringify({ type: 'error', message: 'Engine process error' }));
        this.terminateSession(id, 'error');
      });

      engineProcess.on('exit', (code) => {
        console.log(`Engine exited (Session ${id}) with code ${code}`);
        if (this.active.has(id)) {
          this.sendIfOpen(ws, JSON.stringify({ type: 'error', message: 'Engine crashed unexpectedly' }));
          this.terminateSession(id, 'error');
        }
      });
    } else {
      this.sendIfOpen(ws, JSON.stringify({ type: 'error', message: 'Failed to start engine' }));
      this.terminateSession(id, 'error');
    }

    ws.on('message', (message) => {
      this.handleRawClientMessage(id, ws, message);
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
    if (!session.activeSearch || session.activeSearch.state !== 'running') return;

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
        requestId: session.activeSearch.requestId,
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
    const activeSearch = session.activeSearch;

    // ── Stop-ack discard ─────────────────────────────────────────────────
    // We sent 'stop' to the ponder search; this dummy bestmove is the engine
    // confirming the ponder search is dead.  Issue the real timed search now.
    if (ps.phase === 'pondering' && ps.awaitingStopAck) {
      if (ps.pendingUserMove === '') {
        session.ponderState = { phase: 'idle' };
        this.runPendingOperation(session);
        return;
      }
      console.log(`[Ponder ${id}] Stop-ack received — issuing real 'go' command`);
      const { pendingUserMove, pendingWtime, pendingBtime, pendingWinc, pendingBinc, pendingDepth, pendingMovetimeMs, pendingMultiPv,
              pendingRequestId, baseFen, baseMoves } = ps;
      if (pendingRequestId === undefined) {
        session.ponderState = { phase: 'idle' };
        this.runPendingOperation(session);
        return;
      }

      // Build the real position (opponent played pendingUserMove)
      const realMoves = [...baseMoves, pendingUserMove];
      writeUciCommand(session.process.stdin, buildSetOptionCommand('MultiPV', pendingMultiPv));
      this.sendPositionAndGo(session, pendingRequestId, 'tc', baseFen, realMoves, pendingWtime, pendingBtime, pendingWinc, pendingBinc, pendingDepth, pendingMovetimeMs);
      return;
    }

    if (!activeSearch) {
      session.ponderState = { phase: 'idle' };
      this.runPendingOperation(session);
      return;
    }

    if (activeSearch.state === 'stopping') {
      session.activeSearch = null;
      session.ponderState = { phase: 'idle' };
      this.runPendingOperation(session);
      return;
    }

    // ── Normal thinking result ────────────────────────────────────────────
    if (ps.phase === 'thinking' && ps.requestId === activeSearch.requestId) {
      const { bestMove, ponderMove } = parsed;

      // Send best move to the frontend
      session.ws.send(JSON.stringify({
        type: 'bestmove',
        requestId: activeSearch.requestId,
        move: bestMove === '0000' ? null : bestMove,
      }));
      console.log(`[Engine ${id}] bestmove=${bestMove} ponder=${ponderMove ?? 'none'}`);
      session.activeSearch = null;

      // Start pondering if we have a ponder move
      if (ponderMove && session.process.stdin && ps.searchMode === 'tc') {
        const ponderMoves = [...session.uciMoves, bestMove];

        console.log(`[Ponder ${id}] Starting ponder on move: ${ponderMove}`);
        writeUciCommand(session.process.stdin, buildPositionCommand(session.startFen, ponderMoves));
        writeUciCommand(session.process.stdin, 'go ponder');

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
      this.runPendingOperation(session);
      return;
    }

    // ── Idle / unexpected bestmove — stale search output, swallow it. ─────
    session.ponderState = { phase: 'idle' };
    session.activeSearch = null;
    this.runPendingOperation(session);
  }

  // ── Client message handler ─────────────────────────────────────────────────

  private handleRawClientMessage(id: string, ws: WebSocket, raw: RawData) {
    const parsed = parseClientMessage(raw);
    if (parsed.ok === false) {
      this.sendIfOpen(ws, JSON.stringify({
        type: 'error',
        code: parsed.error.code,
        message: parsed.error.message,
      }));
      return;
    }

    this.handleClientMessage(id, parsed.value);
  }

  private handleClientMessage(id: string, data: ClientMessage) {
    const session = this.active.get(id);
    if (!session || !session.process.stdin) return;

    this.updateActivity(id);

    // ── move (time-controlled game) ────────────────────────────────────────
    if (data.type === 'move') {
      const moves = data.moves;
      const wtime = data.wtime;
      const btime = data.btime;
      const winc  = data.winc;
      const binc  = data.binc;
      const hasClock = wtime > 0 || btime > 0;
      const opponentProfile = hasClock
        ? { depth: data.depth, multiPv: data.multiPv }
        : getDifficultyProfile(data.difficulty, 'opponent');
      const depth = opponentProfile.depth;
      const movetimeMs = hasClock ? undefined : opponentProfile.movetimeMs;
      const targetMultiPv = opponentProfile.multiPv;

      // Update position shadow
      const startFen = data.fen;
      session.startFen = startFen;
      session.uciMoves = moves;

      const ps = session.ponderState;

      if (ps.phase === 'pondering') {
        // The user has made a move while the engine is pondering.
        // Determine the last move the user played (last element of data.moves).
        const userMove = moves.length > 0
          ? moves[moves.length - 1]
          : null;
        const incomingPly = moves.length;

        if (!userMove) {
          // Can't determine user's move — fall back to stop + resync
          this.stopPonderAndGo(session, data.requestId, startFen, moves, wtime, btime, winc, binc, depth, movetimeMs);
          return;
        }

        if (incomingPly > ps.ponderPly && userMove === ps.ponderMove) {
          // ── Ponder HIT ─────────────────────────────────────────────────
          // The opponent played exactly the move we were pondering.
          // Stop the ponder search, then issue a timed go on the same position.
          console.log(`[Ponder ${id}] HIT — user played predicted ${userMove}`);
          this.stopPonderAndGo(session, data.requestId, ps.baseFen, ps.baseMoves.concat(userMove), wtime, btime, winc, binc, depth, movetimeMs, targetMultiPv);
        } else if (incomingPly > ps.ponderPly) {
          // ── Ponder MISS ────────────────────────────────────────────────
          // Opponent played a different move — stop and resync.
          console.log(`[Ponder ${id}] MISS — expected ${ps.ponderMove}, got ${userMove}`);
          this.stopPonderAndGo(session, data.requestId, startFen, moves, wtime, btime, winc, binc, depth, movetimeMs, targetMultiPv);
        } else {
          // ── Ponder RESYNC ──────────────────────────────────────────────
          // Same or fewer moves than when pondering started.
          // Do not treat this as a HIT.
          // Stop/resync safely.
          console.log(`[Ponder ${id}] RESYNC — incomingPly=${incomingPly} <= ponderPly=${ps.ponderPly}`);
          this.stopPonderAndGo(session, data.requestId, startFen, moves, wtime, btime, winc, binc, depth, movetimeMs, targetMultiPv);
        }
        return;
      }

      // Not pondering — issue position + go directly.
      this.enqueueSearch(session, {
        type: 'search',
        requestId: data.requestId,
        searchMode: 'tc',
        startFen,
        moves,
        wtime,
        btime,
        winc,
        binc,
        depth,
        movetimeMs,
        multiPv: targetMultiPv,
      });
      return;
    }

    // ── position (Analysis Mode FEN load, no search) ───────────────────────
    if (data.type === 'position') {
      const moves = data.moves;
      const startFen = data.fen;
      this.enqueueControlOperation(session, { type: 'position', startFen, moves });
      return;
    }

    // ── analyze (Analysis Mode deep search) ───────────────────────────────
    if (data.type === 'analyze') {
      const moves = data.moves;
      const startFen = data.fen;
      const profile = getDifficultyProfile('standard', data.purpose, {
        depth: data.depth,
        multiPv: data.multiPv,
      });
      this.enqueueSearch(session, {
        type: 'search',
        requestId: data.requestId,
        searchMode: 'analysis',
        startFen,
        moves,
        wtime: 0,
        btime: 0,
        winc: 0,
        binc: 0,
        depth: profile.depth,
        movetimeMs: profile.movetimeMs,
        multiPv: profile.multiPv,
      });
      return;
    }

    // ── newgame ────────────────────────────────────────────────────────────
    if (data.type === 'newgame') {
      this.enqueueControlOperation(session, { type: 'newgame' });
      return;
    }

    // ── stop ──────────────────────────────────────────────────────────────
    if (data.type === 'stop') {
      this.invalidateActiveSearch(session, data.requestId);
      return;
    }

    // ── releaseSession ─────────────────────────────────────────────────────
    if (data.type === 'releaseSession') {
      this.invalidateActiveSearch(session);
      writeUciCommand(session.process.stdin, 'stop');
      this.terminateSession(id, 'released');
      return;
    }

    // ── setoption ─────────────────────────────────────────────────────────
    if (data.type === 'setoption') {
      this.enqueueControlOperation(session, {
        type: 'setoption',
        name: data.name,
        value: data.value,
      });
      return;
    }
  }

  // ── Ponder helpers ─────────────────────────────────────────────────────────

  private enqueueSearch(session: EngineSession, operation: Extract<PendingOperation, { type: 'search' }>) {
    if (!session.process.stdin) return;

    if (session.activeSearch || session.ponderState.phase === 'pondering') {
      session.pendingOperation = operation;
      this.invalidateActiveSearch(session);
      return;
    }

    this.startSearch(session, operation);
  }

  private enqueueControlOperation(session: EngineSession, operation: Exclude<PendingOperation, { type: 'search' }>) {
    if (!session.process.stdin) return;

    if (session.activeSearch || session.ponderState.phase === 'pondering') {
      session.pendingOperation = operation;
      this.invalidateActiveSearch(session);
      return;
    }

    this.runControlOperation(session, operation);
  }

  private invalidateActiveSearch(session: EngineSession, requestId?: number) {
    if (!session.process.stdin) return;

    const ps = session.ponderState;
    if (session.activeSearch) {
      if (requestId !== undefined && session.activeSearch.requestId !== requestId) {
        return;
      }
      if (session.activeSearch.state !== 'stopping') {
        session.activeSearch = { ...session.activeSearch, state: 'stopping' };
        writeUciCommand(session.process.stdin, 'stop');
      }
      return;
    }

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
        pendingRequestId: undefined,
      };
      writeUciCommand(session.process.stdin, 'stop');
    }
  }

  private runPendingOperation(session: EngineSession) {
    const operation = session.pendingOperation;
    if (!operation) return;

    session.pendingOperation = null;
    if (operation.type === 'search') {
      this.startSearch(session, operation);
    } else {
      this.runControlOperation(session, operation);
    }
  }

  private runControlOperation(session: EngineSession, operation: Exclude<PendingOperation, { type: 'search' }>) {
    if (!session.process.stdin) return;

    session.activeSearch = null;
    session.currentDepth = 0;
    session.pvs = [];
    session.ponderState = { phase: 'idle' };

    if (operation.type === 'newgame') {
      writeUciCommand(session.process.stdin, buildSetOptionCommand('MultiPV', ANALYSIS_MULTIPV));
      writeUciCommand(session.process.stdin, 'ucinewgame');
      writeUciCommand(session.process.stdin, 'isready');
      session.startFen = DEFAULT_START_FEN;
      session.uciMoves = [];
      return;
    }

    if (operation.type === 'setoption') {
      writeUciCommand(session.process.stdin, buildSetOptionCommand(operation.name, operation.value));
      return;
    }

    writeUciCommand(session.process.stdin, buildPositionCommand(operation.startFen, operation.moves));
    session.startFen = operation.startFen;
    session.uciMoves = operation.moves;
  }

  private startSearch(session: EngineSession, operation: Extract<PendingOperation, { type: 'search' }>) {
    if (!session.process.stdin) return;

    writeUciCommand(session.process.stdin, buildSetOptionCommand('MultiPV', operation.multiPv));
    this.sendPositionAndGo(
      session,
      operation.requestId,
      operation.searchMode,
      operation.startFen,
      operation.moves,
      operation.wtime,
      operation.btime,
      operation.winc,
      operation.binc,
      operation.depth,
      operation.movetimeMs,
    );
  }

  /**
   * Stop the ponder search, buffer the pending user move + TC, and wait for
   * the dummy bestmove stop-ack before issuing the real go command.
   */
  private stopPonderAndGo(
    session: EngineSession,
    requestId: number,
    startFen: string,
    moves: string[],
    wtime: number,
    btime: number,
    winc: number,
    binc: number,
    depth?: number,
    movetimeMs?: number,
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
        pendingMovetimeMs: movetimeMs,
        pendingMultiPv: targetMultiPv,
        pendingRequestId: requestId,
        baseFen:   startFen,
        baseMoves: moves,
      };
      writeUciCommand(session.process.stdin, 'stop');
    } else {
      // Not pondering — issue immediately
      this.enqueueSearch(session, {
        type: 'search',
        requestId,
        searchMode: 'tc',
        startFen,
        moves,
        wtime,
        btime,
        winc,
        binc,
        depth,
        movetimeMs,
        multiPv: targetMultiPv,
      });
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
        pendingMovetimeMs: undefined,
        pendingMultiPv: 1,
        pendingRequestId: undefined,
      };
      writeUciCommand(session.process.stdin, 'stop');
    } else if (ps.phase === 'thinking') {
      this.invalidateActiveSearch(session);
    }
  }

  /**
   * Send a position + timed go command. Updates ponderState to 'thinking'.
   */
  private sendPositionAndGo(
    session: EngineSession,
    requestId: number,
    searchMode: 'tc' | 'analysis',
    startFen: string,
    moves: string[],
    wtime: number,
    btime: number,
    winc: number,
    binc: number,
    depth?: number,
    movetimeMs?: number,
  ) {
    if (!session.process.stdin) return;

    writeUciCommand(session.process.stdin, buildPositionCommand(startFen, moves));
    writeUciCommand(session.process.stdin, buildGoCommand({ depth, movetimeMs, wtime, btime, winc, binc }));
    session.searchGeneration += 1;
    session.activeSearch = {
      requestId,
      generation: session.searchGeneration,
      state: 'running',
      searchMode,
    };
    session.currentDepth = 0;
    session.pvs = [];
    session.ponderState = { phase: 'thinking', requestId, searchMode, wtime, btime, winc, binc, depth, movetimeMs };
    session.startFen = startFen;
    session.uciMoves = moves;
  }

  // ── Session lifecycle ──────────────────────────────────────────────────────

  private updateActivity(id: string) {
    const session = this.active.get(id);
    if (session) {
      session.lastActivityAt = Date.now();
      clearTimeout(session.idleTimer);
      session.idleTimer = setTimeout(() => this.terminateSession(id, 'idle'), this.idleTimeoutMs);
      clearTimeout(session.sessionTimer);
      session.sessionTimer = setTimeout(() => this.terminateSession(id, 'session_expired'), this.sessionMaxMs);
    }
  }

  private isSocketOpen(ws: WebSocket) {
    return ws.readyState === WebSocket.OPEN;
  }

  private sendIfOpen(ws: WebSocket, payload: string) {
    if (!this.isSocketOpen(ws)) {
      return false;
    }

    try {
      ws.send(payload);
      return true;
    } catch {
      return false;
    }
  }

  private terminateSession(id: string, reason: string) {
    const session = this.active.get(id);
    if (!session) {
      this.removeQueuedClient(id);
      return;
    }

    console.log(`Terminating session ${id} (Reason: ${reason})`);

    clearTimeout(session.idleTimer);
    clearTimeout(session.sessionTimer);

    this.active.delete(id);

    if (reason === 'session_expired' || reason === 'idle' || reason === 'released') {
      if (session.ws.readyState === WebSocket.OPEN) {
        if (reason === 'released') {
          this.sendIfOpen(session.ws, JSON.stringify({ type: 'released' }));
        } else {
          this.sendIfOpen(session.ws, JSON.stringify({ type: 'session_expired', reason }));
        }
        session.ws.close();
      }
    }

    if (session.process.stdin) {
      writeUciCommand(session.process.stdin, 'quit');
    }

    session.process.kill('SIGTERM');
    setTimeout(() => {
      if (session.process.exitCode === null && session.process.signalCode === null) {
        session.process.kill('SIGKILL');
      }
    }, 3000);

    this.dequeueNext();
  }

  private dequeueNext() {
    let queueChanged = false;

    while (this.waitQueue.length > 0 && this.active.size < this.maxConcurrent) {
      const next = this.waitQueue.shift();
      if (next) {
        queueChanged = true;
        next.removeCloseListener();

        if (!this.isSocketOpen(next.ws)) {
          continue;
        }

        this.spawnEngine(next.ws, next.id);

        if (this.active.has(next.id)) {
          break;
        }
      }
    }

    if (queueChanged) {
      this.updateQueuePositions();
    }
  }
}

export const enginePool = new EnginePoolManager();
