import { useCallback, useEffect, useMemo, useRef } from 'react';
import type { EngineInfo } from '../types/engine';
import type { EngineRequestId } from '../lib/engine-response-filter';
import type { TrainingEvent, TrainingState } from '../lib/training-machine';
import {
  buildProgressiveHintView,
  deriveHintMove,
  reusableRootHintMove,
  shouldClearHintForFen,
} from '../lib/training-hint';

interface UseTrainingHintInput {
  fen: string;
  uciHistory: string[];
  maxDepth: number;
  engineStatus: string;
  trainingState: TrainingState;
  engineInfo: EngineInfo | null;
  dispatchTraining: (event: TrainingEvent) => void;
  startAnalysis: (
    fen: string,
    moves?: string[],
    depth?: number,
    multiPv?: number,
    purpose?: 'training-root-review' | 'training-result-review' | 'training-hint' | 'analysis',
  ) => EngineRequestId | null;
}

export function useTrainingHint({
  fen,
  uciHistory,
  maxDepth,
  engineStatus,
  trainingState,
  engineInfo,
  dispatchTraining,
  startAnalysis,
}: UseTrainingHintInput) {
  const pendingRequestRef = useRef<{ requestId: EngineRequestId; fen: string } | null>(null);
  const consumedInfoRef = useRef<string | null>(null);

  const hintMove = useMemo(() => {
    if (trainingState.status !== 'waiting-player') return null;
    const hint = trainingState.hint;
    const move = hint?.move;
    if (!hint || !move || hint.fen !== fen) return null;
    return deriveHintMove(fen, move);
  }, [fen, trainingState]);

  const hintView = useMemo(() => {
    if (trainingState.status !== 'waiting-player' || !hintMove) return null;
    return buildProgressiveHintView(hintMove, trainingState.hint?.level ?? 0);
  }, [hintMove, trainingState]);

  const requestHint = useCallback(() => {
    if (trainingState.status !== 'waiting-player') return;
    if (trainingState.hint?.fen === fen && trainingState.hint.status === 'searching') return;

    dispatchTraining({ type: 'HINT_REQUESTED', fen });

    const existingMove = trainingState.hint?.fen === fen ? trainingState.hint.move : undefined;
    if (existingMove && deriveHintMove(fen, existingMove)) return;

    const reusable = reusableRootHintMove(engineInfo, fen);
    if (reusable && deriveHintMove(fen, reusable)) {
      dispatchTraining({ type: 'HINT_AVAILABLE', fen, move: reusable, requestId: engineInfo?.requestId });
      return;
    }

    const requestId = startAnalysis(fen, uciHistory, maxDepth, 1, 'training-hint');
    if (requestId === null) {
      dispatchTraining({ type: 'HINT_FAILED', fen, message: 'Hint analysis is unavailable.' });
      return;
    }

    pendingRequestRef.current = { requestId, fen };
    consumedInfoRef.current = null;
    dispatchTraining({ type: 'HINT_SEARCH_STARTED', fen, requestId });
  }, [dispatchTraining, engineInfo, fen, maxDepth, startAnalysis, trainingState, uciHistory]);

  useEffect(() => {
    if (shouldClearHintForFen(trainingState, fen)) {
      pendingRequestRef.current = null;
      consumedInfoRef.current = null;
      dispatchTraining({ type: 'HINT_CLEARED' });
    }
  }, [dispatchTraining, fen, trainingState]);

  useEffect(() => {
    if (trainingState.status !== 'waiting-player') {
      pendingRequestRef.current = null;
      consumedInfoRef.current = null;
      return;
    }
    const hint = trainingState.hint;
    if (!hint || hint.status !== 'searching' || hint.requestId === undefined) return;
    if (!engineInfo || engineInfo.requestId !== hint.requestId || engineInfo.rootFen !== hint.fen) return;
    if (engineInfo.purpose !== 'training-hint') return;

    const key = `${engineInfo.requestId}:${engineInfo.depth}:${engineInfo.pvs?.[0]?.pv?.[0] ?? ''}`;
    if (consumedInfoRef.current === key) return;
    consumedInfoRef.current = key;

    const move = engineInfo.pvs?.[0]?.pv?.[0];
    if (typeof move === 'string' && deriveHintMove(hint.fen, move)) {
      pendingRequestRef.current = null;
      dispatchTraining({ type: 'HINT_AVAILABLE', fen: hint.fen, move, requestId: hint.requestId });
    }
  }, [dispatchTraining, engineInfo, trainingState]);

  useEffect(() => {
    if (trainingState.status !== 'waiting-player') return;
    const hint = trainingState.hint;
    if (!hint || hint.status !== 'searching' || hint.requestId === undefined) return;
    if (engineStatus !== 'idle') return;
    if (engineInfo?.requestId === hint.requestId && engineInfo.rootFen === hint.fen && engineInfo.pvs?.[0]?.pv?.[0]) return;
    dispatchTraining({
      type: 'HINT_FAILED',
      fen: hint.fen,
      requestId: hint.requestId,
      message: 'No legal hint is available for this position.',
    });
  }, [dispatchTraining, engineInfo, engineStatus, trainingState]);

  return {
    hintMove,
    hintView,
    requestHint,
  };
}
