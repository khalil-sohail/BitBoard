"use client";

import { useEffect, useState } from "react";
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
import { PlayerColor, DifficultyLevel } from "@/types/engine";

export default function Home() {
  const { status, engineInfo, bestMove, queuePosition, sendMove, newGame } = useEngine();
  const { game, fen, moveHistory, uciHistory, makeMove, resetGame, undoMove, turn, isGameOver } = useChessGame();
  const { evalHistory, addEvalPoint, resetEvalHistory } = useEvalHistory();
  const [orientation, setOrientation] = useState<PlayerColor>('w');
  const [difficulty, setDifficulty] = useState<DifficultyLevel>('standard');

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
      }
    }
  }, [bestMove, turn, makeMove, addEvalPoint, moveHistory.length, engineInfo?.score]);

  const handleUserMove = (move: { from: string; to: string; promotion?: string }) => {
    if (turn === engineColor) return false; // Prevent moves while engine is thinking
    return makeMove(move);
  };

  const handleNewGame = () => {
    resetGame();
    resetEvalHistory();
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
    <div className="min-h-screen max-h-screen flex flex-col">
      <Header />
      
      <main className="flex-1 max-w-7xl w-full mx-auto p-4 md:p-6 flex flex-col gap-12">
        <div className="grid grid-cols-1 lg:grid-cols-[1fr_380px] gap-8">
          
          {/* Left Column - Board area */}
          <div className="flex gap-4">
            <EvalBar 
              evalScore={normalizedInfo?.score ?? 0} 
              mate={normalizedInfo?.mate}
              turn={turn} 
              orientation={orientation}
            />
            <div className="flex-1 max-w-[700px] relative">
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

          {/* Right Column - Sidebar */}
          <div className="flex flex-col h-[700px]">
            <EngineToggle currentVersion="Texel-Tuned HCE" />
            <EnginePanel info={normalizedInfo} status={status} queuePosition={queuePosition} />
            <MoveHistory moves={moveHistory} />
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

        {/* Bottom Area - Graph */}
        {/* <EvalGraph data={evalHistory} /> */}
        
      </main>

      <Footer />
    </div>
  );
}
