import { useState, useCallback, useRef } from 'react';
import { GradedMove, MoveGrade, gradeFromDelta } from '../types/grades';

/**
 * useMoveReview
 *
 * Tracks normalized (White's perspective) centipawn evaluations and assigns
 * a grade to each move based on the centipawn delta.
 *
 * - delta > 0 means the position improved for the mover (good).
 * - delta < 0 means the position got worse for the mover (bad).
 *
 * State is backed by a Map<moveIndex, GradedMove> so there are never
 * sparse-array gaps that would cause `g is undefined` crashes.
 */
const BOOK_DEPTH_HALF_MOVES = 10; // First 5 full moves = "Book"

export function useMoveReview() {
  // Map keyed by moveIndex — no sparse-array gaps possible
  const [gradeMap, setGradeMap] = useState<Map<number, GradedMove>>(new Map());
  // Eval history keyed by half-move index (white-normalized centipawns)
  const evalHistory = useRef<Map<number, number>>(new Map());
  // Ordered array of eval scores for graphing
  const [evalGraphData, setEvalGraphData] = useState<{ ply: number, score: number }[]>([]);

  const recordEval = useCallback((
    evalCp: number,
    moveIndex: number,
    moverColor: 'w' | 'b',
    hadPrevMate: boolean = false,
    hasNewMate: boolean = false,
  ) => {
    evalHistory.current.set(moveIndex, evalCp);

    const prevEval = moveIndex > 0 ? (evalHistory.current.get(moveIndex - 1) ?? null) : null;

    let grade: MoveGrade;
    let delta = 0;

    if (prevEval === null) {
      // First move — not enough history to compute delta, mark as Book
      grade = 'Book';
    } else {
      // Delta from the mover's perspective: positive = better for them
      delta = moverColor === 'w' ? evalCp - prevEval : prevEval - evalCp;
      const isBook = moveIndex < BOOK_DEPTH_HALF_MOVES;
      const isBrilliant =
        !isBook &&
        !hadPrevMate &&
        hasNewMate &&
        ((moverColor === 'w' && evalCp > 0) || (moverColor === 'b' && evalCp < 0));
      grade = gradeFromDelta(delta, isBook, isBrilliant);
    }

    setGradeMap(prev => {
      const next = new Map(prev);
      next.set(moveIndex, { moveIndex, grade, delta });
      return next;
    });

    setEvalGraphData(prev => {
      // Calculate float score, capping mate scores (which are +-100,000 centipawns) to +-10
      let displayScore = evalCp / 100;
      if (displayScore > 10) displayScore = 10;
      if (displayScore < -10) displayScore = -10;
      
      const newGraph = [...prev];
      // Keep array in sync with moveIndex (1-indexed usually based on usage)
      // Since moveIndex corresponds to half-moves, we can just replace or push.
      const existingIdx = newGraph.findIndex(g => g.ply === moveIndex);
      if (existingIdx >= 0) {
          newGraph[existingIdx] = { ply: moveIndex, score: displayScore };
      } else {
          newGraph.push({ ply: moveIndex, score: displayScore });
      }
      return newGraph.sort((a, b) => a.ply - b.ply);
    });
  }, []);

  const resetGrades = useCallback(() => {
    setGradeMap(new Map());
    evalHistory.current = new Map();
    setEvalGraphData([]);
  }, []);

  // Expose as a dense array (Map.values() is always contiguous)
  const grades: GradedMove[] = Array.from(gradeMap.values());

  return { grades, evalGraphData, recordEval, resetGrades };
}
