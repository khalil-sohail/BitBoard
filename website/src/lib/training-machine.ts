import type { PlayerColor, EngineTerminalResult } from '../types/engine';
import type { PendingPromotion, PromotionPiece } from './promotion';

export type ResetReason = 'new-game' | 'mode-switch' | 'manual' | 'reconnect' | 'engine-error';

export type GameResult =
  | { reason: 'checkmate'; winner: 'white' | 'black' }
  | { reason: 'stalemate' | 'draw' | 'timeout' | 'resignation' | 'unknown'; winner?: 'white' | 'black' };

export type TrainingState =
  | { status: 'inactive' }
  | { status: 'initializing'; playerColor: PlayerColor }
  | { status: 'waiting-player'; playerColor: PlayerColor }
  | { status: 'promotion-pending'; playerColor: PlayerColor; promotion: PendingPromotion }
  | { status: 'reviewing-player-move'; playerColor: PlayerColor; reviewId: number }
  | { status: 'showing-feedback'; playerColor: PlayerColor; reviewId: number; available: boolean }
  | { status: 'waiting-engine-move'; playerColor: PlayerColor; requestId?: number }
  | { status: 'resetting'; reason: ResetReason; playerColor?: PlayerColor }
  | { status: 'connection-lost'; recoverTo: RecoverableTrainingState }
  | { status: 'engine-error'; message: string }
  | { status: 'game-over'; result: GameResult };

export type RecoverableTrainingState = Exclude<TrainingState, { status: 'connection-lost' | 'engine-error' | 'resetting' }>;

export type TrainingEvent =
  | { type: 'ENTER'; playerColor: PlayerColor }
  | { type: 'READY'; turn: PlayerColor }
  | { type: 'EXIT' }
  | { type: 'PROMOTION_REQUIRED'; promotion: PendingPromotion }
  | { type: 'PROMOTION_SELECTED'; piece: PromotionPiece; reviewId?: number }
  | { type: 'PROMOTION_CANCELLED' }
  | { type: 'REVIEW_STARTED'; reviewId: number }
  | { type: 'REVIEW_COMPLETED'; reviewId: number; available: boolean }
  | { type: 'FEEDBACK_SHOWN' }
  | { type: 'ENGINE_SEARCH_STARTED'; requestId?: number }
  | { type: 'ENGINE_MOVE_RECEIVED'; requestId?: number }
  | { type: 'TERMINAL'; result: GameResult }
  | { type: 'RESET_REQUESTED'; reason: ResetReason; playerColor?: PlayerColor }
  | { type: 'RESET_COMPLETED'; playerColor: PlayerColor; turn: PlayerColor }
  | { type: 'DISCONNECTED' }
  | { type: 'RECONNECTED'; turn: PlayerColor }
  | { type: 'ENGINE_FAILED'; message: string };

export const initialTrainingState: TrainingState = { status: 'inactive' };

export function trainingReducer(state: TrainingState, event: TrainingEvent): TrainingState {
  switch (event.type) {
    case 'ENTER':
      return { status: 'initializing', playerColor: event.playerColor };

    case 'READY':
      if (state.status !== 'initializing' && state.status !== 'resetting') return state;
      if (!state.playerColor) return { status: 'inactive' };
      return event.turn === state.playerColor
        ? { status: 'waiting-player', playerColor: state.playerColor }
        : { status: 'waiting-engine-move', playerColor: state.playerColor };

    case 'EXIT':
      return { status: 'inactive' };

    case 'PROMOTION_REQUIRED':
      if (state.status !== 'waiting-player') return state;
      return { status: 'promotion-pending', playerColor: state.playerColor, promotion: event.promotion };

    case 'PROMOTION_SELECTED':
      if (state.status !== 'promotion-pending') return state;
      if (event.reviewId !== undefined) {
        return { status: 'reviewing-player-move', playerColor: state.playerColor, reviewId: event.reviewId };
      }
      return { status: 'waiting-player', playerColor: state.playerColor };

    case 'PROMOTION_CANCELLED':
      if (state.status !== 'promotion-pending') return state;
      return { status: 'waiting-player', playerColor: state.playerColor };

    case 'REVIEW_STARTED':
      if (state.status !== 'waiting-player') return state;
      return { status: 'reviewing-player-move', playerColor: state.playerColor, reviewId: event.reviewId };

    case 'REVIEW_COMPLETED':
      if (state.status !== 'reviewing-player-move' || state.reviewId !== event.reviewId) return state;
      return {
        status: 'showing-feedback',
        playerColor: state.playerColor,
        reviewId: event.reviewId,
        available: event.available,
      };

    case 'FEEDBACK_SHOWN':
      if (state.status !== 'showing-feedback') return state;
      return { status: 'waiting-engine-move', playerColor: state.playerColor };

    case 'ENGINE_SEARCH_STARTED':
      if (state.status !== 'waiting-engine-move') return state;
      return { ...state, requestId: event.requestId };

    case 'ENGINE_MOVE_RECEIVED':
      if (state.status !== 'waiting-engine-move') return state;
      if (state.requestId !== undefined && event.requestId !== undefined && state.requestId !== event.requestId) return state;
      return { status: 'waiting-player', playerColor: state.playerColor };

    case 'TERMINAL':
      if (state.status === 'inactive') return state;
      return { status: 'game-over', result: event.result };

    case 'RESET_REQUESTED':
      return { status: 'resetting', reason: event.reason, playerColor: event.playerColor ?? currentPlayerColor(state) };

    case 'RESET_COMPLETED':
      if (state.status !== 'resetting') return state;
      return event.turn === event.playerColor
        ? { status: 'waiting-player', playerColor: event.playerColor }
        : { status: 'waiting-engine-move', playerColor: event.playerColor };

    case 'DISCONNECTED':
      if (state.status === 'inactive' || state.status === 'connection-lost') return state;
      return { status: 'connection-lost', recoverTo: recoverableState(state) };

    case 'RECONNECTED':
      if (state.status !== 'connection-lost') return state;
      if (!('playerColor' in state.recoverTo)) return { status: 'inactive' };
      return event.turn === state.recoverTo.playerColor
        ? { status: 'waiting-player', playerColor: state.recoverTo.playerColor }
        : { status: 'waiting-engine-move', playerColor: state.recoverTo.playerColor };

    case 'ENGINE_FAILED':
      if (state.status === 'inactive') return state;
      return { status: 'engine-error', message: event.message };

    default:
      return assertNever(event);
  }
}

export function canPlayerMove(state: TrainingState): boolean {
  return state.status === 'waiting-player';
}

export function isBoardLocked(state: TrainingState): boolean {
  return !canPlayerMove(state);
}

export function isEngineWorkPending(state: TrainingState): boolean {
  return state.status === 'reviewing-player-move' || state.status === 'waiting-engine-move';
}

export function canChangeTrainingSettings(state: TrainingState): boolean {
  return state.status === 'inactive' ||
    state.status === 'waiting-player' ||
    state.status === 'waiting-engine-move' ||
    state.status === 'game-over';
}

export function isTrainingActive(state: TrainingState): boolean {
  return state.status !== 'inactive';
}

export function resultFromTerminal(terminal: EngineTerminalResult): GameResult {
  if (terminal.reason === 'checkmate') {
    return { reason: 'checkmate', winner: terminal.winner ?? 'white' };
  }
  if (terminal.reason === 'stalemate' || terminal.reason === 'draw') {
    return { reason: terminal.reason };
  }
  return { reason: 'unknown' };
}

export function assertTrainingInvariant(state: TrainingState): void {
  switch (state.status) {
    case 'inactive':
    case 'initializing':
    case 'waiting-player':
    case 'promotion-pending':
    case 'reviewing-player-move':
    case 'showing-feedback':
    case 'waiting-engine-move':
    case 'resetting':
    case 'connection-lost':
    case 'engine-error':
    case 'game-over':
      return;
    default:
      return assertNever(state);
  }
}

function currentPlayerColor(state: TrainingState): PlayerColor | undefined {
  return 'playerColor' in state ? state.playerColor : undefined;
}

function recoverableState(state: TrainingState): RecoverableTrainingState {
  if (state.status === 'resetting' || state.status === 'engine-error' || state.status === 'connection-lost') {
    return { status: 'inactive' };
  }
  if (state.status === 'reviewing-player-move') {
    return { status: 'waiting-player', playerColor: state.playerColor };
  }
  if (state.status === 'promotion-pending') {
    return { status: 'waiting-player', playerColor: state.playerColor };
  }
  if (state.status === 'showing-feedback') {
    return { status: 'waiting-engine-move', playerColor: state.playerColor };
  }
  return state;
}

function assertNever(value: never): never {
  throw new Error(`Unhandled training event: ${JSON.stringify(value)}`);
}
