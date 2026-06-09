import { useState, useCallback } from 'react';
import { EvalPoint } from '../types/engine';

export function useEvalHistory() {
  const [evalHistory, setEvalHistory] = useState<EvalPoint[]>([]);

  const addEvalPoint = useCallback((moveNumber: number, evalScore: number) => {
    setEvalHistory(prev => {
      // Don't add duplicate points for the same move
      const existingIdx = prev.findIndex(p => p.moveNumber === moveNumber);
      if (existingIdx !== -1) {
        const newHist = [...prev];
        newHist[existingIdx] = { moveNumber, eval: evalScore };
        return newHist;
      }
      return [...prev, { moveNumber, eval: evalScore }];
    });
  }, []);

  const resetEvalHistory = useCallback(() => {
    setEvalHistory([]);
  }, []);

  return {
    evalHistory,
    addEvalPoint,
    resetEvalHistory
  };
}
