import type { Chess } from 'chess.js';
import type { PlayerColor } from '@/types/engine';
import type { GameResult } from '@/lib/training-machine';

export type SessionLifecycleStatus = 'idle' | 'active' | 'completed';

export function currentGameResult(
  game: Chess,
  turn: PlayerColor,
  timeoutColor: PlayerColor | null,
  resignedBy: PlayerColor | null,
): GameResult {
  if (timeoutColor !== null) {
    return { reason: 'timeout', winner: timeoutColor === 'w' ? 'black' : 'white' };
  }
  if (resignedBy !== null) {
    return { reason: 'resignation', winner: resignedBy === 'w' ? 'black' : 'white' };
  }
  if (game.isCheckmate()) {
    return { reason: 'checkmate', winner: turn === 'w' ? 'black' : 'white' };
  }
  if (game.isStalemate()) return { reason: 'stalemate' };
  if (game.isDraw()) return { reason: 'draw' };
  return { reason: 'unknown' };
}
