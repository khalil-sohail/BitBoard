import { useState, useRef, useCallback, useEffect } from 'react';
import { EngineInfo } from '../types/engine';
import { useToast } from '../components/ui/Toast';

type ConnectionStatus = 'connecting' | 'queued' | 'ready' | 'thinking' | 'disconnected' | 'session_expired' | 'error';

export function useEngine() {
  const [status, setReactStatus] = useState<ConnectionStatus>('disconnected');
  const [engineInfo, setEngineInfo] = useState<EngineInfo | null>(null);
  const [bestMove, setBestMove] = useState<string | null>(null);
  const [queuePosition, setQueuePosition] = useState<number | null>(null);
  
  // Track state internally to avoid stale closures in WS message handlers
  // isWaitingForNewGameReady prevents old 'bestmove' events from bleeding into new games
  const stateRef = useRef({
      status: 'disconnected' as ConnectionStatus,
      isWaitingForNewGameReady: false
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
      // Keep as connecting until we get 'ready' or 'queued' message
    };

    socket.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);
        
        switch (data.type) {
          case 'ready':
            stateRef.current.isWaitingForNewGameReady = false;
            // Only set to ready if we didn't already start a new search
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
                  time: data.time ?? prev?.time
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

  const sendMove = useCallback((fen: string, moves: string[], difficulty: string = 'standard') => {
    if (ws.current?.readyState === WebSocket.OPEN && stateRef.current.status === 'ready') {
      setStatus('thinking');
      setBestMove(null);
      setEngineInfo(null);
      ws.current.send(JSON.stringify({ type: 'move', fen, moves, difficulty }));
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

  const startAnalysis = useCallback((fen: string, moves: string[] = []) => {
    if (ws.current?.readyState === WebSocket.OPEN) {
      setStatus('thinking');
      setBestMove(null);
      setEngineInfo(null);
      ws.current.send(JSON.stringify({ type: 'analyze', fen, moves }));
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
    startAnalysis,
  };
}
