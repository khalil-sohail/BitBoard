import { useState, useRef, useCallback, useEffect } from 'react';
import type { EngineBestMoveResult, EngineInfo, EngineTerminalCompletion, GameMode } from '../types/engine';
import { useToast } from '../components/ui/Toast';
import { EngineRequestId, shouldAcceptSearchResponse } from '../lib/engine-response-filter';
import type { EngineDifficulty, SearchPurpose } from '../lib/engine-difficulty';
import type { SearchLimit } from '../lib/engine-protocol';
import {
  matchesActiveOpponentResult,
  statusAfterBestMove,
  type OpponentMoveApplicationReceipt,
} from '../lib/engine-result-ownership';

export type ConnectionStatus = 'connecting' | 'queued' | 'idle' | 'thinking' | 'analyzing' | 'result_ready' | 'disconnected' | 'session_expired' | 'error';

export interface SendMoveOptions {
  /** Remaining white clock time in ms (0 = no time control). */
  wtime?: number;
  /** Remaining black clock time in ms (0 = no time control). */
  btime?: number;
  /** White increment per move in ms. */
  winc?: number;
  /** Black increment per move in ms. */
  binc?: number;
  /** Opponent strength profile. Review and analysis requests never use this. */
  difficulty: EngineDifficulty;
  /** Optional depth limit to cap the engine's search. */
  depth?: number;
  /** Resolved authoritative search limit. */
  searchLimit?: SearchLimit;
  /** Number of principal variations to calculate (1-3). */
  multiPv?: number;
  /** Explicitly allow backend-managed pondering for fair engine games only. */
  ponder?: boolean;
  policySource?: string;
  mode?: GameMode;
  trainingPonderEnabled?: boolean;
}

export function useEngine() {
  const [status, setReactStatus] = useState<ConnectionStatus>('disconnected');
  const [engineInfo, setEngineInfo] = useState<EngineInfo | null>(null);
  const [terminalCompletion, setTerminalCompletion] = useState<EngineTerminalCompletion | null>(null);
  const [bestMoveResult, setBestMoveResult] = useState<EngineBestMoveResult | null>(null);
  const [queuePosition, setQueuePosition] = useState<number | null>(null);
  const [searchRetryCount, setSearchRetryCount] = useState<number | null>(null);
  const [waitingForSessionReady, setWaitingForSessionReady] = useState(false);
  const nextRequestIdRef = useRef<EngineRequestId>(1);
  const activeRequestIdRef = useRef<EngineRequestId | null>(null);
  const activeRootFenRef = useRef<string | null>(null);
  const activePurposeRef = useRef<SearchPurpose | null>(null);
  const activeLimitRef = useRef<SearchLimit | null>(null);
  const activePositionKeyRef = useRef<string | null>(null);
  const activeSessionGenerationRef = useRef<number | null>(null);
  const activeSessionIdRef = useRef<string | null>(null);
  const activePositionFenRef = useRef<string | null>(null);
  const activeModeRef = useRef<GameMode | null>(null);
  const [searchStartedRequestId, setSearchStartedRequestId] = useState<EngineRequestId | null>(null);
  const [searchStartedPositionKey, setSearchStartedPositionKey] = useState<string | null>(null);
  const [clockStopSignal, setClockStopSignal] = useState(0);

  // Track state internally to avoid stale closures in WS message handlers
  const stateRef = useRef({
    status: 'disconnected' as ConnectionStatus,
    isWaitingForNewGameReady: false,
  });

  const setStatus = useCallback((s: ConnectionStatus) => {
    stateRef.current.status = s;
    setReactStatus(s);
  }, []);

  const ws = useRef<WebSocket | null>(null);
  const { addToast } = useToast();

  const allocateRequestId = useCallback((): EngineRequestId => {
    const requestId = nextRequestIdRef.current;
    nextRequestIdRef.current += 1;
    return requestId;
  }, []);

  const clearActiveRequestIdentity = useCallback(() => {
    activeRequestIdRef.current = null;
    activeRootFenRef.current = null;
    activePurposeRef.current = null;
    activeLimitRef.current = null;
    setSearchStartedRequestId(null);
    activePositionKeyRef.current = null;
    activeSessionGenerationRef.current = null;
    activeSessionIdRef.current = null;
    activePositionFenRef.current = null;
    activeModeRef.current = null;
    setSearchStartedPositionKey(null);
    setSearchRetryCount(null);
  }, []);

  const invalidateActiveRequest = useCallback(() => {
    clearActiveRequestIdentity();
    setBestMoveResult(null);
    setTerminalCompletion(null);
  }, [clearActiveRequestIdentity]);

  const activateRequest = useCallback((requestId: EngineRequestId, rootFen: string, purpose: SearchPurpose, limit?: SearchLimit, mode?: GameMode) => {
    activeRequestIdRef.current = requestId;
    activeRootFenRef.current = rootFen;
    activePurposeRef.current = purpose;
    activeLimitRef.current = limit ?? null;
    activeModeRef.current = mode ?? null;
    setBestMoveResult(null);
    setEngineInfo(null);
    setTerminalCompletion(null);
    setSearchStartedRequestId(null);
    setSearchRetryCount(null);
  }, []);

  const connect = useCallback(() => {
    if (ws.current?.readyState === WebSocket.OPEN) return;

    setStatus('connecting');
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const socket = new WebSocket(`${protocol}//${window.location.host}/api/engine`);

    socket.onopen = () => {
      // Wait for 'ready' or 'queued' before changing status
    };

    socket.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);

        switch (data.type) {
          case 'ready':
            stateRef.current.isWaitingForNewGameReady = false;
            setWaitingForSessionReady(false);
            if (stateRef.current.status !== 'thinking' && stateRef.current.status !== 'analyzing' && stateRef.current.status !== 'result_ready') {
              setStatus('idle');
            }
            setQueuePosition(null);
            break;
          case 'queued':
            setStatus('queued');
            setQueuePosition(data.position);
            break;
          case 'search-started':
            if (shouldAcceptSearchResponse({ activeRequestId: activeRequestIdRef.current }, data.requestId)) {
              activePositionKeyRef.current = data.positionKey ?? null;
              activeSessionGenerationRef.current = data.sessionGeneration ?? null;
              activeSessionIdRef.current = data.sessionId ?? null;
              activePositionFenRef.current = data.positionFen ?? null;
              setSearchStartedRequestId(data.requestId);
              setSearchStartedPositionKey(data.positionKey ?? null);
            }
            break;
          case 'info':
            if (!stateRef.current.isWaitingForNewGameReady &&
                shouldAcceptSearchResponse({ activeRequestId: activeRequestIdRef.current }, data.requestId)) {
              setEngineInfo(prev => ({
                requestId: data.requestId,
                rootFen: activeRootFenRef.current ?? undefined,
                purpose: activePurposeRef.current ?? undefined,
                requestedLimit: activeLimitRef.current ?? undefined,
                depth: data.depth,
                reportedDepth: data.depth,
                selectiveDepth: data.selectiveDepth ?? null,
                pvs: data.pvs || [],
                nodes: data.nodes ?? prev?.nodes,
                time: data.time ?? prev?.time,
              }));
            }
            break;
          case 'bestmove':
            if (!stateRef.current.isWaitingForNewGameReady &&
                shouldAcceptSearchResponse({ activeRequestId: activeRequestIdRef.current }, data.requestId) &&
                (data.terminal || (activePositionKeyRef.current === data.positionKey &&
                activeSessionGenerationRef.current === data.sessionGeneration &&
                activeSessionIdRef.current === data.sessionId &&
                activePositionFenRef.current === data.positionFen))) {
              const purpose = activePurposeRef.current ?? undefined;
              const rootFen = activeRootFenRef.current ?? undefined;
              const result: EngineBestMoveResult = {
                requestId: data.requestId,
                rootFen,
                positionFen: data.positionFen,
                positionKey: data.positionKey,
                sessionId: data.sessionId,
                sessionGeneration: data.sessionGeneration,
                positionSequence: data.positionSequence,
                expectedSide: data.expectedSide,
                purpose,
                move: data.move,
                receivedAt: data.receivedAt,
                engineDeadlineAt: data.engineDeadlineAt,
              };
              setBestMoveResult(result);
              setClockStopSignal(value => value + 1);
              if (data.move === null && data.terminal) {
                setTerminalCompletion({
                  requestId: data.requestId,
                  rootFen,
                  purpose,
                  terminal: data.terminal,
                });
                clearActiveRequestIdentity();
                setStatus('idle');
              } else {
                const nextStatus = statusAfterBestMove(result);
                if (nextStatus === 'idle') {
                  // Review, hint, and Analysis searches are informational. Their
                  // bestmove finalizes the snapshot but never awaits board application.
                  clearActiveRequestIdentity();
                }
                setStatus(nextStatus);
              }
            }
            break;
          case 'search-retrying':
            if (shouldAcceptSearchResponse({ activeRequestId: activeRequestIdRef.current }, data.requestId)) {
              activePositionKeyRef.current = null;
              activeSessionGenerationRef.current = null;
              activeSessionIdRef.current = null;
              activePositionFenRef.current = null;
              setBestMoveResult(null);
              setSearchStartedRequestId(null);
              setSearchStartedPositionKey(null);
              setClockStopSignal(value => value + 1);
              setSearchRetryCount(typeof data.retryCount === 'number' ? data.retryCount : 1);
              setStatus('thinking');
            }
            break;
          case 'move-applied':
            if (shouldAcceptSearchResponse({ activeRequestId: activeRequestIdRef.current }, data.requestId)) {
              invalidateActiveRequest();
              setStatus('idle');
            }
            break;
          case 'session_expired':
            invalidateActiveRequest();
            setWaitingForSessionReady(false);
            setStatus('session_expired');
            addToast('Session expired due to inactivity', 'warning');
            break;
          case 'released':
            invalidateActiveRequest();
            setWaitingForSessionReady(false);
            setStatus('disconnected');
            break;
          case 'error':
            if (data.requestId === undefined ||
                shouldAcceptSearchResponse({ activeRequestId: activeRequestIdRef.current }, data.requestId)) {
              invalidateActiveRequest();
              setWaitingForSessionReady(false);
              setClockStopSignal(value => value + 1);
              setStatus('error');
              addToast(data.message || 'Engine error occurred', 'error');
            }
            break;
        }
      } catch (e) {
        console.error('Failed to parse WS message', e);
      }
    };

    socket.onclose = () => {
      invalidateActiveRequest();
      setWaitingForSessionReady(false);
      setStatus((stateRef.current.status === 'session_expired' ? 'session_expired' : 'disconnected'));
      ws.current = null;
    };

    ws.current = socket;
  }, [addToast, clearActiveRequestIdentity, invalidateActiveRequest, setStatus]);

  useEffect(() => {
    connect();
    return () => {
      if (ws.current) {
        ws.current.close();
      }
    };
  }, [connect]);

  /**
   * Send a move to the engine.
   *
   * If wtime/btime are provided (> 0), the engine will use real time controls.
   * Otherwise it uses the selected opponent difficulty profile.
   */
  const sendMove = useCallback((
    fen: string,
    options: SendMoveOptions,
  ) => {
    if (ws.current?.readyState === WebSocket.OPEN && stateRef.current.status === 'idle') {
      setStatus('thinking');
      const requestId = allocateRequestId();
      const resolvedLimit = options.searchLimit ?? (options.depth !== undefined ? { mode: 'depth' as const, depth: options.depth } : undefined);
      activateRequest(requestId, fen, 'opponent', resolvedLimit, options.mode);

      if (process.env.NODE_ENV !== 'production') {
        console.debug('[engine-search-request]', {
          requestId,
          purpose: 'opponent',
          mode: options.mode,
          fen,
          trainingPonderEnabled: options.trainingPonderEnabled,
          requestedLimit: options.depth !== undefined ? { mode: 'depth', depth: options.depth } : undefined,
          resolvedLimit,
          policySource: options.policySource,
          ponder: options.ponder === true,
          accepted: true,
        });
      }

      ws.current.send(JSON.stringify({
        type: 'move',
        requestId,
        purpose: 'opponent',
        fen,
        // The FEN is the complete authoritative position. Replaying the game
        // history on top of it was the Fair Play desynchronization bug.
        moves: [],
        wtime:      options.wtime      ?? 0,
        btime:      options.btime      ?? 0,
        winc:       options.winc       ?? 0,
        binc:       options.binc       ?? 0,
        depth:      options.searchLimit ? undefined : options.depth,
        searchLimit: options.searchLimit,
        multiPv:    options.multiPv ?? 1,
        difficulty: options.difficulty,
        ponder:     options.ponder === true,
      }));
    }
  }, [activateRequest, allocateRequestId, setStatus]);

  const newGame = useCallback(() => {
    invalidateActiveRequest();
    if (ws.current?.readyState === WebSocket.OPEN) {
      stateRef.current.isWaitingForNewGameReady = true;
      setWaitingForSessionReady(true);
      setStatus('idle');
      setEngineInfo(null);
      ws.current.send(JSON.stringify({ type: 'newgame' }));
    } else {
      connect();
    }
  }, [connect, invalidateActiveRequest, setStatus]);

  const reconnect = useCallback(() => {
    invalidateActiveRequest();
    if (ws.current) {
      ws.current.close();
    }
    connect();
  }, [connect, invalidateActiveRequest]);

  const releaseSession = useCallback(() => {
    invalidateActiveRequest();
    if (ws.current?.readyState === WebSocket.OPEN) {
      ws.current.send(JSON.stringify({ type: 'releaseSession' }));
      setStatus('disconnected');
    }
  }, [invalidateActiveRequest, setStatus]);

  const setEngineOption = useCallback((name: string, value: string | boolean | number) => {
    if (ws.current?.readyState === WebSocket.OPEN) {
      ws.current.send(JSON.stringify({ type: 'setoption', name, value }));
    }
  }, []);

  /**
   * Tell the engine about a position without triggering a search.
   * Used in Analysis Mode when the user loads a custom FEN.
   */
  const setPosition = useCallback((fen: string, moves: string[] = []) => {
    invalidateActiveRequest();
    if (ws.current?.readyState === WebSocket.OPEN) {
      ws.current.send(JSON.stringify({ type: 'position', fen, moves }));
    }
  }, [invalidateActiveRequest]);

  const stopEngine = useCallback(() => {
    const requestId = activeRequestIdRef.current;
    invalidateActiveRequest();
    if (ws.current?.readyState === WebSocket.OPEN) {
      ws.current.send(JSON.stringify(requestId === null ? { type: 'stop' } : { type: 'stop', requestId }));
    }
  }, [invalidateActiveRequest]);

  const startAnalysis = useCallback((
    fen: string,
    moves: string[] = [],
    depth?: number,
    multiPv?: number,
    purpose: Exclude<SearchPurpose, 'opponent'> = 'analysis',
    searchLimit?: SearchLimit,
    policySource?: string,
    mode?: GameMode,
  ): EngineRequestId | null => {
    if (ws.current?.readyState === WebSocket.OPEN) {
      const requestId = allocateRequestId();
      setStatus('analyzing');
      const resolvedLimit = searchLimit ?? (depth !== undefined ? { mode: 'depth' as const, depth } : undefined);
      activateRequest(requestId, fen, purpose, resolvedLimit, mode);
      if (process.env.NODE_ENV !== 'production') {
        console.debug('[engine-search-request]', {
          requestId,
          purpose,
          mode,
          fen,
          requestedLimit: depth !== undefined ? { mode: 'depth', depth } : undefined,
          resolvedLimit,
          policySource,
          ponder: false,
          accepted: true,
        });
      }
      ws.current.send(JSON.stringify({ type: 'analyze', requestId, purpose, fen, moves, depth: searchLimit ? undefined : depth, searchLimit, multiPv }));
      return requestId;
    }
    return null;
  }, [activateRequest, allocateRequestId, setStatus]);

  const acknowledgeAppliedEngineMove = useCallback((params: {
    receipt: OpponentMoveApplicationReceipt;
    applied: boolean;
    oldFen: string;
    newFen?: string;
    failureReason?: string;
  }) => {
    const { receipt, ...acknowledgment } = params;
    const activeIdentity = {
      ownerMode: activeModeRef.current,
      purpose: activePurposeRef.current,
      requestId: activeRequestIdRef.current,
      positionKey: activePositionKeyRef.current,
      sessionId: activeSessionIdRef.current,
      sessionGeneration: activeSessionGenerationRef.current,
      positionFen: activePositionFenRef.current,
    };
    const identityMatches = matchesActiveOpponentResult(activeIdentity, receipt);
    const socket = ws.current;
    if (!identityMatches || socket?.readyState !== WebSocket.OPEN) return false;
    socket.send(JSON.stringify({
      type: 'resultAck',
      requestId: receipt.requestId,
      positionKey: receipt.positionKey,
      ...acknowledgment,
    }));
    return true;
  }, []);

  return {
    status,
    engineInfo,
    bestMoveResult,
    terminalCompletion,
    queuePosition,
    searchRetryCount,
    waitingForSessionReady,
    searchStartedRequestId,
    searchStartedPositionKey,
    clockStopSignal,
    sendMove,
    newGame,
    reconnect,
    setEngineOption,
    setPosition,
    stopEngine,
    startAnalysis,
    releaseSession,
    acknowledgeAppliedEngineMove,
    invalidateActiveRequest,
  };
}
