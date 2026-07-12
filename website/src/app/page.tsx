"use client";

import { useEffect, useMemo, useReducer, useRef, useState, useCallback } from "react";
import { Chess } from "chess.js";
import { Header } from "@/components/layout/Header";
import { Footer } from "@/components/layout/Footer";
import { ChessBoardComponent } from "@/components/board/ChessBoard";
import { EvalBar } from "@/components/board/EvalBar";
import { EnginePanel } from "@/components/panels/EnginePanel";
import { MoveHistory } from "@/components/panels/MoveHistory";
import { EngineToggle } from "@/components/panels/EngineToggle";
import { ModeSelector } from "@/components/panels/ModeSelector";
import { PositionSetup } from "@/components/panels/PositionSetup";
import { EvalGraph } from "@/components/panels/EvalGraph";
import { ClockDisplay } from "@/components/panels/ClockDisplay";
import { GameControls } from "@/components/controls/GameControls";
import { NewGameModal, NewGameConfig } from "@/components/ui/NewGameModal";
import { useEngine } from "@/hooks/useEngine";
import { useChessGame } from "@/hooks/useChessGame";
import { useChessClock } from "@/hooks/useChessClock";
import { useMoveReview } from "@/hooks/useMoveReview";
import { useTrainingHint } from "@/hooks/useTrainingHint";
import { PlayerColor, DifficultyLevel, GameMode, TimeControl, TIME_CONTROLS } from "@/types/engine";
import { NormalizedEvaluation, gradeMove, normalizeEngineInfo } from "@/lib/engine-evaluation";
import {
  canChangeTrainingSettings,
  canPlayerMove,
  canRequestHint,
  initialTrainingState,
  resultFromTerminal,
  trainingReducer,
} from "@/lib/training-machine";
import { hintLevelUsedForMove } from "@/lib/training-hint";
import { composeBoardArrows, toChessboardArrows } from "@/lib/board-arrows";
import { TrainingHintPanel } from "@/components/panels/TrainingHintPanel";
import type { GameResult } from "@/lib/training-machine";
import type { PendingPromotion, PromotionPiece } from "@/lib/promotion";

// ── Types ────────────────────────────────────────────────────────────────────

type GameStatus = 'idle' | 'active' | 'completed';

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

export default function Home() {
  const { status, engineInfo, bestMove, terminalCompletion, queuePosition, sendMove, newGame, startAnalysis, stopEngine, releaseSession, setEngineOption } = useEngine();
  const { game, fen, moveHistory, uciHistory, makeMove, resetGame, undoMove, loadFen, exportPgn, loadPgn, turn, isGameOver } = useChessGame();
  const { grades, evalGraphData, recordPositionEval, setMoveGrade, resetGrades } = useMoveReview();

  const [orientation, setOrientation]   = useState<PlayerColor>('w');
  const [difficulty, setDifficulty]     = useState<DifficultyLevel>('standard');
  const [gameMode, setGameMode]         = useState<GameMode>('fair');
  const [timeControl, setTimeControl]   = useState<TimeControl>(DEFAULT_TC);
  const [gameStatus, setGameStatus]     = useState<GameStatus>('idle');
  const [resignedBy, setResignedBy]     = useState<PlayerColor | null>(null);
  // Default to false so the user can explore the UI before starting a game.
  const [isNewGameModalOpen, setIsNewGameModalOpen] = useState(false);
  const [maxDepth, setMaxDepth] = useState(10);
  const [multiPv, setMultiPv] = useState(3);
  const [ownBook, setOwnBook] = useState(true);
  const [isWaitingForStop, setIsWaitingForStop] = useState(false);
  const [promotionResetKey, setPromotionResetKey] = useState(0);
  const [timeoutColor, setTimeoutColor] = useState<PlayerColor | null>(null);
  const [trainingState, dispatchTraining] = useReducer(trainingReducer, initialTrainingState);
  const ignoreStaleBestMoveRef = useRef(false);
  const stopTimeoutRef = useRef<NodeJS.Timeout | null>(null);
  const appliedOwnBookRef = useRef<boolean | null>(null);

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
      if (hasTC) {
        sendMove(fen, uciHistory, {
          wtime: clock.whiteMs,
          btime: clock.blackMs,
          winc:  timeControl.incMs,
          binc:  timeControl.incMs,
          difficulty,
          multiPv: multiPv,
          ponder: gameMode === 'fair',
        });
      } else {
        sendMove(fen, uciHistory, {
          difficulty,
          multiPv: multiPv,
          ponder: gameMode === 'fair',
        });
      }
    }
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [turn, status, fen, uciHistory, difficulty, effectiveGameOver, engineColor, engineAutoEnabled, isGameActive, maxDepth, multiPv, gameMode, isWaitingForStop, trainingState.status]);

  // ── Analysis Mode Continuous Eval ─────────────────────────────────────────
  useEffect(() => {
    if (gameStatus !== 'active' || gameMode !== 'analysis') return;
    if (effectiveGameOver) return;

    // Immediately send the current position to the engine
    const timer = setTimeout(() => {
      startAnalysis(fen, uciHistory, maxDepth, multiPv, 'analysis');
    }, 100);
    return () => clearTimeout(timer);
  }, [fen, uciHistory, gameMode, startAnalysis, effectiveGameOver, gameStatus, maxDepth, multiPv]);

  // ── Training Mode Continuous Eval ─────────────────────────────────────────
  useEffect(() => {
    if (gameStatus !== 'active' || gameMode !== 'training') return;
    if (effectiveGameOver) return;
    if (trainingState.status !== 'waiting-player') return;
    if (turn !== engineColor) {
      const timer = setTimeout(() => {
        startAnalysis(fen, uciHistory, maxDepth, multiPv, 'training-root-review');
      }, 100);
      return () => clearTimeout(timer);
    }
  }, [fen, uciHistory, gameMode, turn, engineColor, startAnalysis, effectiveGameOver, gameStatus, maxDepth, multiPv, trainingState.status]);

  // ── Apply engine best move + clock management ────────────────────────────
  const normalizedInfo = useMemo(() => normalizeEngineInfo(engineInfo), [engineInfo]);
  const displayEngineInfo = normalizedInfo?.purpose === 'training-hint' ? null : normalizedInfo;
  const latestEngineInfoRef = useRef(normalizedInfo);
  useEffect(() => {
    if (normalizedInfo?.purpose !== 'training-hint') {
      latestEngineInfoRef.current = normalizedInfo;
    }
  }, [normalizedInfo]);

  const trainingHint = useTrainingHint({
    fen,
    uciHistory,
    maxDepth,
    engineStatus: status,
    trainingState,
    engineInfo: normalizedInfo,
    dispatchTraining,
    startAnalysis,
  });

  const boardArrows = useMemo(() => toChessboardArrows(composeBoardArrows({
    mode: gameMode,
    trainingState,
    currentFen: fen,
    engineInfo: displayEngineInfo,
    hintView: isTraining ? trainingHint.hintView : null,
  })), [displayEngineInfo, fen, gameMode, isTraining, trainingHint.hintView, trainingState]);

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
    startAnalysis(fen, uciHistory, maxDepth, multiPv, 'training-result-review');
  }, [fen, gameMode, gameStatus, maxDepth, multiPv, startAnalysis, status, trainingState.status, uciHistory]);

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
    if (gameStatus === 'completed') return;
    let isMounted = true;

    if (!engineAutoEnabled) return;
    if (!isGameActive) return;
    if (bestMove && turn === engineColor) {
      if (ignoreStaleBestMoveRef.current) {
        ignoreStaleBestMoveRef.current = false;
        return;
      }
      if (isWaitingForStop) {
        if (stopTimeoutRef.current) {
          clearTimeout(stopTimeoutRef.current);
          stopTimeoutRef.current = null;
        }
        queueMicrotask(() => {
          if (isMounted) {
            setIsWaitingForStop(false);
          }
        });
        return () => { isMounted = false; };
      }

      queueMicrotask(() => {
        if (!isMounted) return;

        const from      = bestMove.substring(0, 2);
        const to        = bestMove.substring(2, 4);
        const promotion = bestMove.length === 5 ? bestMove[4] : undefined;
        const success   = makeMove({ from, to, promotion });

        if (success) {
          if (gameMode === 'training') {
            dispatchTraining({ type: 'ENGINE_MOVE_RECEIVED' });
          }
          if (hasTC) {
            clock.stopClock();
            clock.applyIncrement(engineColor);
          }

          if (hasTC && !effectiveGameOver) {
            clock.startClock(orientation);
          }

        }
      });
    }

    return () => { isMounted = false; };
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [bestMove, turn, makeMove, moveHistory.length, recordPositionEval,
      setMoveGrade, engineColor, engineAutoEnabled, isGameActive, gameStatus, gameMode]);

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
      if (hasTC && !effectiveGameOver) {
        clock.stopClock();
        clock.applyIncrement(orientation);
        
        // Only start the engine clock if the move successfully applied
        clock.startClock(engineColor);
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
        startAnalysis(fenStr, [], maxDepth, multiPv, 'analysis');
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
    if (isAnalysis) {
      setPromotionResetKey(value => value + 1);
      analysisFenRef.current = null;
      resetGame();
      resetGrades();
      pendingMoveReviewRef.current = null;
    } else {
      setIsNewGameModalOpen(true);
    }
  };

  const handleStartNewGame = (config: NewGameConfig) => {
    setIsNewGameModalOpen(false);

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
      handleNewGame();
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

  return (
    <div className="h-screen overflow-hidden flex flex-col bg-zinc-950 text-zinc-100">
      <Header />

      <main className="flex-1 min-h-0 overflow-hidden">
        <div className="h-full max-w-[1500px] w-full mx-auto px-4 md:px-6 py-4 grid grid-cols-1 lg:grid-cols-[1fr_540px] gap-6">

          {/* ── Left Column — Board ─────────────────────────────────── */}
          <div className="flex gap-4 items-center min-h-0 justify-center">

            {showEvalBar && (
              <EvalBar
                evaluation={displayEngineInfo?.pvs?.[0]?.evaluation ?? null}
                orientation={orientation}
              />
            )}

            <div className="h-full aspect-square max-h-[calc(100vh-12rem)] relative flex-shrink-0">
              <ChessBoardComponent
                key={promotionResetKey}
                fen={fen}
                arrows={showEnginePanel ? boardArrows : []}
                onMove={handleUserMove}
                onPromotionPending={handlePromotionPending}
                onPromotionSelected={handlePromotionSelected}
                onPromotionCancelled={handlePromotionCancelled}
                disabled={isTraining ? !trainingBoardInteractive : false}
                orientation={orientation === 'w' ? 'white' : 'black'}
                checkSquare={checkSquare}
                lastMove={lastMove}
                hintView={isTraining ? trainingHint.hintView : null}
              />


              {/* TC badge */}
              {showClock && (
                <div className="absolute top-2 right-2 z-10 pointer-events-none">
                  <span className="bg-black/60 text-muted text-[9px] font-bold font-mono tracking-wider px-2 py-0.5 rounded-full backdrop-blur-sm">
                    ⏱ {timeControl.label}
                  </span>
                </div>
              )}


              {/* Idle overlay — shown before game starts */}
              {gameStatus === 'idle' && (
                <div className="absolute inset-0 bg-background/50 backdrop-blur-sm z-50 flex flex-col items-center justify-center rounded-md gap-4">
                  <div className="text-center">
                    <h2 className="text-2xl font-bold text-foreground mb-1">Ready to Play?</h2>
                    <p className="text-sm text-muted">Start the engine when you are ready.</p>
                  </div>
                  {isAnalysis ? (
                    <button
                      onClick={() => setGameStatus('active')}
                      className="px-8 py-3 bg-primary hover:bg-primary/90 text-primary-foreground rounded-xl font-bold text-base shadow-lg shadow-primary/30 transition-all duration-150 active:scale-[0.97]"
                    >
                      Start Analysis →
                    </button>
                  ) : (
                    <button
                      onClick={() => setIsNewGameModalOpen(true)}
                      className="px-8 py-3 bg-primary hover:bg-primary/90 text-primary-foreground rounded-xl font-bold text-base shadow-lg shadow-primary/30 transition-all duration-150 active:scale-[0.97]"
                    >
                      Setup Match →
                    </button>
                  )}
                </div>
              )}

              {/* Game over overlay */}
              {effectiveGameOver && !isAnalysis && (
                <div className="absolute inset-0 bg-background/70 backdrop-blur-sm z-50 flex flex-col items-center justify-center rounded-md">
                  <h2 className="text-3xl font-bold text-foreground mb-2">Game Over</h2>
                  <p className="text-lg text-muted-foreground mb-6 font-medium">{gameOverMessage}</p>
                  <button
                    onClick={handleNewGame}
                    className="px-6 py-2 bg-primary hover:bg-primary/90 text-primary-foreground rounded-md font-semibold transition-colors"
                  >
                    Play Again
                  </button>
                </div>
              )}
            </div>
          </div>

          {/* ── Right Column — Sidebar ──────────────────────────────── */}
          <div className="flex flex-col gap-3 h-full min-h-0 overflow-y-auto scrollbar-thin overflow-auto pr-2">

            <ModeSelector mode={gameMode} onModeChange={handleModeChange} />

            {/* Clock */}
            {showClock && (
              <ClockDisplay
                whiteMs={clock.whiteMs}
                blackMs={clock.blackMs}
                activeSide={clock.activeSide}
                playerColor={orientation}
                isRunning={clock.isRunning}
                isGameActive={isGameActive}
                fen={fen}
                disabled={false}
              />
            )}

            {/* Merged FEN+PGN — Analysis only */}
            {isAnalysis && (
              <PositionSetup
                currentFen={fen}
                onLoadFen={handleLoadFen}
                onReset={handleFenReset}
                exportPgn={exportPgn}
                loadPgn={loadPgn}
                onImportSuccess={(finalFen) => {
                  analysisFenRef.current = finalFen;
                  resetGrades();
                  pendingMoveReviewRef.current = null;
                  if (gameMode === 'analysis') {
                    startAnalysis(finalFen, [], maxDepth, multiPv, 'analysis');
                  }
                }}
              />
            )}

            {showEngineConf && (
              <EngineToggle 
                currentVersion="Texel-Tuned HCE" 
                maxDepth={maxDepth}
                onDepthChange={setMaxDepth}
                difficulty={difficulty}
                onDifficultyChange={setDifficulty}
                multiPv={multiPv}
                onMultiPvChange={setMultiPv}
                gameMode={gameMode}
                ownBook={ownBook}
                optionsDisabled={engineOptionsUnavailable}
                onOwnBookChange={handleOwnBookChange}
              />
            )}

            {isTraining && (
              <TrainingHintPanel
                hint={trainingState.status === 'waiting-player' ? trainingState.hint : undefined}
                hintView={trainingHint.hintView}
                canRequest={hintRequestAvailable}
                onRequest={trainingHint.requestHint}
              />
            )}

            {showEnginePanel && (
              <EnginePanel info={displayEngineInfo} status={status} queuePosition={queuePosition} />
            )}

            <MoveHistory moves={moveHistory} grades={grades} showGrades={showEnginePanel} />

            {showEnginePanel && (
              <EvalGraph data={evalGraphData} />
            )}

            <div className="shrink-0">
              <GameControls
                onNewGameClick={handleNewGame}
                onUndo={undoMove}
                onFlipBoard={() => setOrientation(o => o === 'w' ? 'b' : 'w')}
                onResign={handleResign}
                orientation={orientation}
                canUndo={moveHistory.length > 0 && status !== 'thinking' && isGameActive}
                canResign={isGameActive && !effectiveGameOver && !isAnalysis}
              />
            </div>
          </div>

        </div>
      </main>

      <Footer />

      <NewGameModal
        isOpen={isNewGameModalOpen}
        gameMode={gameMode}
        defaultDifficulty={difficulty}
        defaultPlayerColor={orientation}
        defaultTimeControl={timeControl}
        defaultMaxDepth={maxDepth}
        onStart={handleStartNewGame}
        onCancel={() => setIsNewGameModalOpen(false)}
      />
    </div>
  );
}

function currentGameResult(
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
  if (game.isStalemate()) {
    return { reason: 'stalemate' };
  }
  if (game.isDraw()) {
    return { reason: 'draw' };
  }
  return { reason: 'unknown' };
}
