import type { EngineBestMoveResult } from '../types/engine';
import type { GameMode } from '../types/engine';

export interface OpponentMoveApplicationReceipt {
  readonly kind: 'opponent-move';
  readonly ownerMode: Exclude<GameMode, 'analysis'>;
  readonly requestId: number;
  readonly positionKey: string;
  readonly sessionId: string;
  readonly sessionGeneration: number;
  readonly positionFen: string;
}

export interface ActiveEngineResultIdentity {
  readonly ownerMode: GameMode | null;
  readonly purpose: string | null;
  readonly requestId: number | null;
  readonly positionKey: string | null;
  readonly sessionId: string | null;
  readonly sessionGeneration: number | null;
  readonly positionFen: string | null;
}

/** Only opponent searches transfer a move to the board for application. */
export function requiresMoveApplicationAcknowledgment(
  result: Pick<EngineBestMoveResult, 'purpose' | 'move'>,
): boolean {
  return result.purpose === 'opponent' && result.move !== null;
}

/**
 * Capture the immutable identity of the exact opponent result being applied.
 * Review, hint, and analysis results deliberately cannot produce a receipt.
 */
export function createOpponentMoveApplicationReceipt(
  result: EngineBestMoveResult,
  mode: GameMode,
): OpponentMoveApplicationReceipt | null {
  if (mode === 'analysis') return null;
  if (!requiresMoveApplicationAcknowledgment(result)) return null;
  if (
    !result.positionKey ||
    !result.sessionId ||
    result.sessionGeneration === undefined ||
    !result.positionFen
  ) {
    return null;
  }

  return {
    kind: 'opponent-move',
    ownerMode: mode,
    requestId: result.requestId,
    positionKey: result.positionKey,
    sessionId: result.sessionId,
    sessionGeneration: result.sessionGeneration,
    positionFen: result.positionFen,
  };
}

export function statusAfterBestMove(
  result: Pick<EngineBestMoveResult, 'purpose' | 'move'>,
): 'result_ready' | 'idle' {
  return requiresMoveApplicationAcknowledgment(result) ? 'result_ready' : 'idle';
}

export function matchesActiveOpponentResult(
  active: ActiveEngineResultIdentity,
  receipt: OpponentMoveApplicationReceipt,
): boolean {
  return active.ownerMode === receipt.ownerMode &&
    active.purpose === 'opponent' &&
    active.requestId === receipt.requestId &&
    active.positionKey === receipt.positionKey &&
    active.sessionId === receipt.sessionId &&
    active.sessionGeneration === receipt.sessionGeneration &&
    active.positionFen === receipt.positionFen;
}
