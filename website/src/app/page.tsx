"use client";

import { useEffect, useRef, useState } from "react";
import { Header } from "@/components/layout/Header";
import { Footer } from "@/components/layout/Footer";
import { ChessBoardComponent } from "@/components/board/ChessBoard";
import { EvalBar } from "@/components/board/EvalBar";
import { EnginePanel } from "@/components/panels/EnginePanel";
import { MoveHistory } from "@/components/panels/MoveHistory";
import { EngineToggle } from "@/components/panels/EngineToggle";
import { ModeSelector } from "@/components/panels/ModeSelector";
import { FenInput } from "@/components/panels/FenInput";
import { EvalGraph } from "@/components/panels/EvalGraph";
import { PgnControls } from "@/components/panels/PgnControls";
import { GameControls } from "@/components/controls/GameControls";
import { NewGameModal, NewGameConfig } from "@/components/ui/NewGameModal";
import { useEngine } from "@/hooks/useEngine";
import { useChessGame } from "@/hooks/useChessGame";
import { useEvalHistory } from "@/hooks/useEvalHistory";
import { useMoveReview } from "@/hooks/useMoveReview";
import { PlayerColor, DifficultyLevel, GameMode } from "@/types/engine";

export default function Home() {
  const { status, engineInfo, bestMove, queuePosition, sendMove, newGame, setEngineOption, startAnalysis } = useEngine();
  const { game, fen, moveHistory, uciHistory, makeMove, resetGame, undoMove, loadFen, exportPgn, loadPgn, turn, isGameOver } = useChessGame();
  const { evalHistory, addEvalPoint, resetEvalHistory } = useEvalHistory();
  const { grades, evalGraphData, recordEval, resetGrades } = useMoveReview();

  const [orientation, setOrientation] = useState<PlayerColor>('w');
  const [difficulty, setDifficulty] = useState<DifficultyLevel>('standard');
  const [gameMode, setGameMode] = useState<GameMode>('fair');
  const [isNewGameModalOpen, setIsNewGameModalOpen] = useState(false);

  // Track the last White-normalized eval score so we can compute deltas for user moves
  const lastNormalizedEvalRef = useRef<number | null>(null);
  // Track the custom FEN loaded in Analysis Mode (null = startpos)
  const analysisFenRef = useRef<string | null>(null);

  const engineColor = orientation === 'w' ? 'b' : 'w';

  // ── Derived mode flags ────────────────────────────────────────────────────
  const isAnalysis  = gameMode === 'analysis';
  const isTraining  = gameMode === 'training';
  const isFairPlay  = gameMode === 'fair';

  // In analysis mode the user controls both sides — no auto engine response
  const engineAutoEnabled = !isAnalysis;

  // UI visibility derived from mode
  const showEvalBar     = isTraining || isAnalysis;
  const showEnginePanel = isTraining || isAnalysis;
  const showEngineConf  = isTraining || isAnalysis;
  const showFenInput    = isAnalysis;

  // ── Engine auto-trigger ───────────────────────────────────────────────────
  useEffect(() => {
    if (!engineAutoEnabled) return;
    if (turn === engineColor && status === 'ready' && !isGameOver) {
      sendMove(fen, uciHistory, difficulty);
    }
  }, [turn, status, fen, uciHistory, sendMove, difficulty, isGameOver, engineColor, engineAutoEnabled]);

  // ── Analysis / Training Continuous Eval ───────────────────────────────────
  useEffect(() => {
    if (isGameOver) return;
    if (isAnalysis || (isTraining && turn !== engineColor)) {
      // Debounce slightly to avoid spamming the engine
      const timer = setTimeout(() => {
        startAnalysis(fen, uciHistory);
      }, 100);
      return () => clearTimeout(timer);
    }
  }, [fen, uciHistory, isAnalysis, isTraining, turn, engineColor, startAnalysis, isGameOver]);

  // ── Apply engine best move ────────────────────────────────────────────────
  const latestEngineInfoRef = useRef(engineInfo);
  useEffect(() => {
    latestEngineInfoRef.current = engineInfo;
  }, [engineInfo]);

  useEffect(() => {
    if (!engineAutoEnabled) return;
    if (bestMove && turn === engineColor) {
      const from = bestMove.substring(0, 2);
      const to   = bestMove.substring(2, 4);
      const promotion = bestMove.length === 5 ? bestMove[4] : undefined;
      const success = makeMove({ from, to, promotion });

      const info = latestEngineInfoRef.current;
      if (success && info?.pvs && info.pvs.length > 0) {
        const topPv = info.pvs[0];
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
  }, [bestMove, turn, makeMove, addEvalPoint, moveHistory.length,
      recordEval, engineColor, engineAutoEnabled]);

  // ── User move handler ─────────────────────────────────────────────────────
  const handleUserMove = (move: { from: string; to: string; promotion?: string }) => {
    // In normal modes: block moves while it's the engine's turn
    if (!isAnalysis && turn === engineColor) return false;
    const result = makeMove(move);
    if (result && lastNormalizedEvalRef.current !== null) {
      const userColor = turn;
      recordEval(lastNormalizedEvalRef.current, moveHistory.length, userColor);
    }
    return result;
  };

  // ── FEN load (Analysis Mode) ──────────────────────────────────────────────
  const handleLoadFen = (fenStr: string) => {
    const ok = loadFen(fenStr);
    if (ok) {
      analysisFenRef.current = fenStr;
      // The Analysis / Training Continuous Eval effect will automatically trigger startAnalysis
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
    newGame(); // sends ucinewgame + isready
  };

  // ── New game ──────────────────────────────────────────────────────────────
  // Open the modal instead of immediately resetting
  const handleNewGame = () => {
    if (isAnalysis) {
      // In Analysis mode, skip the modal — just reset the board silently
      analysisFenRef.current = null;
      resetGame();
      resetEvalHistory();
      resetGrades();
      lastNormalizedEvalRef.current = null;
    } else {
      setIsNewGameModalOpen(true);
    }
  };

  /**
   * Called when the user confirms the New Game modal.
   * Resolves 'random' color, updates difficulty, resets all state, and
   * triggers a fresh engine session.
   */
  const handleStartNewGame = (config: NewGameConfig) => {
    setIsNewGameModalOpen(false);

    // Resolve random color
    const resolvedColor: PlayerColor =
      config.playerColor === 'random'
        ? (Math.random() < 0.5 ? 'w' : 'b')
        : config.playerColor;

    // orientation = the side the *user* plays
    setOrientation(resolvedColor);
    setDifficulty(config.difficulty);

    analysisFenRef.current = null;
    resetGame();
    resetEvalHistory();
    resetGrades();
    lastNormalizedEvalRef.current = null;
    newGame();
  };

  // ── Normalized eval (always White perspective) ────────────────────────────
  const normalizedInfo = engineInfo ? {
    ...engineInfo,
    pvs: engineInfo.pvs.map(p => {
      const p2 = { ...p };
      if (engineColor === 'b') {
        if (p2.score !== undefined) p2.score = -p2.score;
        if (p2.mate !== undefined) p2.mate = -p2.mate;
      }
      return p2;
    })
  } : null;

  // ── Check square highlight ────────────────────────────────────────────────
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

  // ── Mode change handler ───────────────────────────────────────────────────
  const handleModeChange = (newMode: GameMode) => {
    setGameMode(newMode);
    // When switching into analysis, we don't reset — user keeps their position.
    // When switching out of analysis, start fresh so engine state is clean.
    if (gameMode === 'analysis' && newMode !== 'analysis') {
      handleNewGame();
    }
  };

  return (
    <div className="h-screen overflow-hidden flex flex-col">
      <Header />

      <main className="flex-1 min-h-0 overflow-hidden">
        <div className="h-full max-w-[1500px] w-full mx-auto px-4 md:px-6 py-4 grid grid-cols-1 lg:grid-cols-[1fr_540px] gap-6">

          {/* ── Left Column — Board ─────────────────────────────────── */}
          <div className="flex gap-4 items-center min-h-0 justify-center">

            {/* Eval Bar — hidden in Fair Play */}
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

              {/* Mode badge overlay (top-left of board) */}
              <div className="absolute top-2 left-2 z-10 pointer-events-none">
                {isFairPlay  && <span className="bg-black/60 text-white text-[9px] font-bold uppercase tracking-widest px-2 py-0.5 rounded-full backdrop-blur-sm">🛡️ Fair Play</span>}
                {isTraining  && <span className="bg-black/60 text-primary text-[9px] font-bold uppercase tracking-widest px-2 py-0.5 rounded-full backdrop-blur-sm">🧠 Training</span>}
                {isAnalysis  && <span className="bg-black/60 text-accent text-[9px] font-bold uppercase tracking-widest px-2 py-0.5 rounded-full backdrop-blur-sm">🔍 Analysis</span>}
              </div>

              {/* Analysis mode — both-sides-enabled notice */}
              {isAnalysis && (
                <div className="absolute bottom-2 left-1/2 -translate-x-1/2 z-10 pointer-events-none">
                  <span className="bg-black/60 text-accent text-[9px] font-semibold px-2.5 py-1 rounded-full backdrop-blur-sm">
                    Both sides enabled
                  </span>
                </div>
              )}

              {/* Game over overlay */}
              {isGameOver && !isAnalysis && (
                <div className="absolute inset-0 bg-background/70 backdrop-blur-sm z-50 flex flex-col items-center justify-center rounded-md">
                  <h2 className="text-3xl font-bold text-foreground mb-2">Game Over</h2>
                  <p className="text-lg text-muted-foreground mb-6 font-medium">
                    {game.isCheckmate()
                      ? (turn === 'w' ? 'Black Wins by Checkmate' : 'White Wins by Checkmate')
                      : game.isStalemate()           ? 'Draw by Stalemate'
                      : game.isThreefoldRepetition() ? 'Draw by Repetition'
                      : game.isInsufficientMaterial()? 'Draw by Insufficient Material'
                      : game.isDraw()                ? 'Draw'
                      :                                'Game Over'}
                  </p>
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

            {/* Mode selector — always pinned at top */}
            <ModeSelector mode={gameMode} onModeChange={handleModeChange} />

            {/* FEN input — Analysis only */}
            {showFenInput && (
              <FenInput onLoadFen={handleLoadFen} onReset={handleFenReset} />
            )}

            {/* Engine configuration — hidden in Fair Play */}
            {showEngineConf && (
              <EngineToggle currentVersion="Texel-Tuned HCE" />
            )}

            {/* Engine analysis panel — hidden in Fair Play */}
            {showEnginePanel && (
              <EnginePanel info={normalizedInfo} status={status} queuePosition={queuePosition} />
            )}

            {/* Move history — always visible; badges shown in Training/Analysis only */}
            <MoveHistory moves={moveHistory} grades={grades} showGrades={showEnginePanel} />

            {/* Evaluation Graph — shown in Analysis and Training */}
            {showEnginePanel && (
              <EvalGraph data={evalGraphData} />
            )}

            {/* PGN Controls — shown only in Analysis mode */}
            {isAnalysis && (
              <PgnControls 
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

            {/* Game controls — always pinned to bottom */}
            <div className="shrink-0">
              <GameControls
                onNewGameClick={handleNewGame}
                onUndo={undoMove}
                onFlipBoard={() => setOrientation(o => o === 'w' ? 'b' : 'w')}
                orientation={orientation}
                canUndo={moveHistory.length > 0 && status !== 'thinking'}
              />
            </div>
          </div>

        </div>
      </main>

      <Footer />

      {/* Match Settings Modal — mounted at root so it overlays the full viewport */}
      <NewGameModal
        isOpen={isNewGameModalOpen}
        defaultDifficulty={difficulty}
        defaultPlayerColor={orientation}
        onStart={handleStartNewGame}
        onCancel={() => setIsNewGameModalOpen(false)}
      />
    </div>
  );
}
