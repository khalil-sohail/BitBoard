import { useState, useRef, useCallback, useEffect } from 'react';
import { EngineInfo } from '../types/engine';
import { useToast } from '../components/ui/Toast';

type ConnectionStatus = 'connecting' | 'queued' | 'ready' | 'thinking' | 'disconnected' | 'session_expired' | 'error';

export interface SendMoveOptions {
  /** Remaining white clock time in ms (0 = no time control). */
  wtime?: number;
  /** Remaining black clock time in ms (0 = no time control). */
  btime?: number;
  /** White increment per move in ms. */
  winc?: number;
  /** Black increment per move in ms. */
  binc?: number;
  /** Legacy difficulty string — only used when wtime/btime are both 0. */
  difficulty?: string;
  /** Optional depth limit to cap the engine's search. */
  depth?: number;
}

export function useEngine() {
  const [status, setReactStatus] = useState<ConnectionStatus>('disconnected');
  const [engineInfo, setEngineInfo] = useState<EngineInfo | null>(null);
  const [bestMove, setBestMove] = useState<string | null>(null);
  const [queuePosition, setQueuePosition] = useState<number | null>(null);

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
            if (stateRef.current.status !== 'thinking') {
              setStatus('ready');
            }
            setQueuePosition(null);
            break;
          case 'queued':
            setStatus('queued');
            setQueuePosition(data.position);
            break;
          case 'info':
            if (!stateRef.current.isWaitingForNewGameReady) {
              setEngineInfo(prev => ({
                depth: data.depth,
                pvs: data.pvs || [],
                nodes: data.nodes ?? prev?.nodes,
                time: data.time ?? prev?.time,
              }));
            }
            break;
          case 'bestmove':
            if (!stateRef.current.isWaitingForNewGameReady) {
              setBestMove(data.move);
              setStatus('ready');
            }
            break;
          case 'session_expired':
            setStatus('session_expired');
            addToast('Session expired due to inactivity', 'warning');
            break;
          case 'released':
            setStatus('disconnected');
            break;
          case 'error':
            setStatus('error');
            addToast(data.message || 'Engine error occurred', 'error');
            break;
        }
      } catch (e) {
        console.error('Failed to parse WS message', e);
      }
    };

    socket.onclose = () => {
      setStatus((stateRef.current.status === 'session_expired' ? 'session_expired' : 'disconnected'));
      ws.current = null;
    };

    ws.current = socket;
  }, [addToast, setStatus]);

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
   * Otherwise it falls back to the legacy difficulty movetime approach.
   */
  const sendMove = useCallback((
    fen: string,
    moves: string[],
    options: SendMoveOptions | string = 'standard',
  ) => {
    if (ws.current?.readyState === WebSocket.OPEN && stateRef.current.status === 'ready') {
      setStatus('thinking');
      setBestMove(null);
      setEngineInfo(null);

      // Normalise: accept either the new options object or the legacy difficulty string
      const opts: SendMoveOptions = typeof options === 'string'
        ? { difficulty: options }
        : options;

      ws.current.send(JSON.stringify({
        type: 'move',
        fen,
        moves,
        wtime:      opts.wtime      ?? 0,
        btime:      opts.btime      ?? 0,
        winc:       opts.winc       ?? 0,
        binc:       opts.binc       ?? 0,
        depth:      opts.depth,
        difficulty: opts.difficulty ?? 'standard',
      }));
    }
  }, [setStatus]);

  const newGame = useCallback(() => {
    if (ws.current?.readyState === WebSocket.OPEN) {
      stateRef.current.isWaitingForNewGameReady = true;
      setStatus('ready');
      setBestMove(null);
      setEngineInfo(null);
      ws.current.send(JSON.stringify({ type: 'newgame' }));
    } else {
      connect();
    }
  }, [connect, setStatus]);

  const reconnect = useCallback(() => {
    if (ws.current) {
      ws.current.close();
    }
    connect();
  }, [connect]);

  const releaseSession = useCallback(() => {
    if (ws.current?.readyState === WebSocket.OPEN) {
      ws.current.send(JSON.stringify({ type: 'releaseSession' }));
      setStatus('disconnected');
    }
  }, [setStatus]);

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
    if (ws.current?.readyState === WebSocket.OPEN) {
      ws.current.send(JSON.stringify({ type: 'position', fen, moves }));
    }
  }, []);

  const stopEngine = useCallback(() => {
    if (ws.current?.readyState === WebSocket.OPEN) {
      ws.current.send(JSON.stringify({ type: 'stop' }));
      setStatus('ready'); // Optionally transition to ready immediately
    }
  }, [setStatus]);

  const startAnalysis = useCallback((fen: string, moves: string[] = [], depth?: number) => {
    if (ws.current?.readyState === WebSocket.OPEN) {
      setStatus('thinking');
      setBestMove(null);
      setEngineInfo(null);
      ws.current.send(JSON.stringify({ type: 'analyze', fen, moves, depth }));
    }
  }, [setStatus]);

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
  };
}
