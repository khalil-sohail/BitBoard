import type { Move } from 'chess.js';
import type { EngineInfo } from '../../types/engine';
import type { GradedMove } from '../../types/grades';
import type { MoveHistoryEntry, PrincipalVariationView } from '../live-data/live-data.types';
import { storedMoveNotation } from '../live-data/move-notation';

export function buildTrainingHistoryEntries(moves: readonly Move[], grades: readonly GradedMove[]): MoveHistoryEntry[] {
  const gradeMap = new Map(grades.filter(Boolean).map(grade => [grade.moveIndex, grade]));
  return moves.map((move, index) => {
    const grade = gradeMap.get(index);
    return {
      ply: index + 1,
      notation: storedMoveNotation(move),
      markers: grade ? [
        { text: grade.grade, accessibleLabel: `grade ${grade.grade}`, tone: gradeTone(grade.grade) },
        ...(grade.hintLevelUsed ? [{ text: `Hint ${grade.hintLevelUsed}`, accessibleLabel: `hint level ${grade.hintLevelUsed} used`, tone: 'hint' as const }] : []),
      ] : undefined,
    };
  });
}

export function selectTrainingReviewInfo(info: EngineInfo | null, reviewing: boolean): EngineInfo | null {
  if (!info) return null;
  if (reviewing && info.purpose !== 'training-result-review') return null;
  if (info.purpose !== 'training-root-review' && info.purpose !== 'training-result-review') return null;
  return info;
}

export function trainingPvLines(info: EngineInfo | null): PrincipalVariationView[] {
  return info?.pvs.map(line => ({ rank: line.multipv, evaluation: line.evaluation ?? null, moves: line.pv, depth: info.reportedDepth ?? info.depth })) ?? [];
}

function gradeTone(grade: string): 'positive' | 'warning' | 'negative' | 'neutral' {
  if (grade === 'Best' || grade === 'Good' || grade === 'Book' || grade === 'Forced') return 'positive';
  if (grade === 'Inaccuracy') return 'warning';
  if (grade === 'Mistake' || grade === 'Blunder') return 'negative';
  return 'neutral';
}
