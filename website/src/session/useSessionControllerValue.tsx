"use client";

import { useEffect, useMemo, useReducer, useRef, useState, useCallback } from "react";
import { Chess } from "chess.js";
import type { NewGameConfig } from "@/components/setup/session-setup.types";
import { useEngine } from "@/hooks/useEngine";
import { useChessGame } from "@/hooks/useChessGame";
import { useChessClock } from "@/hooks/useChessClock";
import { useMoveReview } from "@/hooks/useMoveReview";
import { useTrainingHint } from "@/hooks/useTrainingHint";
import { PlayerColor, DifficultyLevel, GameMode, TimeControl, TIME_CONTROLS } from "@/types/engine";
import { NormalizedEvaluation, gradeMove, normalizeEngineInfo } from "@/lib/engine-evaluation";
import {
  clockTransitionAfterLegalMove,
  shouldAcceptEngineBestMove,
  shouldStartEngineClockForSearch,
} from "@/lib/time-management-policy";
import {
  canChangeTrainingSettings,
  canPlayerMove,
  canRequestHint,
  initialTrainingState,
  resultFromTerminal,
  trainingReducer,
} from "@/lib/training-machine";
import type { GameResult } from "@/lib/training-machine";
import { hintLevelUsedForMove } from "@/lib/training-hint";
import { composeBoardArrows, toChessboardArrows } from "@/lib/board-arrows";
import { resolveSearchPolicy } from "@/lib/search-policy";
import type { PendingPromotion, PromotionPiece } from "@/lib/promotion";
import type { AnalysisSnapshot } from "@/lib/board-arrows";
import type { SearchPurpose } from "@/lib/engine-difficulty";
import { analysisDisplayReducer } from './analysis-display-state';
import { currentGameResult, type SessionLifecycleStatus } from './session-lifecycle';

// ── Types ────────────────────────────────────────────────────────────────────

interface PendingMoveReview {
  reviewId: number;
  moveIndex: number;
  resultFen: string;
  playerColor: PlayerColor;
  legalMoveCount: number;
  bestEvaluation: NormalizedEvaluation | null;
  isBook: boolean;
  hintLevelUsed: 0 | 1 | 2 | 3;
  resultRequestStarted: boolean;
}

// Default time control: 3+0
const DEFAULT_TC = TIME_CONTROLS[2];

// ── Component ─────────────────────────────────────────────────────────────────

export function useSessionControllerValue() {
  const { status, engineInfo, bestMoveResult, terminalCompletion, queuePosition, searchStartedRequestId, searchStartedPositionKey, clockStopSignal, sendMove, newGame, startAnalysis, stopEngine, releaseSession, acknowledgeBestMove, setEngineOption } = useEngine();
  const { game, fen, moveHistory, uciHistory, makeMove, resetGame, undoMove, loadFen, exportPgn, loadPgn, turn, isGameOver } = useChessGame();
  const { grades, evalGraphData, recordPositionEval, setMoveGrade, resetGrades } = useMoveReview();

  const [orientation, setOrientation]   = useState<PlayerColor>('w');
  const [difficulty, setDifficulty]     = useState<DifficultyLevel>('standard');
  const [gameMode, setGameMode]         = useState<GameMode>('fair');
  const [timeControl, setTimeControl]   = useState<TimeControl>(DEFAULT_TC);
  const [gameStatus, setGameStatus]     = useState<SessionLifecycleStatus>('idle');
  const [resignedBy, setResignedBy]     = useState<PlayerColor | null>(null);
  // Default to false so the user can explore the UI before starting a game.
  const [isSetupOpen, setIsSetupOpen] = useState(false);
  const [maxDepth, setMaxDepth] = useState(10);
  const [multiPv, setMultiPv] = useState(3);
  const [ownBook, setOwnBook] = useState(true);
  const [trainingPonderEnabled, setTrainingPonderEnabled] = useState(false);
  const [isWaitingForStop, setIsWaitingForStop] = useState(false);
  const [promotionResetKey, setPromotionResetKey] = useState(0);
  const [timeoutColor, setTimeoutColor] = useState<PlayerColor | null>(null);
  const [analysisDisplay, dispatchAnalysisDisplay] = useReducer(analysisDisplayReducer, { live: null, finalized: null });
  const [trainingState, dispatchTraining] = useReducer(trainingReducer, initialTrainingState);
  const setupTriggerRef = useRef<HTMLElement | null>(null);
  const ignoreStaleBestMoveRef = useRef(false);
  const stopTimeoutRef = useRef<NodeJS.Timeout | null>(null);
  const appliedOwnBookRef = useRef<boolean | null>(null);
  const lastClockSearchPositionRef = useRef<string | null>(null);
  const appliedBestMoveRequestRef = useRef<number | null>(null);

  useEffect(() => {
    if (typeof window === 'undefined') return;
    queueMicrotask(() => {
      setTrainingPonderEnabled(window.localStorage.getItem('bitboard.trainingPonderEnabled') === 'true');
    });
  }, []);

  const handleTrainingPonderChange = useCallback((enabled: boolean) => {
    setTrainingPonderEnabled(enabled);
    if (typeof window !== 'undefined') {
      window.localStorage.setItem('bitboard.trainingPonderEnabled', String(enabled));
    }
    if (!enabled && gameMode === 'training') {
      stopEngine();
    }
  }, [gameMode, stopEngine]);

  // Cleanup timeout on unmount
  useEffect(() => {
    return () => {
      if (stopTimeoutRef.current) {
        clearTimeout(stopTimeoutRef.current);
      }
    };
  }, []);

  // ── Derived mode flags ───────────────────────────────────────────────────
  const isAnalysis = gameMode === 'analysis';
  const isTraining = gameMode === 'training';

  // Whether a real time control is active
  // Detach clock entirely from Training and Analysis modes
  const hasTC = !isAnalysis && !isTraining && timeControl.initialMs > 0;
  const isGameActive = gameStatus === 'active';

  // ── Clock ─────────────────────────────────────────────────────────────────
  const handleTimeout = useCallback((color: PlayerColor) => {
    stopEngine();
    setTimeoutColor(color);
    setGameStatus('completed');
  }, [stopEngine]);

  const clock = useChessClock({
    initialWhiteMs: timeControl.initialMs,
    initialBlackMs: timeControl.initialMs,
    whiteIncMs:     timeControl.incMs,
    blackIncMs:     timeControl.incMs,
    onTimeout:      handleTimeout,
  }, game);

  const analysisFenRef = useRef<string | null>(null);
  const nextReviewIdRef = useRef(1);
  const pendingMoveReviewRef = useRef<PendingMoveReview | null>(null);

  const engineColor: PlayerColor = orientation === 'w' ? 'b' : 'w';

  const engineAutoEnabled = !isAnalysis;

  const showEvalBar     = isTraining || isAnalysis;
  const showEnginePanel = isTraining || isAnalysis;
  const showEngineConf  = isTraining || isAnalysis;
  const showClock       = !isAnalysis && hasTC;

  const engineOptionsUnavailable =
    status === 'connecting' ||
    status === 'queued' ||
    status === 'disconnected' ||
    status === 'session_expired' ||
    status === 'error' ||
    status === 'thinking' ||
    status === 'analyzing' ||
    (isTraining && !canChangeTrainingSettings(trainingState));

  const resolvePolicy = useCallback((purpose: SearchPurpose, overrides: { multiPv?: number } = {}) => {
    return resolveSearchPolicy({
      mode: gameMode,
      purpose,
      difficulty,
      userMaxDepth: maxDepth,
      multiPv: overrides.multiPv ?? multiPv,
      trainingPonderEnabled,
      fairPonderEnabled: gameMode === 'fair',
      clock: hasTC
        ? {
            wtime: clock.whiteMs,
            btime: clock.blackMs,
            winc: timeControl.incMs,
            binc: timeControl.incMs,
          }
        : undefined,
    });
  }, [clock.blackMs, clock.whiteMs, difficulty, gameMode, hasTC, maxDepth, multiPv, timeControl.incMs, trainingPonderEnabled]);

  const startResolvedAnalysis = useCallback((
    rootFen: string,
    moves: string[],
    purpose: Exclude<SearchPurpose, 'opponent'>,
    requestedMultiPv = multiPv,
  ) => {
    const policy = resolvePolicy(purpose, { multiPv: requestedMultiPv });
    return startAnalysis(rootFen, moves, undefined, policy.multiPv, purpose, policy.limit, policy.source, gameMode);
  }, [gameMode, multiPv, resolvePolicy, startAnalysis]);

  useEffect(() => {
    if (
      status === 'connecting' ||
      status === 'queued' ||
      status === 'disconnected' ||
      status === 'session_expired' ||
      status === 'error'
    ) {
      appliedOwnBookRef.current = null;
    }
  }, [status]);

  useEffect(() => {
    if (!hasTC) return;
    if (
      status === 'queued' ||
      status === 'disconnected' ||
      status === 'session_expired' ||
      status === 'error'
    ) {
      clock.stopClock();
      lastClockSearchPositionRef.current = null;
    }
  }, [clock, hasTC, status]);

  useEffect(() => {
    if (status !== 'idle') return;
    if (appliedOwnBookRef.current === ownBook) return;
    setEngineOption('OwnBook', ownBook);
    appliedOwnBookRef.current = ownBook;
  }, [ownBook, setEngineOption, status]);

  const handleOwnBookChange = useCallback((enabled: boolean) => {
    setOwnBook(enabled);
    if (status === 'idle') {
      setEngineOption('OwnBook', enabled);
      appliedOwnBookRef.current = enabled;
    } else {
      appliedOwnBookRef.current = null;
    }
  }, [setEngineOption, status]);

  // ── Effective game-over (board or resign) ────────────────────────────────
  const effectiveGameOver = (gameStatus === 'active' && isGameOver) || gameStatus === 'completed';

  const trainingBoardInteractive =
    isTraining &&
    gameStatus === 'active' &&
    canPlayerMove(trainingState) &&
    !effectiveGameOver &&
    status !== 'connecting' &&
    status !== 'queued' &&
    status !== 'disconnected' &&
    status !== 'session_expired' &&
    status !== 'error';

  useEffect(() => {
    if (!isTraining || gameStatus !== 'active') return;
    if (trainingState.status === 'initializing' && status === 'idle') {
      dispatchTraining({ type: 'READY', turn });
    } else if (trainingState.status === 'resetting' && status === 'idle') {
      dispatchTraining({ type: 'RESET_COMPLETED', playerColor: orientation, turn });
    }
  }, [gameStatus, isTraining, orientation, status, trainingState.status, turn]);

  useEffect(() => {
    if (!isTraining || gameStatus !== 'active') return;
    if (status === 'disconnected' || status === 'session_expired') {
      pendingMoveReviewRef.current = null;
      dispatchTraining({ type: 'DISCONNECTED' });
    } else if (status === 'error') {
      pendingMoveReviewRef.current = null;
      dispatchTraining({ type: 'ENGINE_FAILED', message: 'Engine connection failed' });
    } else if (status === 'idle' && trainingState.status === 'connection-lost') {
      dispatchTraining({ type: 'RECONNECTED', turn });
    }
  }, [gameStatus, isTraining, status, trainingState.status, turn]);

  useEffect(() => {
    if (!isTraining || !effectiveGameOver) return;
    dispatchTraining({ type: 'TERMINAL', result: currentGameResult(game, turn, timeoutColor, resignedBy) });
  }, [effectiveGameOver, game, isTraining, resignedBy, timeoutColor, turn]);

  // ── Engine auto-trigger ──────────────────────────────────────────────────
  useEffect(() => {
    if (!engineAutoEnabled) return;
    // ONLY fire when the game is actively running
    if (!isGameActive) return;
    if (isWaitingForStop) return;
    if (gameMode === 'training' && trainingState.status !== 'waiting-engine-move') return;
    if (turn === engineColor && status === 'idle' && !effectiveGameOver) {
      if (gameMode === 'training') {
        dispatchTraining({ type: 'ENGINE_SEARCH_STARTED' });
      }
      const policy = resolvePolicy('opponent');
      sendMove(fen, {
        difficulty,
        searchLimit: policy.limit,
        multiPv: policy.multiPv,
        ponder: policy.ponder,
        policySource: policy.source,
        mode: gameMode,
        trainingPonderEnabled,
      });
    }
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [turn, status, fen, uciHistory, difficulty, effectiveGameOver, engineColor, engineAutoEnabled, isGameActive, maxDepth, multiPv, gameMode, isWaitingForStop, trainingState.status, resolvePolicy, trainingPonderEnabled]);

  // Queue/reconnect policy: the engine clock is paused until the backend
  // confirms that the matching UCI search actually started.
  useEffect(() => {
    if (!searchStartedPositionKey || lastClockSearchPositionRef.current === searchStartedPositionKey) return;
    if (!shouldStartEngineClockForSearch({
      hasTimeControl: hasTC,
      gameStatus,
      isTerminal: effectiveGameOver,
      timeoutColor,
      turn,
      engineColor,
      searchStartedRequestId,
      // Position identity, rather than requestId, distinguishes a controlled
      // retry using a fresh engine generation.
      lastStartedRequestId: null,
    })) return;

    lastClockSearchPositionRef.current = searchStartedPositionKey;
    clock.startClock(engineColor);
  }, [clock, effectiveGameOver, engineColor, gameStatus, hasTC, searchStartedPositionKey, searchStartedRequestId, timeoutColor, turn]);

  useEffect(() => {
    if (clockStopSignal === 0) return;
    clock.stopClock();
  }, [clock, clockStopSignal]);

  // ── Analysis Mode Continuous Eval ─────────────────────────────────────────
  useEffect(() => {
    if (gameStatus !== 'active' || gameMode !== 'analysis') return;
    if (effectiveGameOver) return;

    // Immediately send the current position to the engine
    const timer = setTimeout(() => {
      startResolvedAnalysis(fen, uciHistory, 'analysis');
    }, 100);
    return () => clearTimeout(timer);
  }, [fen, uciHistory, gameMode, startResolvedAnalysis, effectiveGameOver, gameStatus, maxDepth, multiPv]);

  // ── Training Mode Continuous Eval ─────────────────────────────────────────
  useEffect(() => {
    if (gameStatus !== 'active' || gameMode !== 'training') return;
    if (effectiveGameOver) return;
    if (trainingState.status !== 'waiting-player') return;
    if (turn !== engineColor) {
      const timer = setTimeout(() => {
        startResolvedAnalysis(fen, uciHistory, 'training-root-review');
      }, 100);
      return () => clearTimeout(timer);
    }
  }, [fen, uciHistory, gameMode, turn, engineColor, startResolvedAnalysis, effectiveGameOver, gameStatus, maxDepth, multiPv, trainingState.status]);

  // ── Apply engine best move + clock management ────────────────────────────
  const normalizedInfo = useMemo(() => normalizeEngineInfo(engineInfo), [engineInfo]);
  const displayEngineInfo = normalizedInfo?.purpose === 'training-hint' ? null : normalizedInfo;
  const latestEngineInfoRef = useRef(normalizedInfo);
  useEffect(() => {
    if (normalizedInfo?.purpose !== 'training-hint') {
      latestEngineInfoRef.current = normalizedInfo;
    }
  }, [normalizedInfo]);

  useEffect(() => {
    if (!normalizedInfo?.requestId || !normalizedInfo.rootFen || normalizedInfo.purpose === 'training-hint') return;
    if (normalizedInfo.pvs.length === 0) return;

    const snapshot: AnalysisSnapshot = {
      requestId: normalizedInfo.requestId,
      purpose: normalizedInfo.purpose,
      fen: normalizedInfo.rootFen,
      lines: normalizedInfo.pvs.map(line => ({ ...line, pv: [...line.pv] })),
      requestedLimit: normalizedInfo.requestedLimit,
      reportedDepth: normalizedInfo.reportedDepth ?? normalizedInfo.depth ?? null,
      selectiveDepth: normalizedInfo.selectiveDepth ?? null,
      multiPv: normalizedInfo.pvs.length,
      status: 'live',
      createdAt: Date.now(),
    };

    dispatchAnalysisDisplay({ type: 'INFO', snapshot });
  }, [normalizedInfo]);

  useEffect(() => {
    if (!bestMoveResult || bestMoveResult.purpose === 'opponent') return;
    dispatchAnalysisDisplay({ type: 'COMPLETE', requestId: bestMoveResult.requestId });
  }, [bestMoveResult]);

  useEffect(() => {
    dispatchAnalysisDisplay({ type: 'FEN_CHANGED', fen });
  }, [fen]);

  useEffect(() => {
    dispatchAnalysisDisplay({ type: 'CLEAR' });
  }, [gameMode, gameStatus]);

  const trainingHint = useTrainingHint({
    fen,
    uciHistory,
    engineStatus: status,
    trainingState,
    engineInfo: normalizedInfo,
    dispatchTraining,
    startHintAnalysis: (rootFen, moves = []) => startResolvedAnalysis(rootFen, moves, 'training-hint', 1),
  });

  const boardArrows = useMemo(() => toChessboardArrows(composeBoardArrows({
    mode: gameMode,
    trainingState,
    currentFen: fen,
    engineInfo: displayEngineInfo,
    analysis: analysisDisplay,
    hintView: isTraining ? trainingHint.hintView : null,
  })), [analysisDisplay, displayEngineInfo, fen, gameMode, isTraining, trainingHint.hintView, trainingState]);

  const hintRequestAvailable = canRequestHint(trainingState, status, {
    isTraining,
    isGameActive,
    isTerminal: effectiveGameOver,
    isPlayerTurn: turn === orientation,
    hasPromotionPending: trainingState.status === 'promotion-pending',
  });

  useEffect(() => {
    const evaluation = normalizedInfo?.pvs?.[0]?.evaluation ?? null;
    if (evaluation && normalizedInfo?.rootFen === fen && normalizedInfo.purpose !== 'training-hint') {
      recordPositionEval(evaluation, moveHistory.length);
    }
  }, [fen, moveHistory.length, normalizedInfo, recordPositionEval]);

  useEffect(() => {
    const pendingReview = pendingMoveReviewRef.current;
    if (!pendingReview) return;
    if (gameMode !== 'training' || gameStatus !== 'active') return;
    if (trainingState.status !== 'reviewing-player-move') return;
    if (status !== 'idle' || fen !== pendingReview.resultFen) return;
    if (pendingReview.resultRequestStarted) return;

    pendingReview.resultRequestStarted = true;
    startResolvedAnalysis(fen, uciHistory, 'training-result-review');
  }, [fen, gameMode, gameStatus, maxDepth, multiPv, startResolvedAnalysis, status, trainingState.status, uciHistory]);

  useEffect(() => {
    const pendingReview = pendingMoveReviewRef.current;
    if (!pendingReview?.resultRequestStarted) return;
    if (status !== 'idle') return;
    if (normalizedInfo?.rootFen !== pendingReview.resultFen) return;

    const rawResultEvaluation = normalizedInfo.pvs?.[0]?.evaluation ?? null;
    const resultLooksSyntheticBook =
      pendingReview.moveIndex < 10 &&
      normalizedInfo.depth === 1 &&
      rawResultEvaluation?.kind === 'centipawn' &&
      rawResultEvaluation.value === 0;
    const resultEvaluation = resultLooksSyntheticBook ? null : rawResultEvaluation;
    const review = gradeMove({
      best: pendingReview.bestEvaluation,
      played: resultEvaluation,
      playerColor: pendingReview.playerColor,
      legalMoveCount: pendingReview.legalMoveCount,
      isBook: pendingReview.isBook,
    });
    if (review) {
      setMoveGrade(pendingReview.moveIndex, review.grade, review.loss, { hintLevelUsed: pendingReview.hintLevelUsed });
    }
    recordPositionEval(resultEvaluation, pendingReview.moveIndex + 1);
    dispatchTraining({ type: 'REVIEW_COMPLETED', reviewId: pendingReview.reviewId, available: resultEvaluation !== null });
    pendingMoveReviewRef.current = null;
  }, [normalizedInfo, recordPositionEval, setMoveGrade, status]);

  useEffect(() => {
    const pendingReview = pendingMoveReviewRef.current;
    if (!pendingReview?.resultRequestStarted) return;
    if (terminalCompletion?.rootFen !== pendingReview.resultFen) return;
    if (terminalCompletion.purpose !== 'training-result-review') return;

    recordPositionEval(null, pendingReview.moveIndex + 1);
    dispatchTraining({ type: 'REVIEW_COMPLETED', reviewId: pendingReview.reviewId, available: false });
    pendingMoveReviewRef.current = null;
  }, [recordPositionEval, terminalCompletion]);

  useEffect(() => {
    if (gameMode !== 'training') return;
    if (terminalCompletion?.purpose !== 'opponent') return;
    dispatchTraining({ type: 'TERMINAL', result: resultFromTerminal(terminalCompletion.terminal) });
    queueMicrotask(() => setGameStatus('completed'));
  }, [gameMode, terminalCompletion]);

  useEffect(() => {
    if (gameMode !== 'training') return;
    if (trainingState.status === 'showing-feedback') {
      dispatchTraining({ type: 'FEEDBACK_SHOWN' });
    }
  }, [gameMode, trainingState.status]);

  useEffect(() => {
    if (!engineAutoEnabled) return;
    const moveResult = bestMoveResult;
    const backendTimed = moveResult?.receivedAt !== undefined && moveResult.engineDeadlineAt !== undefined;
    const backendTimely = !backendTimed || moveResult.receivedAt! <= moveResult.engineDeadlineAt!;
    const timeoutOnlyCompletion = gameStatus === 'completed' && timeoutColor !== null;
    if (!isGameActive && !timeoutOnlyCompletion) return;
    if (!shouldAcceptEngineBestMove({ gameStatus, timeoutColor }) && !(timeoutOnlyCompletion && backendTimely)) return;
    if (moveResult?.move && turn === engineColor) {
      const normalizedCurrentFen = (() => {
        try { return new Chess(fen).fen(); } catch { return null; }
      })();
      const rejectionReason =
        appliedBestMoveRequestRef.current === moveResult.requestId ? 'duplicate-request' :
        !backendTimely ? 'flag-fell-before-backend-result' :
        !moveResult.positionKey ? 'missing-position-identity' :
        moveResult.positionFen !== normalizedCurrentFen ? 'fen-mismatch' :
        moveResult.expectedSide !== turn ? 'side-to-move-mismatch' :
        null;

      if (process.env.NODE_ENV !== 'production') {
        console.debug('[engine-bestmove-decision]', {
          requestId: moveResult.requestId,
          currentRequestId: appliedBestMoveRequestRef.current,
          move: moveResult.move,
          receivedAt: moveResult.receivedAt,
          currentFen: fen,
          expectedFen: moveResult.rootFen,
          clockExpired: timeoutColor !== null || !backendTimely,
          gameOver: effectiveGameOver,
          accepted: rejectionReason === null,
          rejectionReason,
        });
      }

      if (rejectionReason !== null) {
        clock.stopClock();
        if (moveResult.positionKey) {
          acknowledgeBestMove({ requestId: moveResult.requestId, positionKey: moveResult.positionKey, applied: false, oldFen: fen, failureReason: rejectionReason });
        }
        setGameStatus('completed');
        return;
      }
      appliedBestMoveRequestRef.current = moveResult.requestId;
      const acceptedMove = moveResult.move;

      // Receipt transfers clock ownership away from the search immediately;
      // application is then acknowledged separately.
      clock.stopClock();

      if (ignoreStaleBestMoveRef.current) {
        ignoreStaleBestMoveRef.current = false;
        acknowledgeBestMove({ requestId: moveResult.requestId, positionKey: moveResult.positionKey!, applied: false, oldFen: fen, failureReason: 'cancelled_request' });
        return;
      }
      if (isWaitingForStop) {
        if (stopTimeoutRef.current) {
          clearTimeout(stopTimeoutRef.current);
          stopTimeoutRef.current = null;
        }
        queueMicrotask(() => {
          setIsWaitingForStop(false);
        });
        acknowledgeBestMove({ requestId: moveResult.requestId, positionKey: moveResult.positionKey!, applied: false, oldFen: fen, failureReason: 'search_stopped' });
        return;
      }

      const from      = acceptedMove.substring(0, 2);
      const to        = acceptedMove.substring(2, 4);
      const promotion = acceptedMove.length === 5 ? acceptedMove[4] : undefined;
      let terminalAfterMove = false;
      let expectedNewFen: string | undefined;
      try {
        const preview = new Chess(fen);
        const applied = preview.move({ from, to, promotion });
        if (!applied) throw new Error('illegal move');
        terminalAfterMove = preview.isGameOver();
        expectedNewFen = preview.fen();
      } catch {
        acknowledgeBestMove({ requestId: moveResult.requestId, positionKey: moveResult.positionKey!, applied: false, oldFen: fen, failureReason: 'illegal_move_for_position' });
        queueMicrotask(() => setGameStatus('completed'));
        return;
      }
      const success = makeMove({ from, to, promotion });

      if (success && expectedNewFen) {
          acknowledgeBestMove({ requestId: moveResult.requestId, positionKey: moveResult.positionKey!, applied: true, oldFen: fen, newFen: expectedNewFen });
          queueMicrotask(() => {
            setTimeoutColor(null);
            if (timeoutOnlyCompletion) {
              setGameStatus('active');
            }
            if (gameMode === 'training') {
              dispatchTraining({ type: 'ENGINE_MOVE_RECEIVED' });
            }
            const transition = clockTransitionAfterLegalMove({
              hasTimeControl: hasTC,
              mover: engineColor,
              nextSide: orientation,
              isTerminal: terminalAfterMove,
            });

            if (transition.stopClock) {
              clock.stopClock();
              if (transition.incrementSide) {
                clock.applyIncrement(transition.incrementSide);
              }
            }

            if (transition.completeGame) {
              setGameStatus('completed');
              return;
            }

            if (transition.nextActiveSide) {
              clock.startClock(transition.nextActiveSide);
            }
          });
      } else {
        acknowledgeBestMove({ requestId: moveResult.requestId, positionKey: moveResult.positionKey!, applied: false, oldFen: fen, failureReason: 'move_application_failed' });
        queueMicrotask(() => setGameStatus('completed'));
      }
    }
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [bestMoveResult, turn, makeMove, moveHistory.length, recordPositionEval,
      setMoveGrade, engineColor, engineAutoEnabled, isGameActive, gameStatus, gameMode, timeoutColor, fen]);

  useEffect(() => {
    if (!engineAutoEnabled || turn !== engineColor || status !== 'error') return;
    clock.stopClock();
    queueMicrotask(() => setGameStatus('completed'));
  }, [clock, engineAutoEnabled, engineColor, status, turn]);

  // ── Global Game Over Watcher (Teardown) ──────────────────────────────────
  useEffect(() => {
    if (effectiveGameOver) {
      clock.stopClock();
      if (!isAnalysis) {
        stopEngine();
        releaseSession();
      }
    }
  }, [effectiveGameOver, stopEngine, clock, isAnalysis, releaseSession]);

  // ── User move handler ────────────────────────────────────────────────────
  const handleUserMove = (move: { from: string; to: string; promotion?: string }) => {
    if (isTraining && !canPlayerMove(trainingState)) return false;
    if (!isAnalysis && turn === engineColor) return false;
    // Block moves if game not started or already over
    if (!isAnalysis && (!isGameActive || effectiveGameOver)) return false;

    const moveIndex = moveHistory.length;
    const playerColor = turn;
    const rootInfo = latestEngineInfoRef.current?.rootFen === fen &&
      (!isTraining || latestEngineInfoRef.current?.purpose === 'training-root-review')
      ? latestEngineInfoRef.current
      : null;
    const bestPv = rootInfo?.pvs?.[0];
    const rootBestMove = bestPv?.pv?.[0];
    const rootEvaluation = bestPv?.evaluation ?? null;
    const legalMoveCount = game.moves({ verbose: true }).length;
    const hintLevelUsed = gameMode === 'training' ? hintLevelUsedForMove(trainingState, fen) : 0;
    let resultFen: string | null = null;
    let terminalAfterMove: GameResult | null = null;
    try {
      const preview = new Chess(fen);
      const applied = preview.move(move);
      if (applied) {
        resultFen = preview.fen();
        if (preview.isGameOver()) {
          terminalAfterMove = currentGameResult(preview, preview.turn() as PlayerColor, null, null);
        }
      }
    } catch {
      resultFen = null;
    }

    const result = makeMove(move);

    if (result) {
      if (gameMode === 'training' && resultFen) {
        const reviewId = nextReviewIdRef.current++;
        const uciMove = `${move.from}${move.to}${move.promotion ?? ''}`;
        const isLikelyBook =
          moveIndex < 10 &&
          rootInfo?.depth === 1 &&
          rootEvaluation?.kind === 'centipawn' &&
          rootEvaluation.value === 0 &&
          rootBestMove === uciMove;
        pendingMoveReviewRef.current = {
          reviewId,
          moveIndex,
          resultFen,
          playerColor,
          legalMoveCount,
          bestEvaluation: rootEvaluation,
          isBook: isLikelyBook,
          hintLevelUsed,
          resultRequestStarted: false,
        };
        dispatchTraining({ type: 'REVIEW_STARTED', reviewId });
        if (terminalAfterMove) {
          dispatchTraining({ type: 'TERMINAL', result: terminalAfterMove });
        }
      }
      const transition = clockTransitionAfterLegalMove({
        hasTimeControl: hasTC,
        mover: orientation,
        nextSide: engineColor,
        isTerminal: terminalAfterMove !== null,
      });

      if (transition.stopClock) {
        clock.stopClock();
        if (transition.incrementSide) {
          clock.applyIncrement(transition.incrementSide);
        }
      }

      if (transition.completeGame) {
        setGameStatus('completed');
      }
    }
    return result;
  };

  const handlePromotionPending = (promotion: PendingPromotion) => {
    if (gameMode === 'training') {
      dispatchTraining({ type: 'PROMOTION_REQUIRED', promotion });
    }
  };

  const handlePromotionSelected = (piece: PromotionPiece) => {
    if (gameMode === 'training') {
      dispatchTraining({ type: 'PROMOTION_SELECTED', piece });
    }
  };

  const handlePromotionCancelled = () => {
    if (gameMode === 'training') {
      dispatchTraining({ type: 'PROMOTION_CANCELLED' });
    }
  };

  // ── Resign ───────────────────────────────────────────────────────────────
  const handleResign = () => {
    setResignedBy(orientation);
    setGameStatus('completed');
    clock.stopClock();
  };

  // ── FEN/PGN (Analysis Mode) ──────────────────────────────────────────────
  const handleLoadFen = (fenStr: string) => {
    const ok = loadFen(fenStr);
    if (ok) {
      setPromotionResetKey(value => value + 1);
      analysisFenRef.current = fenStr;
      resetGrades();
      pendingMoveReviewRef.current = null;
      if (gameMode === 'analysis') {
        startResolvedAnalysis(fenStr, [], 'analysis');
      }
    }
  };

  const handleFenReset = () => {
    analysisFenRef.current = null;
    setPromotionResetKey(value => value + 1);
    resetGame();
    resetGrades();
    pendingMoveReviewRef.current = null;
    newGame();
  };

  // ── New game ─────────────────────────────────────────────────────────────
  const handleNewGame = () => {
    setupTriggerRef.current = document.activeElement instanceof HTMLElement ? document.activeElement : null;
    setIsSetupOpen(true);
  };

  const handleStartNewGame = (config: NewGameConfig) => {
    setIsSetupOpen(false);

    const resolvedColor: PlayerColor =
      config.playerColor === 'random'
        ? (Math.random() < 0.5 ? 'w' : 'b')
        : config.playerColor;

    setOrientation(resolvedColor);
    setDifficulty(config.difficulty);
    setTimeControl(config.timeControl);
    setResignedBy(null);
    setTimeoutColor(null);
    setGameStatus('active');
    if (gameMode === 'training') {
      dispatchTraining({ type: 'RESET_REQUESTED', reason: 'new-game', playerColor: resolvedColor });
      dispatchTraining({ type: 'ENTER', playerColor: resolvedColor });
    } else {
      dispatchTraining({ type: 'EXIT' });
    }

    analysisFenRef.current = null;
    setPromotionResetKey(value => value + 1);
    resetGame();
    resetGrades();
    pendingMoveReviewRef.current = null;

    clock.resetClock({
      initialWhiteMs: config.timeControl.initialMs,
      initialBlackMs: config.timeControl.initialMs,
    });
    newGame();

    if (config.maxDepth !== undefined) {
      setMaxDepth(config.maxDepth);
    }

    // If the player chose Black, the engine moves first — start the engine's clock.
    if (resolvedColor === 'b' && config.timeControl.initialMs > 0 && hasTC) {
      // The engine-auto-trigger will fire once status=idle; delay is fine.
    }
  };

  // ── Mode change ──────────────────────────────────────────────────────────
  const handleModeChange = (newMode: GameMode) => {
    setIsSetupOpen(false);
    stopEngine();
    if (gameMode === 'training') {
      dispatchTraining({ type: 'RESET_REQUESTED', reason: 'mode-switch', playerColor: orientation });
    }
    if (newMode !== 'training') {
      dispatchTraining({ type: 'EXIT' });
    }
    setPromotionResetKey(value => value + 1);
    pendingMoveReviewRef.current = null;
    resetGrades();
    setGameMode(newMode);
    setGameStatus('idle');
    clock.stopClock();
    if (gameMode === 'analysis' && newMode !== 'analysis') {
      analysisFenRef.current = null;
      resetGame();
    }
  };

  // ── Check square ─────────────────────────────────────────────────────────
  let checkSquare: string | null = null;
  if (game.isCheck()) {
    const board = game.board();
    for (let r = 0; r < 8; r++) {
      for (let c = 0; c < 8; c++) {
        if (board[r][c]?.type === 'k' && board[r][c]?.color === turn) {
          checkSquare = String.fromCharCode(97 + c) + (8 - r);
          break;
        }
      }
    }
  }

  const lastMove = moveHistory.length > 0 ? {
    from: moveHistory[moveHistory.length - 1].from,
    to:   moveHistory[moveHistory.length - 1].to,
  } : null;

  // ── Game over result string ───────────────────────────────────────────────
  const gameOverMessage = (() => {
    if (timeoutColor !== null) {
      return `${timeoutColor === 'w' ? 'Black' : 'White'} wins on Time`;
    }
    if (resignedBy !== null) {
      return `${resignedBy === 'w' ? 'Black' : 'White'} wins by Resignation`;
    }
    if (game.isCheckmate())             return turn === 'w' ? 'Black Wins by Checkmate' : 'White Wins by Checkmate';
    if (game.isStalemate())             return 'Draw by Stalemate';
    if (game.isThreefoldRepetition())   return 'Draw by Repetition';
    if (game.isInsufficientMaterial())  return 'Draw by Insufficient Material';
    if (game.isDraw())                  return 'Draw';
    return 'Game Over';
  })();

  const openSetup = useCallback(() => {
    setupTriggerRef.current = document.activeElement instanceof HTMLElement ? document.activeElement : null;
    setIsSetupOpen(true);
  }, []);
  const closeSetup = useCallback(() => {
    const trigger = setupTriggerRef.current;
    setIsSetupOpen(false);
    // Restore after the dialog cleanup removes `inert` from the application shell.
    requestAnimationFrame(() => {
      if (trigger?.isConnected) trigger.focus();
    });
  }, []);
  const startAnalysisSession = useCallback(() => setGameStatus('active'), []);
  const flipBoard = useCallback(() => setOrientation(value => value === 'w' ? 'b' : 'w'), []);
  const handleAnalysisImportSuccess = useCallback((finalFen: string) => {
    analysisFenRef.current = finalFen;
    resetGrades();
    pendingMoveReviewRef.current = null;
    if (gameMode === 'analysis') {
      startResolvedAnalysis(finalFen, [], 'analysis');
    }
  }, [gameMode, resetGrades, startResolvedAnalysis]);

  const startAnalysisFromDefault = () => {
    analysisFenRef.current = null;
    setPromotionResetKey(value => value + 1);
    resetGame();
    resetGrades();
    pendingMoveReviewRef.current = null;
    newGame();
    setGameStatus('active');
    setIsSetupOpen(false);
    return true;
  };

  const startAnalysisFromFen = (sourceFen: string) => {
    if (!loadFen(sourceFen)) return false;
    analysisFenRef.current = sourceFen;
    setPromotionResetKey(value => value + 1);
    resetGrades();
    pendingMoveReviewRef.current = null;
    setGameStatus('active');
    setIsSetupOpen(false);
    return true;
  };

  const startAnalysisFromPgn = (sourcePgn: string) => {
    const finalFen = loadPgn(sourcePgn);
    if (finalFen === false) return false;
    analysisFenRef.current = finalFen;
    setPromotionResetKey(value => value + 1);
    resetGrades();
    pendingMoveReviewRef.current = null;
    setGameStatus('active');
    setIsSetupOpen(false);
    return true;
  };

  return {
    mode: {
      value: gameMode,
      isAnalysis,
      isTraining,
      change: handleModeChange,
    },
    lifecycle: {
      status: gameStatus,
      isActive: isGameActive,
      isComplete: effectiveGameOver,
      gameOverMessage,
      timeoutColor,
      resignedBy,
    },
    board: {
      fen,
      orientation,
      arrows: boardArrows,
      checkSquare,
      lastMove,
      promotionResetKey,
      trainingHintView: isTraining ? trainingHint.hintView : null,
      disabled: isTraining ? !trainingBoardInteractive : false,
      onMove: handleUserMove,
      onPromotionPending: handlePromotionPending,
      onPromotionSelected: handlePromotionSelected,
      onPromotionCancelled: handlePromotionCancelled,
    },
    engine: {
      status,
      queuePosition,
      displayInfo: displayEngineInfo,
      showEvaluation: showEvalBar,
      showPanel: showEnginePanel,
      showConfiguration: showEngineConf,
      optionsUnavailable: engineOptionsUnavailable,
    },
    clocks: {
      value: clock,
      show: showClock,
      timeControl,
    },
    setup: {
      isOpen: isSetupOpen,
      difficulty,
      timeControl,
      maxDepth,
      multiPv,
      ownBook,
      trainingPonderEnabled,
      open: openSetup,
      close: closeSetup,
      start: handleStartNewGame,
      changeDifficulty: setDifficulty,
      changeMaxDepth: setMaxDepth,
      changeMultiPv: setMultiPv,
      changeOwnBook: handleOwnBookChange,
      changeTrainingPonder: handleTrainingPonderChange,
    },
    fairPlay: {
      connectionStatus: status,
      queuePosition,
      currentTurn: turn,
    },
    training: {
      state: trainingState,
      hint: trainingHint,
      canRequestHint: hintRequestAvailable,
    },
    analysis: {
      loadFen: handleLoadFen,
      resetFen: handleFenReset,
      exportPgn,
      loadPgn,
      importSucceeded: handleAnalysisImportSuccess,
      start: startAnalysisSession,
      startFromDefault: startAnalysisFromDefault,
      startFromFen: startAnalysisFromFen,
      startFromPgn: startAnalysisFromPgn,
    },
    history: {
      moves: moveHistory,
      grades,
      evaluationGraph: evalGraphData,
    },
    actions: {
      newGame: handleNewGame,
      undo: undoMove,
      flipBoard,
      resign: handleResign,
      canUndo: moveHistory.length > 0 && status !== 'thinking' && isGameActive,
      canResign: isGameActive && !effectiveGameOver && !isAnalysis,
    },
  } as const;

}
