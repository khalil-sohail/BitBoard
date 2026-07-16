import type { ConnectionStatus } from '../../hooks/useEngine';
import type { PlayerColor } from '../../types/engine';

export type FairPlaySidebarState = 'idle' | 'starting' | 'connecting' | 'queued' | 'active' | 'completed' | 'error';
export type FairPlayLifecycleStatus = 'idle' | 'active' | 'completed';

export interface FairPlayStatusInput {
  lifecycle: FairPlayLifecycleStatus;
  connectionStatus: ConnectionStatus;
  queuePosition: number | null;
  searchRetryCount: number | null;
  waitingForSessionReady: boolean;
  currentTurn: PlayerColor;
  playerColor: PlayerColor;
}

export interface FairPlayStatusPresentation {
  state: FairPlaySidebarState;
  headline: string;
  detail: string;
  tone: 'neutral' | 'healthy' | 'waiting' | 'error';
}

export function deriveFairPlayStatus(input: FairPlayStatusInput): FairPlayStatusPresentation {
  if (input.lifecycle === 'idle') {
    return {
      state: 'idle',
      headline: 'Ready for a fair game',
      detail: 'Choose your side and time control to begin.',
      tone: 'neutral',
    };
  }
  if (input.lifecycle === 'completed') {
    return {
      state: 'completed',
      headline: 'Game over',
      detail: 'The final position and move record remain available.',
      tone: 'neutral',
    };
  }

  if (input.connectionStatus === 'connecting') {
    return { state: 'connecting', headline: 'Connecting to engine…', detail: 'Clocks remain paused until a session is ready.', tone: 'waiting' };
  }
  if (input.connectionStatus === 'queued') {
    const position = input.queuePosition === null ? '' : ` — position ${input.queuePosition}`;
    return { state: 'queued', headline: `Waiting in queue${position}`, detail: 'Clocks remain paused while an engine session is assigned.', tone: 'waiting' };
  }
  if (input.waitingForSessionReady) {
    return { state: 'starting', headline: 'Preparing engine session…', detail: 'Clocks remain paused until the new game is ready.', tone: 'waiting' };
  }
  if (input.connectionStatus === 'disconnected') {
    return { state: 'error', headline: 'Engine connection lost', detail: 'The game clock is paused. Start a new session when the service is available.', tone: 'error' };
  }
  if (input.connectionStatus === 'session_expired') {
    return { state: 'error', headline: 'Engine session expired', detail: 'The current session is unavailable. Start a new game to continue.', tone: 'error' };
  }
  if (input.connectionStatus === 'error') {
    return { state: 'error', headline: 'Engine session unavailable', detail: 'The game cannot continue through this session.', tone: 'error' };
  }
  if (input.connectionStatus === 'result_ready') {
    return { state: 'active', headline: 'Move received, applying…', detail: 'The engine move is being validated and acknowledged.', tone: 'waiting' };
  }
  if (input.connectionStatus === 'thinking') {
    if (input.searchRetryCount !== null) {
      return { state: 'active', headline: 'Retrying engine move…', detail: `Controlled retry ${input.searchRetryCount} is in progress; the clock is paused.`, tone: 'waiting' };
    }
    return { state: 'active', headline: 'Engine thinking', detail: 'Waiting for the engine move.', tone: 'waiting' };
  }

  const isPlayerTurn = input.currentTurn === input.playerColor;
  return isPlayerTurn
    ? { state: 'active', headline: 'Your turn', detail: 'The engine session is ready.', tone: 'healthy' }
    : { state: 'active', headline: 'Waiting for engine', detail: 'The engine move will begin when the session is ready.', tone: 'waiting' };
}

export function deriveCompletedHeadline(message: string, playerColor: PlayerColor): string {
  const normalized = message.toLowerCase();
  if (normalized.startsWith('draw')) return 'Draw';
  if (normalized.startsWith('white wins')) return playerColor === 'w' ? 'You won' : 'Engine won';
  if (normalized.startsWith('black wins')) return playerColor === 'b' ? 'You won' : 'Engine won';
  if (normalized.includes('engine session')) return 'Session ended';
  return 'Game over';
}

export function formatClockMs(ms: number): string {
  const totalSeconds = Math.ceil(Math.max(0, ms) / 1000);
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  if (minutes >= 60) {
    const hours = Math.floor(minutes / 60);
    return `${hours}:${String(minutes % 60).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
  }
  return `${minutes}:${String(seconds).padStart(2, '0')}`;
}
