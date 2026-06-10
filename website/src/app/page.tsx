"use client";

import { useEffect, useRef, useState } from "react";
import { Header } from "@/components/layout/Header";
import { Footer } from "@/components/layout/Footer";
import { ChessBoardComponent } from "@/components/board/ChessBoard";
import { EvalBar } from "@/components/board/EvalBar";
import { EnginePanel } from "@/components/panels/EnginePanel";
import { MoveHistory } from "@/components/panels/MoveHistory";
import { EvalGraph } from "@/components/panels/EvalGraph";
import { EngineToggle } from "@/components/panels/EngineToggle";
import { GameControls } from "@/components/controls/GameControls";
import { useEngine } from "@/hooks/useEngine";
import { useChessGame } from "@/hooks/useChessGame";
import { useEvalHistory } from "@/hooks/useEvalHistory";
import { useMoveReview } from "@/hooks/useMoveReview";
import { PlayerColor, DifficultyLevel } from "@/types/engine";

export default function Home() {
  const { status, engineInfo, bestMove, queuePosition, sendMove, newGame } = useEngine();
  const { game, fen, moveHistory, uciHistory, makeMove, resetGame, undoMove, turn, isGameOver } = useChessGame();
  const { evalHistory, addEvalPoint, resetEvalHistory } = useEvalHistory();
  const { grades, recordEval, resetGrades } = useMoveReview();
  const [orientation, setOrientation] = useState<PlayerColor>('w');
  const [difficulty, setDifficulty] = useState<DifficultyLevel>('standard');

  // Track the last White-normalized eval score so we can compute deltas for user moves
  const lastNormalizedEvalRef = useRef<number | null>(null);

  const engineColor = orientation === 'w' ? 'b' : 'w';

  // Trigger engine move when it's the engine's turn
  useEffect(() => {
    if (turn === engineColor && status === 'ready' && !isGameOver) {
      sendMove(fen, uciHistory, difficulty);
    }
  }, [turn, status, fen, uciHistory, sendMove, difficulty, isGameOver, engineColor]);

  // Apply engine's best move when received
  useEffect(() => {
    if (bestMove && turn === engineColor) {
      const from = bestMove.substring(0, 2);
      const to = bestMove.substring(2, 4);
      const promotion = bestMove.length === 5 ? bestMove[4] : undefined;
      makeMove({ from, to, promotion });
      
      // Add eval to graph (normalized to White's perspective)
      if (engineInfo?.score !== undefined) {
         const scoreToLog = engineColor === 'b' ? -engineInfo.score : engineInfo.score;
         addEvalPoint(moveHistory.length + 1, scoreToLog);

         // Grade the engine's move (the engine just moved as engineColor)
         // moveHistory.length is the index of the move just made (before state update)
         recordEval(
           scoreToLog,
           moveHistory.length,           // 0-based index of the move just applied
           engineColor,
           !!(engineInfo.mate !== undefined && lastNormalizedEvalRef.current !== null),
           !!(engineInfo.mate !== undefined),
         );
         lastNormalizedEvalRef.current = scoreToLog;
      }
    }
  }, [bestMove, turn, makeMove, addEvalPoint, moveHistory.length, engineInfo?.score, engineInfo?.mate, recordEval, engineColor]);

  const handleUserMove = (move: { from: string; to: string; promotion?: string }) => {
    if (turn === engineColor) return false; // Prevent moves while engine is thinking
    const result = makeMove(move);
    // For user moves, the engine hasn't responded yet — we record a provisional grade
    // using the last known eval. The grade will be refined when the engine next reports.
    // We tag this half-move as the user's color (turn before the move)
    if (result && lastNormalizedEvalRef.current !== null) {
      const userColor = turn; // at time of move, turn === user's color
      // delta will be refined on next engine eval; for now record with current known eval
      recordEval(lastNormalizedEvalRef.current, moveHistory.length, userColor);
    }
    return result;
  };

  const handleNewGame = () => {
    resetGame();
    resetEvalHistory();
    resetGrades();
    lastNormalizedEvalRef.current = null;
    newGame();
  };

  // The engine evaluates from its own perspective (the side it is playing)
  // We want to normalize the score so > 0 is always White winning
  const normalizedInfo = engineInfo ? { ...engineInfo } : null;
  if (normalizedInfo) {
    if (engineColor === 'b') {
        if (normalizedInfo.score !== undefined) normalizedInfo.score = -normalizedInfo.score;
        if (normalizedInfo.mate !== undefined) normalizedInfo.mate = -normalizedInfo.mate;
    }
  }

  // Calculate board highlights
  let checkSquare = null;
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
      to: moveHistory[moveHistory.length - 1].to
  } : null;

  return (
    <div className="h-screen overflow-hidden flex flex-col">
      <Header />
      
      <main className="flex-1 min-h-0 overflow-hidden">
        <div className="h-full max-w-7xl w-full mx-auto px-4 md:px-6 py-4 grid grid-cols-1 lg:grid-cols-[1fr_380px] gap-6">
          
          {/* Left Column - Board area */}
          <div className="flex gap-4 items-center h-full min-h-0 justify-center">
            <EvalBar 
              evalScore={normalizedInfo?.score ?? 0} 
              mate={normalizedInfo?.mate}
              turn={turn} 
              orientation={orientation}
            />
            <div className="h-full aspect-square relative flex-shrink-0">
               <ChessBoardComponent 
                 fen={fen} 
                 pv={normalizedInfo?.pv} 
                 onMove={handleUserMove} 
                 orientation={orientation === 'w' ? 'white' : 'black'}
                 checkSquare={checkSquare}
                 lastMove={lastMove}
               />
               
               {isGameOver && (
                 <div className="absolute inset-0 bg-background/70 backdrop-blur-sm z-50 flex flex-col items-center justify-center rounded-md">
                   <h2 className="text-3xl font-bold text-foreground mb-2">Game Over</h2>
                   <p className="text-lg text-muted-foreground mb-6 font-medium">
                     {game.isCheckmate() ? (turn === 'w' ? 'Black Wins by Checkmate' : 'White Wins by Checkmate') :
                      game.isStalemate() ? 'Draw by Stalemate' :
                      game.isThreefoldRepetition() ? 'Draw by Repetition' :
                      game.isInsufficientMaterial() ? 'Draw by Insufficient Material' :
                      game.isDraw() ? 'Draw' : 'Game Over'}
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

          {/* Right Column - Sidebar: scrolls internally, GameControls always pinned to bottom */}
          <div className="flex flex-col gap-3 h-full min-h-0 overflow-hidden">
            <EngineToggle currentVersion="Texel-Tuned HCE" />
            <EnginePanel info={normalizedInfo} status={status} queuePosition={queuePosition} />
            <MoveHistory moves={moveHistory} grades={grades} />
            <div className="shrink-0">
              <GameControls 
                onNewGame={handleNewGame} 
                onUndo={undoMove} 
                onFlipBoard={() => setOrientation(o => o === 'w' ? 'b' : 'w')}
                orientation={orientation}
                canUndo={moveHistory.length > 0 && status !== 'thinking'}
                difficulty={difficulty}
                onDifficultyChange={setDifficulty}
              />
            </div>
          </div>
          
        </div>
      </main>

      <Footer />
    </div>
  );
}
