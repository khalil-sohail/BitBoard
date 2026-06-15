"use client";

import { useEffect, useRef, useState, useCallback } from "react";
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
import { useEvalHistory } from "@/hooks/useEvalHistory";
import { useMoveReview } from "@/hooks/useMoveReview";
import { PlayerColor, DifficultyLevel, GameMode, TimeControl, TIME_CONTROLS } from "@/types/engine";

// ── Types ────────────────────────────────────────────────────────────────────

type GameStatus = 'idle' | 'active' | 'completed';

// Default time control: 3+0
const DEFAULT_TC = TIME_CONTROLS[2];

// ── Component ─────────────────────────────────────────────────────────────────

export default function Home() {
  const { status, engineInfo, bestMove, queuePosition, sendMove, newGame, startAnalysis, stopEngine } = useEngine();
  const { game, fen, moveHistory, uciHistory, makeMove, resetGame, undoMove, loadFen, exportPgn, loadPgn, turn, isGameOver } = useChessGame();
  const { addEvalPoint, resetEvalHistory } = useEvalHistory();
  const { grades, evalGraphData, recordEval, resetGrades } = useMoveReview();

  const [orientation, setOrientation]   = useState<PlayerColor>('w');
  const [difficulty, setDifficulty]     = useState<DifficultyLevel>('standard');
  const [gameMode, setGameMode]         = useState<GameMode>('fair');
  const [timeControl, setTimeControl]   = useState<TimeControl>(DEFAULT_TC);
  const [gameStatus, setGameStatus]     = useState<GameStatus>('idle');
  const [resignedBy, setResignedBy]     = useState<PlayerColor | null>(null);
  // Default to false so the user can explore the UI before starting a game.
  const [isNewGameModalOpen, setIsNewGameModalOpen] = useState(false);
  const [maxDepth, setMaxDepth] = useState(30);
  const [isWaitingForStop, setIsWaitingForStop] = useState(false);
  const ignoreStaleBestMoveRef = useRef(false);
  const stopTimeoutRef = useRef<NodeJS.Timeout | null>(null);

  // Cleanup timeout on unmount
  useEffect(() => {
    return () => {
      if (stopTimeoutRef.current) {
        clearTimeout(stopTimeoutRef.current);
      }
    };
  }, []);

  // Whether a real time control is active (initialMs > 0)
  const hasTC = timeControl.initialMs > 0;
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

  const lastNormalizedEvalRef = useRef<number | null>(null);
  const analysisFenRef = useRef<string | null>(null);

  const engineColor: PlayerColor = orientation === 'w' ? 'b' : 'w';

  // ── Derived mode flags ───────────────────────────────────────────────────
  const isAnalysis = gameMode === 'analysis';
  const isTraining = gameMode === 'training';
  const isFairPlay = gameMode === 'fair';

  const engineAutoEnabled = !isAnalysis;

  const showEvalBar     = isTraining || isAnalysis;
  const showEnginePanel = isTraining || isAnalysis;
  const showEngineConf  = isTraining || isAnalysis;
  const showClock       = !isAnalysis && hasTC;

  // ── Effective game-over (board or resign) ────────────────────────────────
  const effectiveGameOver = (gameStatus === 'active' && isGameOver) || gameStatus === 'completed';

  // ── Engine auto-trigger ──────────────────────────────────────────────────
  useEffect(() => {
    if (!engineAutoEnabled) return;
    // ONLY fire when the game is actively running
    if (!isGameActive) return;
    if (isWaitingForStop) return;
    if (turn === engineColor && status === 'ready' && !effectiveGameOver) {
      
      // Fair Play mode runs purely on time, no depth ceiling.
      // Other modes (Training, Analysis) include depth as a limit or ceiling.
      const searchDepth = gameMode === 'fair' ? undefined : maxDepth;

      if (hasTC) {
        sendMove(fen, uciHistory, {
          wtime: clock.whiteMs,
          btime: clock.blackMs,
          winc:  timeControl.incMs,
          binc:  timeControl.incMs,
          depth: searchDepth,
        });
      } else {
        sendMove(fen, uciHistory, {
          difficulty,
          depth: searchDepth,
        });
      }
    }
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [turn, status, fen, uciHistory, difficulty, effectiveGameOver, engineColor, engineAutoEnabled, isGameActive, maxDepth, gameMode, isWaitingForStop]);

  // ── Analysis Mode Continuous Eval ─────────────────────────────────────────
  useEffect(() => {
    if (gameStatus !== 'active' || gameMode !== 'analysis') return;
    if (effectiveGameOver) return;

    // Immediately send the current position to the engine
    const timer = setTimeout(() => {
      startAnalysis(fen, uciHistory, maxDepth);
    }, 100);
    return () => clearTimeout(timer);
  }, [fen, uciHistory, gameMode, startAnalysis, effectiveGameOver, gameStatus, maxDepth]);

  // ── Training Mode Continuous Eval ─────────────────────────────────────────
  useEffect(() => {
    if (gameStatus !== 'active' || gameMode !== 'training') return;
    if (effectiveGameOver) return;
    if (turn !== engineColor) {
      const timer = setTimeout(() => {
        startAnalysis(fen, uciHistory, maxDepth);
      }, 100);
      return () => clearTimeout(timer);
    }
  }, [fen, uciHistory, gameMode, turn, engineColor, startAnalysis, effectiveGameOver, gameStatus, maxDepth]);

  // ── Apply engine best move + clock management ────────────────────────────
  const latestEngineInfoRef = useRef(engineInfo);
  useEffect(() => { latestEngineInfoRef.current = engineInfo; }, [engineInfo]);

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
        setIsWaitingForStop(false);
        if (stopTimeoutRef.current) {
          clearTimeout(stopTimeoutRef.current);
          stopTimeoutRef.current = null;
        }
        return;
      }

      queueMicrotask(() => {
        if (!isMounted) return;

        const from      = bestMove.substring(0, 2);
        const to        = bestMove.substring(2, 4);
        const promotion = bestMove.length === 5 ? bestMove[4] : undefined;
        const success   = makeMove({ from, to, promotion });

        if (success) {
          if (hasTC) {
            clock.stopClock();
            clock.applyIncrement(engineColor);
          }

          if (hasTC && !effectiveGameOver) {
            clock.startClock(orientation);
          }

          const info = latestEngineInfoRef.current;
          if (info?.pvs && info.pvs.length > 0) {
            const topPv      = info.pvs[0];
            const scoreToLog = engineColor === 'b' ? -topPv.score : topPv.score;
            addEvalPoint(moveHistory.length + 1, scoreToLog);
            recordEval(
              scoreToLog,
              moveHistory.length,
              engineColor,
              !!(topPv.mate !== undefined && lastNormalizedEvalRef.current !== null),
              !!(topPv.mate !== undefined),
            );
            lastNormalizedEvalRef.current = scoreToLog;
          }
        }
      });
    }

    return () => { isMounted = false; };
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [bestMove, turn, makeMove, addEvalPoint, moveHistory.length,
      recordEval, engineColor, engineAutoEnabled, isGameActive, gameStatus]);

  // ── Global Game Over Watcher (Teardown) ──────────────────────────────────
  useEffect(() => {
    if (effectiveGameOver) {
      stopEngine();
      clock.stopClock();
    }
  }, [effectiveGameOver, stopEngine, clock]);

  // ── User move handler ────────────────────────────────────────────────────
  const handleUserMove = (move: { from: string; to: string; promotion?: string }) => {
    if (!isAnalysis && turn === engineColor) return false;
    // Block moves if game not started or already over
    if (!isAnalysis && (!isGameActive || effectiveGameOver)) return false;

    if (gameMode === 'training') {
      if (status === 'thinking') {
        setIsWaitingForStop(true);
        if (stopTimeoutRef.current) clearTimeout(stopTimeoutRef.current);
        stopTimeoutRef.current = setTimeout(() => {
          setIsWaitingForStop((prev) => {
            if (prev) return false;
            return prev;
          });
          stopTimeoutRef.current = null;
        }, 500);
      } else if (bestMove) {
        ignoreStaleBestMoveRef.current = true;
      }
      stopEngine();
    }

    const result = makeMove(move);

    if (result) {
      if (lastNormalizedEvalRef.current !== null) {
        recordEval(lastNormalizedEvalRef.current, moveHistory.length, turn);
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
      analysisFenRef.current = fenStr;
      resetEvalHistory();
      resetGrades();
      lastNormalizedEvalRef.current = null;
    }
  };

  const handleFenReset = () => {
    analysisFenRef.current = null;
    resetGame();
    resetEvalHistory();
    resetGrades();
    lastNormalizedEvalRef.current = null;
    newGame();
  };

  // ── New game ─────────────────────────────────────────────────────────────
  const handleNewGame = () => {
    if (isAnalysis) {
      analysisFenRef.current = null;
      resetGame();
      resetEvalHistory();
      resetGrades();
      lastNormalizedEvalRef.current = null;
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

    analysisFenRef.current = null;
    resetGame();
    resetEvalHistory();
    resetGrades();
    lastNormalizedEvalRef.current = null;

    clock.resetClock();
    newGame();

    // If the player chose Black, the engine moves first — start the engine's clock.
    if (resolvedColor === 'b' && config.timeControl.initialMs > 0) {
      // The engine-auto-trigger will fire once status=ready; delay is fine.
    }
  };

  // ── Mode change ──────────────────────────────────────────────────────────
  const handleModeChange = (newMode: GameMode) => {
    stopEngine();
    setGameMode(newMode);
    setGameStatus('idle');
    clock.stopClock();
    if (gameMode === 'analysis' && newMode !== 'analysis') {
      handleNewGame();
    }
  };

  // ── Normalized eval ──────────────────────────────────────────────────────
  const normalizedInfo = engineInfo ? {
    ...engineInfo,
    pvs: engineInfo.pvs.map(p => {
      const p2 = { ...p };
      if (engineColor === 'b') {
        if (p2.score !== undefined) p2.score = -p2.score;
        if (p2.mate  !== undefined) p2.mate  = -p2.mate;
      }
      return p2;
    })
  } : null;

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

  const [timeoutColor, setTimeoutColor] = useState<PlayerColor | null>(null);

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
                evalScore={normalizedInfo?.pvs?.[0]?.score ?? 0}
                mate={normalizedInfo?.pvs?.[0]?.mate}
                turn={turn}
                orientation={orientation}
              />
            )}

            <div className="h-full aspect-square max-h-[calc(100vh-12rem)] relative flex-shrink-0">
              <ChessBoardComponent
                fen={fen}
                pvs={showEnginePanel ? normalizedInfo?.pvs : undefined}
                onMove={handleUserMove}
                orientation={orientation === 'w' ? 'white' : 'black'}
                checkSquare={checkSquare}
                lastMove={lastMove}
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
                onLoadFen={handleLoadFen}
                onReset={handleFenReset}
                exportPgn={exportPgn}
                loadPgn={loadPgn}
                onImportSuccess={(finalFen) => {
                  analysisFenRef.current = finalFen;
                  resetEvalHistory();
                  resetGrades();
                  lastNormalizedEvalRef.current = null;
                }}
              />
            )}

            {showEngineConf && (
              <EngineToggle 
                currentVersion="Texel-Tuned HCE" 
                maxDepth={maxDepth}
                onDepthChange={setMaxDepth}
                gameMode={gameMode}
              />
            )}

            {showEnginePanel && (
              <EnginePanel info={normalizedInfo} status={status} queuePosition={queuePosition} />
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
        defaultDifficulty={difficulty}
        defaultPlayerColor={orientation}
        defaultTimeControl={timeControl}
        onStart={handleStartNewGame}
        onCancel={() => setIsNewGameModalOpen(false)}
      />
    </div>
  );
}
