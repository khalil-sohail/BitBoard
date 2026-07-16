import type { Move } from 'chess.js';
import type { MoveHistoryEntry } from '../live-data/live-data.types';
import { storedMoveNotation } from '../live-data/move-notation';

export function buildFairPlayHistoryEntries(moves: readonly Move[]): MoveHistoryEntry[] {
  return moves.map((move, index) => ({ ply: index + 1, notation: storedMoveNotation(move) }));
}
