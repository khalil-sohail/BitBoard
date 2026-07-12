export type PromotionPiece = 'q' | 'r' | 'b' | 'n';
export type PieceColor = 'w' | 'b';

export interface PromotionMove {
  from: string;
  to: string;
  promotion: PromotionPiece;
}

export interface PendingPromotion {
  from: string;
  to: string;
  color: PieceColor;
}

export const PROMOTION_PIECES: readonly PromotionPiece[] = ['q', 'r', 'b', 'n'];

export function isPromotionPiece(value: unknown): value is PromotionPiece {
  return typeof value === 'string' && PROMOTION_PIECES.includes(value as PromotionPiece);
}

export function isPromotionCandidate(
  fen: string,
  from: string,
  to: string,
): PendingPromotion | null {
  const fields = fen.split(/\s+/);
  const board = fields[0];
  if (!board) return null;

  const piece = pieceAt(board, from);
  if (piece !== 'P' && piece !== 'p') return null;

  const targetRank = to[1];
  if (piece === 'P' && targetRank === '8') {
    return { from, to, color: 'w' };
  }

  if (piece === 'p' && targetRank === '1') {
    return { from, to, color: 'b' };
  }

  return null;
}

export function buildPromotionMove(pending: PendingPromotion, promotion: PromotionPiece): PromotionMove {
  return {
    from: pending.from,
    to: pending.to,
    promotion,
  };
}

function pieceAt(board: string, square: string): string | null {
  if (!/^[a-h][1-8]$/.test(square)) return null;

  const fileIndex = square.charCodeAt(0) - 97;
  const rank = Number(square[1]);
  const targetRankFromTop = 8 - rank;
  const ranks = board.split('/');
  const row = ranks[targetRankFromTop];
  if (!row) return null;

  let file = 0;
  for (const char of row) {
    if (/\d/.test(char)) {
      file += Number(char);
      continue;
    }

    if (file === fileIndex) {
      return char;
    }
    file += 1;
  }

  return null;
}
