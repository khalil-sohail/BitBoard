import { useState, useCallback } from 'react';
import type { GradedMove, MoveGrade } from '../types/grades';
import type { NormalizedEvaluation } from '../lib/engine-evaluation';
import { evalBarCentipawns } from '../lib/engine-evaluation';

export function useMoveReview() {
  const [gradeMap, setGradeMap] = useState<Map<number, GradedMove>>(new Map());
  const [evalGraphData, setEvalGraphData] = useState<{ ply: number, score: number }[]>([]);

  const recordPositionEval = useCallback((evaluation: NormalizedEvaluation | null, ply: number) => {
    const cp = evalBarCentipawns(evaluation);
    const displayScore = Math.max(-10, Math.min(10, cp / 100));

    setEvalGraphData(prev => {
      const next = [...prev];
      const existingIdx = next.findIndex(point => point.ply === ply);
      if (existingIdx >= 0) {
        next[existingIdx] = { ply, score: displayScore };
      } else {
        next.push({ ply, score: displayScore });
      }
      return next.sort((a, b) => a.ply - b.ply);
    });
  }, []);

  const setMoveGrade = useCallback((moveIndex: number, grade: MoveGrade, loss: number) => {
    setGradeMap(prev => {
      const next = new Map(prev);
      next.set(moveIndex, { moveIndex, grade, delta: -loss });
      return next;
    });
  }, []);

  const resetGrades = useCallback(() => {
    setGradeMap(new Map());
    setEvalGraphData([]);
  }, []);

  const grades: GradedMove[] = Array.from(gradeMap.values());

  return { grades, evalGraphData, recordPositionEval, setMoveGrade, resetGrades };
}
