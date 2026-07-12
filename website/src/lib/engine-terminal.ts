import { Chess } from 'chess.js';
import { DEFAULT_START_FEN } from './engine-protocol';

export type TerminalReason = 'checkmate' | 'stalemate' | 'draw' | 'no-legal-move' | 'unknown';
export type TerminalWinner = 'white' | 'black';

export interface TerminalResult {
  reason: TerminalReason;
  winner?: TerminalWinner;
}

export function classifyTerminalPosition(fen: string, moves: readonly string[]): TerminalResult {
  try {
    const chess = new Chess(fen === 'startpos' ? DEFAULT_START_FEN : fen);
    for (const move of moves) {
      chess.move(move);
    }

    if (chess.isCheckmate()) {
      return {
        reason: 'checkmate',
        winner: chess.turn() === 'w' ? 'black' : 'white',
      };
    }

    if (chess.isStalemate()) {
      return { reason: 'stalemate' };
    }

    if (chess.isDraw()) {
      return { reason: 'draw' };
    }

    return { reason: 'no-legal-move' };
  } catch {
    return { reason: 'unknown' };
  }
}
