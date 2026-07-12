import { useState, useRef, useCallback, useEffect } from 'react';
import type { EngineInfo } from '../types/engine';
import { useToast } from '../components/ui/Toast';
import { EngineRequestId, shouldAcceptSearchResponse } from '../lib/engine-response-filter';
import type { EngineDifficulty, SearchPurpose } from '../lib/engine-difficulty';

type ConnectionStatus = 'connecting' | 'queued' | 'idle' | 'thinking' | 'analyzing' | 'disconnected' | 'session_expired' | 'error';

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
  /** Number of principal variations to calculate (1-3). */
  multiPv?: number;
}

export function useEngine() {
  const [status, setReactStatus] = useState<ConnectionStatus>('disconnected');
  const [engineInfo, setEngineInfo] = useState<EngineInfo | null>(null);
  const [bestMove, setBestMove] = useState<string | null>(null);
  const [queuePosition, setQueuePosition] = useState<number | null>(null);
  const nextRequestIdRef = useRef<EngineRequestId>(1);
  const activeRequestIdRef = useRef<EngineRequestId | null>(null);
  const activeRootFenRef = useRef<string | null>(null);
  const activePurposeRef = useRef<SearchPurpose | null>(null);

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

  const invalidateActiveRequest = useCallback(() => {
    activeRequestIdRef.current = null;
    activeRootFenRef.current = null;
    activePurposeRef.current = null;
    setBestMove(null);
  }, []);

  const activateRequest = useCallback((requestId: EngineRequestId, rootFen: string, purpose: SearchPurpose) => {
    activeRequestIdRef.current = requestId;
    activeRootFenRef.current = rootFen;
    activePurposeRef.current = purpose;
    setBestMove(null);
    setEngineInfo(null);
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
            if (stateRef.current.status !== 'thinking' && stateRef.current.status !== 'analyzing') {
              setStatus('idle');
            }
            setQueuePosition(null);
            break;
          case 'queued':
            setStatus('queued');
            setQueuePosition(data.position);
            break;
          case 'info':
            if (!stateRef.current.isWaitingForNewGameReady &&
                shouldAcceptSearchResponse({ activeRequestId: activeRequestIdRef.current }, data.requestId)) {
              setEngineInfo(prev => ({
                requestId: data.requestId,
                rootFen: activeRootFenRef.current ?? undefined,
                depth: data.depth,
                pvs: data.pvs || [],
                nodes: data.nodes ?? prev?.nodes,
                time: data.time ?? prev?.time,
              }));
            }
            break;
          case 'bestmove':
            if (!stateRef.current.isWaitingForNewGameReady &&
                shouldAcceptSearchResponse({ activeRequestId: activeRequestIdRef.current }, data.requestId)) {
              if (activePurposeRef.current === 'opponent') {
                setBestMove(data.move);
              }
              activeRequestIdRef.current = null;
              activePurposeRef.current = null;
              setStatus('idle');
            }
            break;
          case 'session_expired':
            invalidateActiveRequest();
            setStatus('session_expired');
            addToast('Session expired due to inactivity', 'warning');
            break;
          case 'released':
            invalidateActiveRequest();
            setStatus('disconnected');
            break;
          case 'error':
            if (data.requestId === undefined ||
                shouldAcceptSearchResponse({ activeRequestId: activeRequestIdRef.current }, data.requestId)) {
              invalidateActiveRequest();
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
      setStatus((stateRef.current.status === 'session_expired' ? 'session_expired' : 'disconnected'));
      ws.current = null;
    };

    ws.current = socket;
  }, [addToast, invalidateActiveRequest, setStatus]);

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
    moves: string[],
    options: SendMoveOptions,
  ) => {
    if (ws.current?.readyState === WebSocket.OPEN && stateRef.current.status === 'idle') {
      setStatus('thinking');
      const requestId = allocateRequestId();
      activateRequest(requestId, fen, 'opponent');

      ws.current.send(JSON.stringify({
        type: 'move',
        requestId,
        purpose: 'opponent',
        fen,
        moves,
        wtime:      options.wtime      ?? 0,
        btime:      options.btime      ?? 0,
        winc:       options.winc       ?? 0,
        binc:       options.binc       ?? 0,
        depth:      options.depth,
        multiPv:    options.multiPv ?? 1,
        difficulty: options.difficulty,
      }));
    }
  }, [activateRequest, allocateRequestId, setStatus]);

  const newGame = useCallback(() => {
    invalidateActiveRequest();
    if (ws.current?.readyState === WebSocket.OPEN) {
      stateRef.current.isWaitingForNewGameReady = true;
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
  ) => {
    if (ws.current?.readyState === WebSocket.OPEN) {
      const requestId = allocateRequestId();
      setStatus('analyzing');
      activateRequest(requestId, fen, purpose);
      ws.current.send(JSON.stringify({ type: 'analyze', requestId, purpose, fen, moves, depth, multiPv }));
    }
  }, [activateRequest, allocateRequestId, setStatus]);

  return {
    status,
    engineInfo,
    bestMove,
    queuePosition,
    sendMove,
    newGame,
    reconnect,
    setEngineOption,
    setPosition,
    stopEngine,
    startAnalysis,
    releaseSession,
    invalidateActiveRequest,
  };
}
