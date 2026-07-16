import type { Move } from 'chess.js';
import type { AnalysisSnapshot } from '../../lib/board-arrows';
import type { MoveHistoryEntry, PrincipalVariationView } from '../live-data/live-data.types';
import { storedMoveNotation } from '../live-data/move-notation';

export function buildAnalysisHistoryEntries(moves: readonly Move[], cursorPly: number): MoveHistoryEntry[] {
  return moves.map((move, index) => ({
    ply: index + 1,
    notation: storedMoveNotation(move),
    selected: cursorPly === index + 1,
    selectable: true,
    accessibleLabel: `${index % 2 === 0 ? 'White' : 'Black'} move ${move.san}, position after ply ${index + 1}`,
  }));
}

export function analysisPvLines(snapshot: AnalysisSnapshot | null): PrincipalVariationView[] {
  return snapshot?.lines.map(line => ({ rank: line.multipv, evaluation: line.evaluation ?? null, moves: line.pv, depth: snapshot.reportedDepth })) ?? [];
}
