import { Chess } from 'chess.js';

export function validateFen(value: string): string | null {
  if (!value.trim()) return 'Enter a FEN position.';
  try {
    new Chess(value.trim());
    return null;
  } catch {
    return 'Enter a valid FEN with piece placement, side to move, castling, and en-passant fields.';
  }
}

export function validatePgn(value: string): string | null {
  if (!value.trim()) return 'Paste a PGN game.';
  try {
    const game = new Chess();
    game.loadPgn(value);
    return null;
  } catch {
    return 'Enter a valid PGN mainline.';
  }
}
