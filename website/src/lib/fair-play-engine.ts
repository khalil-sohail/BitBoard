import { createHash } from 'crypto';
import { Chess } from 'chess.js';

export type EngineMoveFailureReason =
  | 'invalid_uci_move'
  | 'wrong_side_piece'
  | 'illegal_move_for_position'
  | 'terminal_position'
  | 'position_mismatch'
  | 'stale_request';

export interface PositionIdentity {
  requestId: number;
  sessionId: string;
  sessionGeneration: number;
  positionSequence: number;
  normalizedFen: string;
  expectedSide: 'w' | 'b';
  key: string;
}

export function resolveExactPosition(fen: string, moves: readonly string[]): Chess | null {
  let chess: Chess;
  try {
    chess = new Chess(fen);
  } catch {
    return null;
  }
  for (const uci of moves) {
    if (!/^[a-h][1-8][a-h][1-8][qrbn]?$/.test(uci)) return null;
    try {
      const applied = chess.move({
        from: uci.slice(0, 2),
        to: uci.slice(2, 4),
        promotion: uci.length === 5 ? uci[4] : undefined,
      });
      if (!applied) return null;
    } catch {
      return null;
    }
  }
  return chess;
}

export function createPositionIdentity(params: Omit<PositionIdentity, 'normalizedFen' | 'expectedSide' | 'key'> & { fen: string }): PositionIdentity {
  const chess = new Chess(params.fen);
  const normalizedFen = chess.fen();
  const expectedSide = chess.turn();
  const source = [params.sessionId, params.sessionGeneration, params.positionSequence, params.requestId, normalizedFen, expectedSide].join('|');
  return {
    requestId: params.requestId,
    sessionId: params.sessionId,
    sessionGeneration: params.sessionGeneration,
    positionSequence: params.positionSequence,
    normalizedFen,
    expectedSide,
    key: `sha256:${createHash('sha256').update(source).digest('hex')}`,
  };
}

export type ValidatedEngineMove =
  | { ok: true; move: string | null; newFen: string; terminal: boolean }
  | { ok: false; reason: EngineMoveFailureReason };

export function validateEngineMove(fen: string, move: string): ValidatedEngineMove {
  let chess: Chess;
  try {
    chess = new Chess(fen);
  } catch {
    return { ok: false, reason: 'position_mismatch' };
  }
  if (move === '0000') {
    return chess.isGameOver()
      ? { ok: true, move: null, newFen: chess.fen(), terminal: true }
      : { ok: false, reason: 'terminal_position' };
  }
  if (!/^[a-h][1-8][a-h][1-8][qrbn]?$/.test(move)) {
    return { ok: false, reason: 'invalid_uci_move' };
  }
  const source = chess.get(move.slice(0, 2) as Parameters<Chess['get']>[0]);
  if (!source || source.color !== chess.turn()) {
    return { ok: false, reason: 'wrong_side_piece' };
  }
  try {
    const applied = chess.move({
      from: move.slice(0, 2),
      to: move.slice(2, 4),
      promotion: move.length === 5 ? move[4] : undefined,
    });
    if (!applied) return { ok: false, reason: 'illegal_move_for_position' };
  } catch {
    return { ok: false, reason: 'illegal_move_for_position' };
  }
  return { ok: true, move, newFen: chess.fen(), terminal: chess.isGameOver() };
}

export class SamePositionRetryGuard {
  private completed = new Set<string>();
  private failures = new Map<string, number>();

  public recordValidated(positionKey: string): void {
    this.completed.add(positionKey);
  }

  public canStart(positionKey: string): boolean {
    return !this.completed.has(positionKey) && (this.failures.get(positionKey) ?? 0) <= 1;
  }

  public recordFailure(positionKey: string): { retryAllowed: boolean; retryCount: number } {
    const retryCount = (this.failures.get(positionKey) ?? 0) + 1;
    this.failures.set(positionKey, retryCount);
    return { retryAllowed: retryCount === 1, retryCount };
  }
}
