import { spawn, ChildProcess, SpawnOptions } from 'child_process';
import { RawData, WebSocket } from 'ws';
import { Chess } from 'chess.js';
import path from 'path';
import fs from 'fs';
import { performance } from 'perf_hooks';
import { parseUciInfo, parseBestMove } from './uci-parser';
import { classifyTerminalPosition } from './engine-terminal';
import { createPositionIdentity, resolveExactPosition, validateEngineMove } from './fair-play-engine';
import type { PositionIdentity } from './fair-play-engine';
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
import type { EngineOptionName, SearchLimit } from './engine-protocol';
import { getDifficultyProfile } from './engine-difficulty';
import type { OpeningSelectionConfiguration, SearchPurpose } from './engine-difficulty';

interface SessionPv {
  multipv: number;
  score?: number;
  mate?: number;
  pv?: string[];
}

// ── Ponder state machine ──────────────────────────────────────────────────────

type PonderSearchContext = SearchLimit;

type PonderStopReason =
  | 'miss'
  | 'reset'
  | 'newgame'
  | 'mode-switch'
  | 'option-change'
  | 'disconnect'
  | 'release';

type PonderState =
  | { status: 'idle' }
  | {
      status: 'pondering';
      parentRequestId: number;
      expectedPlayerMove: string;
      expectedMoves: string[];
      expectedPositionFen: string;
      searchContext: PonderSearchContext;
    }
  | {
      status: 'hit';
      nextRequestId: number;
      expectedPlayerMove: string;
      expectedMoves: string[];
      expectedPositionFen: string;
      searchContext: PonderSearchContext;
    }
  | {
      status: 'stopping';
      reason: PonderStopReason;
      pendingFreshSearch?: Extract<PendingOperation, { type: 'search' }>;
    };

type ActiveSearchState = 'running' | 'stopping';

type WatchdogPhase = 'search' | 'stop-ack';

interface ActiveSearch {
  requestId: number;
  generation: number;
  state: ActiveSearchState;
  purpose: SearchPurpose;
  searchMode: 'tc' | 'analysis';
  ponderAllowed: boolean;
  ponderContext: PonderSearchContext | null;
  searchLimit: SearchLimit;
  generatedGoCommand?: string;
  startedAt: number;
  deadlineAt?: number;
  position: PositionIdentity;
  positionCommand: string;
  operation: Extract<PendingOperation, { type: 'search' }>;
}

interface AwaitingMoveApplication {
  requestId: number;
  position: PositionIdentity;
  move: string | null;
  expectedNewFen: string;
  receivedAt: number;
  timer: NodeJS.Timeout;
}

interface ActiveSearchWatchdog {
  requestId?: number;
  timer: NodeJS.Timeout;
  startedAt: number;
  timeoutMs: number;
  phase: WatchdogPhase;
}

interface PendingBookResult {
  move: string;
  candidateCount: number;
  selectedWeight?: number;
}

type PendingOperation =
  | {
      type: 'search';
      requestId: number;
      searchMode: 'tc' | 'analysis';
      purpose: SearchPurpose;
      startFen: string;
      moves: string[];
      searchLimit: SearchLimit;
      multiPv: number;
      openingSelection?: OpeningSelectionConfiguration;
      ponderAllowed: boolean;
      ponderContext: PonderSearchContext | null;
      retryCount?: number;
    }
  | { type: 'newgame' }
  | { type: 'position'; startFen: string; moves: string[] }
  | { type: 'setoption'; name: EngineOptionName; value: number | boolean | string };

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
  ponderState: PonderState;
  activeSearch: ActiveSearch | null;
  pendingOperation: PendingOperation | null;
  requestedOwnBook: boolean;
  pendingBookResult: PendingBookResult | null;
  searchGeneration: number;
  sessionGeneration: number;
  positionSequence: number;
  awaitingMoveApplication: AwaitingMoveApplication | null;
  lastAppliedOpponentFen: string | null;
  lineBuffer: string;
  watchdog: ActiveSearchWatchdog | null;
  suppressExitError: boolean;
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
  searchWatchdogToleranceMs?: number;
  stopAckTimeoutMs?: number;
  depthSearchWatchdogMs?: number;
  spawnEngineProcess?: (enginePath: string, args: string[], options?: SpawnOptions) => ChildProcess;
}

const DEFAULT_MAX_CONCURRENT = 10;
const IDLE_TIMEOUT_MS = 2 * 60_000;
const SESSION_MAX_MS = 2 * 60 * 60_000;
const HASH_SIZE_MB = 32;
const SEARCH_WATCHDOG_TOLERANCE_MS = 5_000;
const STOP_ACK_TIMEOUT_MS = 2_000;
const DEPTH_SEARCH_WATCHDOG_MS = 60_000;

export class EnginePoolManager {
  private active = new Map<string, EngineSession>();
  private waitQueue: QueuedClient[] = [];
  private maxConcurrent: number;
  private enginePathOverride?: string;
  private idleTimeoutMs: number;
  private sessionMaxMs: number;
  private searchWatchdogToleranceMs: number;
  private stopAckTimeoutMs: number;
  private depthSearchWatchdogMs: number;
  private spawnEngineProcess: (enginePath: string, args: string[], options?: SpawnOptions) => ChildProcess;
  private sessionGenerations = new Map<string, number>();

  public constructor(options: EnginePoolOptions = {}) {
    this.maxConcurrent = options.maxConcurrent ?? DEFAULT_MAX_CONCURRENT;
    this.enginePathOverride = options.enginePath;
    this.idleTimeoutMs = options.idleTimeoutMs ?? IDLE_TIMEOUT_MS;
    this.sessionMaxMs = options.sessionMaxMs ?? SESSION_MAX_MS;
    this.searchWatchdogToleranceMs = options.searchWatchdogToleranceMs ?? SEARCH_WATCHDOG_TOLERANCE_MS;
    this.stopAckTimeoutMs = options.stopAckTimeoutMs ?? STOP_ACK_TIMEOUT_MS;
    this.depthSearchWatchdogMs = options.depthSearchWatchdogMs ?? DEPTH_SEARCH_WATCHDOG_MS;
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

  private spawnEngine(
    ws: WebSocket,
    id: string,
    attachSocketHandlers = true,
    recoverySearch?: Extract<PendingOperation, { type: 'search' }>,
    requestedOwnBook = true,
    lastAppliedOpponentFen: string | null = null,
  ) {
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

    const sessionGeneration = (this.sessionGenerations.get(id) ?? 0) + 1;
    this.sessionGenerations.set(id, sessionGeneration);
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
      ponderState: { status: 'idle' },
      activeSearch: null,
      pendingOperation: null,
      requestedOwnBook,
      pendingBookResult: null,
      searchGeneration: 0,
      sessionGeneration,
      positionSequence: 0,
      awaitingMoveApplication: null,
      lastAppliedOpponentFen,
      lineBuffer: '',
      watchdog: null,
      suppressExitError: false,
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
          if (this.active.get(id) !== session) continue;
          this.updateActivity(id);

          if (!isReady) {
            // ── Initialisation handshake ─────────────────────────────────
            if (trimmed === 'uciok') {
              writeUciCommand(engineProcess.stdin, buildSetOptionCommand('Hash', HASH_SIZE_MB));
              writeUciCommand(engineProcess.stdin, buildSetOptionCommand('OwnBook', requestedOwnBook));
              writeUciCommand(engineProcess.stdin, buildSetOptionCommand('BookDepth', 30));
              writeUciCommand(engineProcess.stdin, buildSetOptionCommand('MultiPV', ANALYSIS_MULTIPV));
              writeUciCommand(engineProcess.stdin, buildSetOptionCommand('BookSelection', 'weighted'));
              writeUciCommand(engineProcess.stdin, 'isready');
            } else if (trimmed === 'readyok') {
              isReady = true;
              if (recoverySearch) {
                this.startSearch(session, recoverySearch);
              } else {
                this.sendIfOpen(ws, JSON.stringify({ type: 'ready', sessionId: id, sessionGeneration }));
              }
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
        if (this.active.get(id) !== session) return;
        console.error(`Engine error (Session ${id}):`, err);
        this.sendIfOpen(ws, JSON.stringify({ type: 'error', message: 'Engine process error' }));
        this.terminateSession(id, 'error');
      });

      engineProcess.on('exit', (code) => {
        console.log(`Engine exited (Session ${id}) with code ${code}`);
        if (this.active.get(id) === session && !session.suppressExitError) {
          this.sendIfOpen(ws, JSON.stringify({ type: 'error', message: 'Engine crashed unexpectedly' }));
          this.terminateSession(id, 'error');
        }
      });
    } else {
      this.sendIfOpen(ws, JSON.stringify({ type: 'error', message: 'Failed to start engine' }));
      this.terminateSession(id, 'error');
    }

    if (attachSocketHandlers) {
      ws.on('message', (message) => {
        this.handleRawClientMessage(id, ws, message);
      });

      ws.on('close', () => {
        this.terminateSession(id, 'disconnected');
      });
    }
  }

  // ── Engine stdout handlers ──────────────────────────────────────────────────

  private handleInfoLine(id: string, trimmed: string) {
    const session = this.active.get(id);
    if (!session) return;

    // Ponder info is backend-internal until a verified ponderhit adopts it.
    const ps = session.ponderState;
    if (ps.status === 'pondering' || ps.status === 'stopping') return;
    if (!session.activeSearch || session.activeSearch.state !== 'running') return;

    const bookResult = parseBookInfo(trimmed);
    if (bookResult) {
      session.pendingBookResult = bookResult;
      return;
    }

    const parsedInfo = parseUciInfo(trimmed);
    if (parsedInfo && parsedInfo.depth) {
      if (session.activeSearch.searchLimit.mode === 'depth' && parsedInfo.depth > session.activeSearch.searchLimit.depth) {
        if (process.env.DEBUG === 'true' || process.env.NODE_ENV === 'development') {
          console.warn('[engine-search-diagnostic]', {
            requestId: session.activeSearch.requestId,
            purpose: session.activeSearch.purpose,
            fen: session.startFen,
            requestedLimit: session.activeSearch.searchLimit,
            generatedGoCommand: session.activeSearch.generatedGoCommand,
            reportedDepth: parsedInfo.depth,
            selectiveDepth: parsedInfo.selectiveDepth ?? null,
            snapshotType: 'live',
            ponderState: session.ponderState.status,
            accepted: false,
            rejectionReason: 'nominal-depth-exceeds-depth-limit',
          });
        }
        return;
      }
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
        selectiveDepth: parsedInfo.selectiveDepth ?? null,
        requestedLimit: session.activeSearch.searchLimit,
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

    if (ps.status === 'stopping') {
      const pendingFreshSearch = ps.pendingFreshSearch;
      this.clearWatchdog(session, 'stop-ack');
      session.ponderState = { status: 'idle' };
      if (pendingFreshSearch) {
        console.log(`[Ponder ${id}] Stop-ack received — issuing fresh search`);
        this.startSearch(session, pendingFreshSearch);
      } else {
        this.runPendingOperation(session);
      }
      return;
    }

    if (!activeSearch) {
      if (ps.status !== 'pondering') {
        session.ponderState = { status: 'idle' };
      }
      this.runPendingOperation(session);
      return;
    }

    if (activeSearch.state === 'stopping') {
      this.clearWatchdog(session, 'stop-ack');
      session.activeSearch = null;
      session.ponderState = { status: 'idle' };
      this.runPendingOperation(session);
      return;
    }

    // ── Normal thinking result ────────────────────────────────────────────
    if (activeSearch.state === 'running') {
      const receivedAt = Date.now();
      this.clearWatchdog(session, 'search');
      const { bestMove, ponderMove } = parsed;
      const validation = validateEngineMove(activeSearch.position.normalizedFen, bestMove);
      if (validation.ok === false) {
        this.rejectEngineResult(session, activeSearch, bestMove, validation.reason);
        return;
      }
      const terminal = bestMove === '0000'
        ? classifyTerminalPosition(activeSearch.position.normalizedFen, [])
        : undefined;
      const move = validation.move;
      const ponderPlan = move && activeSearch.ponderAllowed && activeSearch.ponderContext
        ? buildValidatedPonderPlan(activeSearch.position.normalizedFen, [], move, ponderMove, activeSearch.ponderContext)
        : null;
      const bookResult = session.pendingBookResult;

      session.activeSearch = null;
      session.pendingBookResult = null;

      // A terminal `bestmove 0000` has no board move to apply and therefore
      // must never enter the application-acknowledgment state.
      if (activeSearch.purpose === 'opponent' && move !== null) {
        const timer = setTimeout(() => {
          const current = this.active.get(id);
          if (current !== session || current.awaitingMoveApplication?.requestId !== activeSearch.requestId) return;
          current.awaitingMoveApplication = null;
          this.sendIfOpen(current.ws, JSON.stringify({
            type: 'error',
            code: 'MOVE_APPLICATION_TIMEOUT',
            requestId: activeSearch.requestId,
            failureReason: 'application_timeout',
            clockStopped: true,
            message: 'Engine move was not acknowledged by the game state.',
          }));
          this.recoverEngineSession(current);
        }, this.stopAckTimeoutMs);
        session.awaitingMoveApplication = {
          requestId: activeSearch.requestId,
          position: activeSearch.position,
          move,
          expectedNewFen: validation.newFen,
          receivedAt,
          timer,
        };
      }

      // Send best move to the frontend
      session.ws.send(JSON.stringify({
        type: 'bestmove',
        requestId: activeSearch.requestId,
        sessionId: id,
        sessionGeneration: activeSearch.position.sessionGeneration,
        positionSequence: activeSearch.position.positionSequence,
        positionKey: activeSearch.position.key,
        positionFen: activeSearch.position.normalizedFen,
        newFen: validation.newFen,
        expectedSide: activeSearch.position.expectedSide,
        move,
        receivedAt,
        resultValidated: true,
        moveApplied: false,
        clockStopped: true,
        ...(activeSearch.deadlineAt !== undefined ? { engineDeadlineAt: activeSearch.deadlineAt } : {}),
        ...(bookResult && bookResult.move === bestMove
          ? {
              source: 'book',
              book: {
                candidateCount: bookResult.candidateCount,
                ...(bookResult.selectedWeight !== undefined
                  ? { selectedWeight: bookResult.selectedWeight }
                  : {}),
              },
            }
          : { source: 'search' }),
        ...(ponderPlan ? { ponder: ponderPlan.expectedPlayerMove } : {}),
        ...(terminal ? { terminal } : {}),
      }));
      console.log(`[Engine ${id}] bestmove=${bestMove} ponder=${ponderPlan?.expectedPlayerMove ?? 'none'}`);

      if (ponderPlan && session.process.stdin) {
        const expectedPositionFen = resolveExactPosition(session.startFen, ponderPlan.expectedMoves)?.fen();
        if (!expectedPositionFen) {
          session.ponderState = { status: 'idle' };
          return;
        }
        console.log(`[Ponder ${id}] Starting ponder on move: ${ponderPlan.expectedPlayerMove}`);
        writeUciCommand(session.process.stdin, buildPositionCommand(session.startFen, ponderPlan.expectedMoves));
        writeUciCommand(session.process.stdin, buildGoCommand(ponderPlan.searchContext, { ponder: true }));
        session.ponderState = {
          status: 'pondering',
          parentRequestId: activeSearch.requestId,
          expectedPlayerMove: ponderPlan.expectedPlayerMove,
          expectedMoves: ponderPlan.expectedMoves,
          expectedPositionFen,
          searchContext: ponderPlan.searchContext,
        };
      } else {
        session.ponderState = { status: 'idle' };
      }
      if (activeSearch.purpose !== 'opponent') this.runPendingOperation(session);
      return;
    }

    // ── Idle / unexpected bestmove — stale search output, swallow it. ─────
    session.ponderState = { status: 'idle' };
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
      if (session.awaitingMoveApplication) {
        this.sendIfOpen(session.ws, JSON.stringify({
          type: 'error', code: 'RESULT_NOT_ACKNOWLEDGED', requestId: data.requestId,
          failureReason: 'duplicate_position_request', clockStopped: true,
          message: 'The previous engine result must be applied or rejected before another search.',
        }));
        return;
      }
      const moves = data.moves;
      const opponentProfile = getDifficultyProfile(data.difficulty, 'opponent');
      const searchLimit = data.searchLimit ?? searchLimitFromProfile(opponentProfile);
      const targetMultiPv = data.searchLimit ? data.multiPv : opponentProfile.multiPv;
      const openingSelection = opponentProfile.openingSelection;
      const ponderContext = getPonderSearchContext(searchLimit);
      const ponderAllowed = data.ponder === true && ponderContext !== null;

      const startFen = data.fen;

      const ps = session.ponderState;

      if (ps.status === 'pondering') {
        // The user has made a move while the engine is pondering.
        // Correlate by the complete requested position. This works both for
        // authoritative current-FEN requests and base-FEN-plus-moves clients.
        const requestedPosition = resolveExactPosition(startFen, moves);
        if (requestedPosition?.fen() === ps.expectedPositionFen) {
          console.log(`[Ponder ${id}] HIT — request matched predicted position after ${ps.expectedPlayerMove}`);
          const ponderPosition = requestedPosition;
          if (!ponderPosition) {
            this.stopPonderAndGo(session, 'miss', {
              type: 'search', requestId: data.requestId, searchMode: 'tc', purpose: 'opponent',
              startFen, moves, searchLimit, multiPv: targetMultiPv, openingSelection,
              ponderAllowed, ponderContext,
            });
            return;
          }
          session.searchGeneration += 1;
          session.positionSequence += 1;
          const operation: Extract<PendingOperation, { type: 'search' }> = {
            type: 'search', requestId: data.requestId, searchMode: 'tc', purpose: 'opponent',
            startFen: ponderPosition.fen(), moves: [], searchLimit: ps.searchContext,
            multiPv: targetMultiPv, openingSelection, ponderAllowed, ponderContext,
          };
          const position = createPositionIdentity({
            requestId: data.requestId, sessionId: session.id,
            sessionGeneration: session.sessionGeneration, positionSequence: session.positionSequence,
            fen: ponderPosition.fen(),
          });
          const adoptedSearch: ActiveSearch = {
            requestId: data.requestId,
            generation: session.searchGeneration,
            state: 'running',
            purpose: 'opponent',
            searchMode: 'tc',
            ponderAllowed,
            ponderContext,
            searchLimit: ps.searchContext,
            generatedGoCommand: buildGoCommand(ps.searchContext, { ponder: true }),
            startedAt: Date.now(),
            deadlineAt: deadlineAtForSearch(ponderPosition.fen(), [], ps.searchContext),
            position,
            positionCommand: buildPositionCommand(ponderPosition.fen(), []),
            operation,
          };
          session.activeSearch = adoptedSearch;
          session.currentDepth = 0;
          session.pvs = [];
          session.pendingBookResult = null;
          session.ponderState = {
            status: 'hit',
            nextRequestId: data.requestId,
            expectedPlayerMove: ps.expectedPlayerMove,
            expectedMoves: ps.expectedMoves,
            expectedPositionFen: ps.expectedPositionFen,
            searchContext: ps.searchContext,
          };
          writeUciCommand(session.process.stdin, 'ponderhit');
          this.sendIfOpen(session.ws, JSON.stringify({
            type: 'search-started',
            requestId: data.requestId,
            sessionId: session.id, sessionGeneration: session.sessionGeneration,
            positionSequence: position.positionSequence, positionKey: position.key,
            positionFen: position.normalizedFen, expectedSide: position.expectedSide,
            startedAt: adoptedSearch.startedAt,
            ...(adoptedSearch.deadlineAt !== undefined ? { engineDeadlineAt: adoptedSearch.deadlineAt } : {}),
          }));
          this.armSearchWatchdog(session, data.requestId, ps.searchContext, []);
        } else {
          console.log(`[Ponder ${id}] MISS — request did not match predicted ${ps.expectedPlayerMove} position`);
          this.stopPonderAndGo(session, 'miss', {
            type: 'search',
            requestId: data.requestId,
            searchMode: 'tc',
            purpose: 'opponent',
            startFen,
            moves: [...moves],
            searchLimit,
            multiPv: targetMultiPv,
            openingSelection,
            ponderAllowed,
            ponderContext,
          });
        }
        return;
      }

      // Not pondering — issue position + go directly.
      this.enqueueSearch(session, {
        type: 'search',
        requestId: data.requestId,
        searchMode: 'tc',
        purpose: 'opponent',
        startFen,
        moves,
        searchLimit,
        multiPv: targetMultiPv,
        openingSelection,
        ponderAllowed,
        ponderContext,
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
      const searchLimit = data.searchLimit ?? searchLimitFromProfile(profile);
      this.enqueueSearch(session, {
        type: 'search',
        requestId: data.requestId,
        searchMode: 'analysis',
        purpose: data.purpose,
        startFen,
        moves,
        searchLimit,
        multiPv: profile.multiPv,
        ponderAllowed: false,
        ponderContext: null,
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

    if (data.type === 'resultAck') {
      this.acknowledgeMoveApplication(session, data);
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

  private rejectEngineResult(
    session: EngineSession,
    activeSearch: ActiveSearch,
    bestMove: string,
    failureReason: string,
  ) {
    this.clearWatchdog(session, 'search');
    session.activeSearch = null;
    session.pendingBookResult = null;
    const retryCount = activeSearch.operation.retryCount ?? 0;
    const retryAllowed = activeSearch.purpose === 'opponent' && retryCount < 1;
    this.sendIfOpen(session.ws, JSON.stringify({
      type: retryAllowed ? 'search-retrying' : 'error',
      code: 'ENGINE_RESULT_REJECTED',
      requestId: activeSearch.requestId,
      sessionId: session.id,
      sessionGeneration: session.sessionGeneration,
      positionSequence: activeSearch.position.positionSequence,
      positionKey: activeSearch.position.key,
      positionFen: activeSearch.position.normalizedFen,
      expectedSide: activeSearch.position.expectedSide,
      bestmove: bestMove,
      moveLegal: false,
      resultValidated: false,
      moveApplied: false,
      failureReason,
      retryCount: retryCount + 1,
      retryAllowed,
      clockStopped: true,
      message: retryAllowed
        ? 'Engine returned an invalid move; resetting once before retry.'
        : 'Engine turn failed after an invalid result.',
    }));
    this.recoverEngineSession(
      session,
      retryAllowed ? { ...activeSearch.operation, retryCount: retryCount + 1 } : undefined,
    );
  }

  private acknowledgeMoveApplication(
    session: EngineSession,
    data: Extract<ClientMessage, { type: 'resultAck' }>,
  ) {
    const awaiting = session.awaitingMoveApplication;
    if (!awaiting || awaiting.requestId !== data.requestId || awaiting.position.key !== data.positionKey) {
      this.sendIfOpen(session.ws, JSON.stringify({
        type: 'error', code: 'STALE_APPLICATION_ACK', requestId: data.requestId,
        failureReason: 'stale_request', clockStopped: true,
        message: 'Move application acknowledgement did not match the active result.',
      }));
      return;
    }
    clearTimeout(awaiting.timer);
    session.awaitingMoveApplication = null;
    const oldPosition = resolveExactPosition(data.oldFen, []);
    const newPosition = data.newFen ? resolveExactPosition(data.newFen, []) : null;
    const oldMatches = oldPosition?.fen() === awaiting.position.normalizedFen;
    const newMatches = data.applied && newPosition?.fen() === awaiting.expectedNewFen;
    if (!data.applied || !oldMatches || !newMatches) {
      this.sendIfOpen(session.ws, JSON.stringify({
        type: 'error', code: 'MOVE_APPLICATION_FAILED', requestId: data.requestId,
        failureReason: !oldMatches ? 'position_mismatch' : data.failureReason ?? 'move_application_failed',
        resultValidated: true, moveApplied: false, clockStopped: true,
        message: 'Validated engine move was not applied to the expected position.',
      }));
      this.recoverEngineSession(session);
      return;
    }
    this.sendIfOpen(session.ws, JSON.stringify({
      type: 'move-applied', requestId: data.requestId, positionKey: data.positionKey,
      resultValidated: true, moveApplied: true, oldFen: oldPosition.fen(), newFen: newPosition.fen(),
    }));
    session.lastAppliedOpponentFen = awaiting.position.normalizedFen;
    session.startFen = newPosition.fen();
    session.uciMoves = [];
    this.runPendingOperation(session);
  }

  // ── Ponder helpers ─────────────────────────────────────────────────────────

  private enqueueSearch(session: EngineSession, operation: Extract<PendingOperation, { type: 'search' }>) {
    if (!session.process.stdin) return;

    if (session.awaitingMoveApplication) {
      this.sendIfOpen(session.ws, JSON.stringify({
        type: 'error', code: 'RESULT_NOT_ACKNOWLEDGED', requestId: operation.requestId,
        failureReason: 'duplicate_position_request', clockStopped: true,
        message: 'The previous engine result must be applied or rejected before another search.',
      }));
      return;
    }

    if (session.activeSearch || session.ponderState.status === 'pondering' || session.ponderState.status === 'hit') {
      session.pendingOperation = operation;
      this.invalidateActiveSearch(session);
      return;
    }

    this.startSearch(session, operation);
  }

  private enqueueControlOperation(session: EngineSession, operation: Exclude<PendingOperation, { type: 'search' }>) {
    if (!session.process.stdin) return;

    if (session.activeSearch || session.ponderState.status === 'pondering' || session.ponderState.status === 'hit') {
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
        session.pendingBookResult = null;
        writeUciCommand(session.process.stdin, 'stop');
        this.armStopAckWatchdog(session, session.activeSearch.requestId);
      }
      return;
    }

    if (ps.status === 'pondering') {
      session.ponderState = { status: 'stopping', reason: 'reset' };
      writeUciCommand(session.process.stdin, 'stop');
      this.armStopAckWatchdog(session, ps.parentRequestId);
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
    session.pendingBookResult = null;
    session.ponderState = { status: 'idle' };

    if (operation.type === 'newgame') {
      writeUciCommand(session.process.stdin, buildSetOptionCommand('MultiPV', ANALYSIS_MULTIPV));
      writeUciCommand(session.process.stdin, 'ucinewgame');
      writeUciCommand(session.process.stdin, 'isready');
      session.startFen = DEFAULT_START_FEN;
      session.uciMoves = [];
      session.lastAppliedOpponentFen = null;
      return;
    }

    if (operation.type === 'setoption') {
      if (operation.name === 'OwnBook' && typeof operation.value === 'boolean') {
        session.requestedOwnBook = operation.value;
      }
      writeUciCommand(session.process.stdin, buildSetOptionCommand(operation.name, operation.value));
      return;
    }

    writeUciCommand(session.process.stdin, buildPositionCommand(operation.startFen, operation.moves));
    session.startFen = operation.startFen;
    session.uciMoves = operation.moves;
  }

  private startSearch(session: EngineSession, operation: Extract<PendingOperation, { type: 'search' }>) {
    if (!session.process.stdin) return;

    const resolved = resolveExactPosition(operation.startFen, operation.moves);
    if (!resolved) {
      this.sendIfOpen(session.ws, JSON.stringify({
        type: 'error', code: 'POSITION_MISMATCH', requestId: operation.requestId,
        failureReason: 'position_mismatch', clockStopped: true,
        message: 'FEN and move continuation do not describe one legal position.',
      }));
      return;
    }
    const exactOperation = { ...operation, startFen: resolved.fen(), moves: [] };
    if (exactOperation.purpose === 'opponent' && session.lastAppliedOpponentFen === exactOperation.startFen) {
      this.sendIfOpen(session.ws, JSON.stringify({
        type: 'error', code: 'DUPLICATE_POSITION_REQUEST', requestId: operation.requestId,
        positionFen: exactOperation.startFen, failureReason: 'duplicate_position_request',
        retryCount: 0, clockStopped: true,
        message: 'This exact opponent position already produced an applied move.',
      }));
      return;
    }
    if (resolved.isGameOver()) {
      this.sendIfOpen(session.ws, JSON.stringify({
        type: 'bestmove', requestId: operation.requestId, move: null,
        positionFen: resolved.fen(), expectedSide: resolved.turn(),
        resultValidated: true, moveApplied: false, clockStopped: true,
        terminal: classifyTerminalPosition(resolved.fen(), []),
      }));
      return;
    }

    writeUciCommand(session.process.stdin, buildSetOptionCommand('MultiPV', exactOperation.multiPv));
    this.applyBookOptions(session, exactOperation.purpose, exactOperation.openingSelection);
    this.sendPositionAndGo(
      session,
      exactOperation,
    );
  }

  private applyBookOptions(session: EngineSession, purpose: SearchPurpose, openingSelection?: OpeningSelectionConfiguration) {
    if (!session.process.stdin) return;

    const useBook = purpose === 'opponent' && session.requestedOwnBook;
    writeUciCommand(session.process.stdin, buildSetOptionCommand('OwnBook', useBook));

    if (purpose !== 'opponent') {
      writeUciCommand(session.process.stdin, buildSetOptionCommand('BookSelection', 'best'));
      return;
    }

    if (openingSelection?.mode === 'best') {
      writeUciCommand(session.process.stdin, buildSetOptionCommand('BookSelection', 'best'));
    } else if (openingSelection?.mode === 'top-n-weighted') {
      writeUciCommand(session.process.stdin, buildSetOptionCommand('BookSelection', 'top-n-weighted'));
      writeUciCommand(session.process.stdin, buildSetOptionCommand('BookSelectionTopN', openingSelection.maxCandidates));
    } else {
      writeUciCommand(session.process.stdin, buildSetOptionCommand('BookSelection', 'weighted'));
    }
  }

  private stopPonderAndGo(
    session: EngineSession,
    reason: PonderStopReason,
    pendingFreshSearch?: Extract<PendingOperation, { type: 'search' }>,
  ) {
    if (!session.process.stdin) return;

    const ps = session.ponderState;

    if (ps.status === 'pondering') {
      session.ponderState = { status: 'stopping', reason, pendingFreshSearch };
      writeUciCommand(session.process.stdin, 'stop');
      this.armStopAckWatchdog(session, ps.parentRequestId);
    }
  }

  private sendPositionAndGo(
    session: EngineSession,
    operation: Extract<PendingOperation, { type: 'search' }>,
  ) {
    if (!session.process.stdin) return;

    const positionCommand = buildPositionCommand(operation.startFen, []);
    const goCommand = buildGoCommand(operation.searchLimit);
    const startedAt = Date.now();
    session.searchGeneration += 1;
    session.positionSequence += 1;
    const position = createPositionIdentity({
      requestId: operation.requestId,
      sessionId: session.id,
      sessionGeneration: session.sessionGeneration,
      positionSequence: session.positionSequence,
      fen: operation.startFen,
    });
    const activeSearch: ActiveSearch = {
      requestId: operation.requestId,
      generation: session.searchGeneration,
      state: 'running',
      purpose: operation.purpose,
      searchMode: operation.searchMode,
      ponderAllowed: operation.ponderAllowed,
      ponderContext: operation.ponderContext,
      searchLimit: operation.searchLimit,
      generatedGoCommand: goCommand,
      startedAt,
      deadlineAt: deadlineAtForSearch(operation.startFen, [], operation.searchLimit),
      position,
      positionCommand,
      operation,
    };
    session.activeSearch = activeSearch;
    session.currentDepth = 0;
    session.pvs = [];
    session.pendingBookResult = null;
    session.ponderState = { status: 'idle' };
    session.startFen = operation.startFen;
    session.uciMoves = [];
    // The active identity is installed before `go`, so even an immediate book
    // result is correlated with this exact request.
    writeUciCommand(session.process.stdin, positionCommand);
    this.sendIfOpen(session.ws, JSON.stringify({
      type: 'search-started',
      requestId: operation.requestId,
      sessionId: session.id,
      sessionGeneration: session.sessionGeneration,
      positionSequence: session.positionSequence,
      positionKey: position.key,
      positionFen: position.normalizedFen,
      expectedSide: position.expectedSide,
      positionCommandSent: positionCommand,
      goCommandSent: goCommand,
      startedAt,
      requestedLimit: operation.searchLimit,
      generatedGoCommand: goCommand,
      ...(activeSearch.deadlineAt !== undefined ? { engineDeadlineAt: activeSearch.deadlineAt } : {}),
    }));
    this.armSearchWatchdog(session, operation.requestId, operation.searchLimit, []);
    writeUciCommand(session.process.stdin, goCommand);
    if (process.env.DEBUG === 'true' || process.env.NODE_ENV === 'development') {
      console.log('[engine-search-diagnostic]', {
        requestId: operation.requestId,
        sessionId: session.id,
        sessionGeneration: session.sessionGeneration,
        positionSequence: session.positionSequence,
        positionKey: position.key,
        expectedSide: position.expectedSide,
        purpose: operation.purpose,
        mode: operation.searchMode,
        fen: position.normalizedFen,
        positionCommandSent: positionCommand,
        goCommandSent: goCommand,
        requestedLimit: operation.searchLimit,
        resolvedLimit: operation.searchLimit,
        generatedGoCommand: goCommand,
        snapshotType: 'live',
        ponderState: session.ponderState.status,
        requestAccepted: true,
        resultValidated: false,
        moveApplied: false,
      });
    }
  }

  private armSearchWatchdog(session: EngineSession, requestId: number, limit: SearchLimit, moves: readonly string[]) {
    const timeoutMs = this.deriveSearchWatchdogMs(session.startFen, moves, limit);
    this.armWatchdog(session, {
      requestId,
      timeoutMs,
      phase: 'search',
    });
  }

  private armStopAckWatchdog(session: EngineSession, requestId?: number) {
    this.armWatchdog(session, {
      requestId,
      timeoutMs: this.stopAckTimeoutMs,
      phase: 'stop-ack',
    });
  }

  private armWatchdog(
    session: EngineSession,
    params: { requestId?: number; timeoutMs: number; phase: WatchdogPhase },
  ) {
    this.clearWatchdog(session);
    if (session.awaitingMoveApplication) {
      clearTimeout(session.awaitingMoveApplication.timer);
      session.awaitingMoveApplication = null;
    }
    const startedAt = performance.now();
    const timer = setTimeout(() => {
      this.handleWatchdogTimeout(session.id, params.phase, params.requestId, startedAt);
    }, params.timeoutMs);
    session.watchdog = {
      requestId: params.requestId,
      timer,
      startedAt,
      timeoutMs: params.timeoutMs,
      phase: params.phase,
    };
  }

  private clearWatchdog(session: EngineSession, phase?: WatchdogPhase) {
    if (!session.watchdog) return;
    if (phase !== undefined && session.watchdog.phase !== phase) return;
    clearTimeout(session.watchdog.timer);
    session.watchdog = null;
  }

  private deriveSearchWatchdogMs(startFen: string, moves: readonly string[], limit: SearchLimit): number {
    if (limit.mode === 'movetime') {
      return limit.movetimeMs + this.searchWatchdogToleranceMs;
    }

    if (limit.mode === 'depth') {
      return this.depthSearchWatchdogMs;
    }

    const position = replayPosition(startFen, moves);
    const sideToMove = position?.turn() ?? 'w';
    const remaining = sideToMove === 'w' ? limit.wtime : limit.btime;
    return Math.max(remaining + this.searchWatchdogToleranceMs, this.searchWatchdogToleranceMs);
  }

  private handleWatchdogTimeout(
    id: string,
    phase: WatchdogPhase,
    requestId: number | undefined,
    startedAt: number,
  ) {
    const session = this.active.get(id);
    if (!session?.watchdog) return;
    if (session.watchdog.phase !== phase || session.watchdog.requestId !== requestId || session.watchdog.startedAt !== startedAt) {
      return;
    }

    const code = phase === 'search' ? 'ENGINE_SEARCH_TIMEOUT' : 'ENGINE_STOP_TIMEOUT';
    this.sendIfOpen(session.ws, JSON.stringify({
      type: 'error',
      code,
      ...(requestId !== undefined ? { requestId } : {}),
      message: phase === 'search'
        ? 'Engine search timed out.'
        : 'Engine did not acknowledge stop.',
    }));

    this.recoverEngineSession(session);
  }

  private recoverEngineSession(
    session: EngineSession,
    recoverySearch?: Extract<PendingOperation, { type: 'search' }>,
  ) {
    const { id, ws } = session;
    this.clearWatchdog(session);
    clearTimeout(session.idleTimer);
    clearTimeout(session.sessionTimer);
    session.suppressExitError = true;
    session.activeSearch = null;
    session.pendingOperation = null;
    session.pendingBookResult = null;
    if (session.awaitingMoveApplication) {
      clearTimeout(session.awaitingMoveApplication.timer);
      session.awaitingMoveApplication = null;
    }
    session.ponderState = { status: 'idle' };
    this.active.delete(id);

    if (session.process.stdin) {
      try {
        writeUciCommand(session.process.stdin, 'quit');
      } catch {
        // Process may already be wedged or closed; kill below is authoritative.
      }
    }
    session.process.kill('SIGTERM');
    setTimeout(() => {
      if (session.process.exitCode === null && session.process.signalCode === null) {
        session.process.kill('SIGKILL');
      }
    }, 1000);

    if (this.isSocketOpen(ws)) {
      this.spawnEngine(ws, id, false, recoverySearch, session.requestedOwnBook, session.lastAppliedOpponentFen);
    } else {
      this.dequeueNext();
    }
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
    this.clearWatchdog(session);

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

function parseBookInfo(line: string): PendingBookResult | null {
  const match = /^info string book move ([a-h][1-8][a-h][1-8][qrbn]?) candidates (\d+)(?: weight (\d+))?$/.exec(line);
  if (!match) return null;

  const candidateCount = Number.parseInt(match[2], 10);
  const selectedWeight = match[3] === undefined ? undefined : Number.parseInt(match[3], 10);
  if (!Number.isSafeInteger(candidateCount) || candidateCount < 1) return null;
  if (selectedWeight !== undefined && (!Number.isSafeInteger(selectedWeight) || selectedWeight < 0)) return null;

  return {
    move: match[1],
    candidateCount,
    selectedWeight,
  };
}

function isCanonicalUciMove(move: string | null | undefined): move is string {
  return typeof move === 'string' &&
    move !== '0000' &&
    /^[a-h][1-8][a-h][1-8][qrbn]?$/.test(move);
}

function applyUciMove(chess: Chess, move: string): boolean {
  if (!isCanonicalUciMove(move)) {
    return false;
  }

  try {
    const applied = chess.move({
      from: move.slice(0, 2),
      to: move.slice(2, 4),
      promotion: move.length === 5 ? move[4] : undefined,
    });
    return applied !== null;
  } catch {
    return false;
  }
}

function replayPosition(startFen: string, moves: readonly string[]): Chess | null {
  let chess: Chess;
  try {
    chess = new Chess(startFen);
  } catch {
    return null;
  }

  for (const move of moves) {
    if (!applyUciMove(chess, move)) {
      return null;
    }
  }

  return chess;
}

function deadlineAtForSearch(startFen: string, moves: readonly string[], limit: SearchLimit): number | undefined {
  if (limit.mode !== 'clock') {
    return undefined;
  }

  const position = replayPosition(startFen, moves);
  const sideToMove = position?.turn() ?? 'w';
  const remaining = sideToMove === 'w' ? limit.wtime : limit.btime;
  return Date.now() + Math.max(0, remaining);
}

function searchLimitFromProfile(profile: { depth?: number; movetimeMs?: number }): SearchLimit {
  if (profile.depth !== undefined) {
    return { mode: 'depth', depth: profile.depth };
  }
  if (profile.movetimeMs !== undefined) {
    return { mode: 'movetime', movetimeMs: profile.movetimeMs };
  }
  return { mode: 'movetime', movetimeMs: 3000 };
}

function getPonderSearchContext(limit: SearchLimit): PonderSearchContext | null {
  return limit;
}

function buildValidatedPonderPlan(
  startFen: string,
  priorMoves: readonly string[],
  bestMove: string,
  ponderMove: string | null,
  searchContext: PonderSearchContext,
): { expectedPlayerMove: string; expectedMoves: string[]; searchContext: PonderSearchContext } | null {
  if (!isCanonicalUciMove(bestMove) || !isCanonicalUciMove(ponderMove)) {
    return null;
  }

  const chess = replayPosition(startFen, priorMoves);
  if (!chess) {
    return null;
  }

  if (!applyUciMove(chess, bestMove)) {
    return null;
  }

  if (chess.isGameOver()) {
    return null;
  }

  if (!applyUciMove(chess, ponderMove)) {
    return null;
  }

  return {
    expectedPlayerMove: ponderMove,
    expectedMoves: [...priorMoves, bestMove, ponderMove],
    searchContext,
  };
}

export const enginePool = new EnginePoolManager();
