import { Chess } from 'chess.js';

export type MoveNotation = 'SAN' | 'UCI';

export interface FormattedMoveSequence {
  notation: MoveNotation;
  moves: string[];
}

export function storedMoveNotation(move: { san?: string; from: string; to: string; promotion?: string }): string {
  return move.san?.trim() || `${move.from}${move.to}${move.promotion ?? ''}`;
}

/**
 * Convert a PV only when every move can be replayed from its exact root FEN.
 * A failed or partial replay falls back to the original UCI line as a unit so
 * mixed notation can never imply that an unverified suffix was reconstructed.
 */
export function formatPrincipalVariation(rootFen: string | undefined, uciMoves: readonly string[]): FormattedMoveSequence {
  const fallback = { notation: 'UCI' as const, moves: [...uciMoves] };
  if (!rootFen || uciMoves.length === 0) return fallback;
  try {
    const game = new Chess(rootFen);
    const san: string[] = [];
    for (const uci of uciMoves) {
      const match = /^([a-h][1-8])([a-h][1-8])([qrbn])?$/.exec(uci);
      if (!match) return fallback;
      const move = game.move({ from: match[1], to: match[2], promotion: match[3] });
      if (!move) return fallback;
      san.push(move.san);
    }
    return { notation: 'SAN', moves: san };
  } catch {
    return fallback;
  }
}
