import type { PlayerColor } from '../types/engine';

export type GameClockStatus = 'idle' | 'active' | 'completed';

export interface LegalMoveClockTransition {
  stopClock: boolean;
  incrementSide?: PlayerColor;
  nextActiveSide?: PlayerColor;
  completeGame: boolean;
}

// Product policy: a legal terminal move receives its increment, but the
// opponent clock never starts after checkmate, stalemate, draw, or timeout.
export function clockTransitionAfterLegalMove(params: {
  hasTimeControl: boolean;
  mover: PlayerColor;
  nextSide: PlayerColor;
  isTerminal: boolean;
}): LegalMoveClockTransition {
  if (!params.hasTimeControl) {
    return { stopClock: false, completeGame: params.isTerminal };
  }

  return {
    stopClock: true,
    incrementSide: params.mover,
    nextActiveSide: params.isTerminal ? undefined : params.nextSide,
    completeGame: params.isTerminal,
  };
}

export function shouldAcceptEngineBestMove(params: {
  gameStatus: GameClockStatus;
  timeoutColor: PlayerColor | null;
}): boolean {
  return params.gameStatus === 'active' && params.timeoutColor === null;
}

export function shouldStartEngineClockForSearch(params: {
  hasTimeControl: boolean;
  gameStatus: GameClockStatus;
  isTerminal: boolean;
  timeoutColor: PlayerColor | null;
  turn: PlayerColor;
  engineColor: PlayerColor;
  searchStartedRequestId: number | null;
  lastStartedRequestId: number | null;
}): boolean {
  return params.hasTimeControl &&
    params.gameStatus === 'active' &&
    !params.isTerminal &&
    params.timeoutColor === null &&
    params.turn === params.engineColor &&
    params.searchStartedRequestId !== null &&
    params.searchStartedRequestId !== params.lastStartedRequestId;
}

export function shouldStartPlayerClock(params: {
  hasTimeControl: boolean;
  gameStatus: GameClockStatus;
  isTerminal: boolean;
  timeoutColor: PlayerColor | null;
  engineReady: boolean;
  waitingForSessionReady: boolean;
  turn: PlayerColor;
  playerColor: PlayerColor;
  activeSide: PlayerColor | null;
  isRunning: boolean;
}): boolean {
  return params.hasTimeControl &&
    params.gameStatus === 'active' &&
    !params.isTerminal &&
    params.timeoutColor === null &&
    params.engineReady &&
    !params.waitingForSessionReady &&
    params.turn === params.playerColor &&
    (!params.isRunning || params.activeSide !== params.playerColor);
}
