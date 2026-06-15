import { useState, useCallback, useRef } from 'react';
import { Chess, Move } from 'chess.js';
import { PlayerColor } from '../types/engine';

export function useChessGame() {
  const [game, setGame] = useState(new Chess());
  const [moveHistory, setMoveHistory] = useState<Move[]>([]);
  // Store UCI moves to send to engine (e.g. e2e4, g1f3)
  const [uciHistory, setUciHistory] = useState<string[]>([]);
  
  const moveAudioRef = useRef<HTMLAudioElement | null>(null);
  const captureAudioRef = useRef<HTMLAudioElement | null>(null);
  const checkAudioRef = useRef<HTMLAudioElement | null>(null);
  const endAudioRef = useRef<HTMLAudioElement | null>(null);

  // Initialize audio
  if (typeof window !== 'undefined' && !moveAudioRef.current) {
    try {
        moveAudioRef.current = new Audio('https://images.chesscomfiles.com/chess-themes/sounds/_MP3_/default/move-self.mp3');
        captureAudioRef.current = new Audio('https://images.chesscomfiles.com/chess-themes/sounds/_MP3_/default/capture.mp3');
        checkAudioRef.current = new Audio('https://images.chesscomfiles.com/chess-themes/sounds/_MP3_/default/move-check.mp3');
        endAudioRef.current = new Audio('https://images.chesscomfiles.com/chess-themes/sounds/_MP3_/default/game-end.mp3');
    } catch(e) {}
  }

  const playSound = (type: 'move' | 'capture' | 'check' | 'end') => {
    try {
        let audio: HTMLAudioElement | null = null;
        if (type === 'end') audio = endAudioRef.current;
        else if (type === 'check') audio = checkAudioRef.current;
        else if (type === 'capture') audio = captureAudioRef.current;
        else audio = moveAudioRef.current;

        if (audio) {
            audio.currentTime = 0;
            audio.play().catch(e=>e);
        }
    } catch(e) {}
  };

  const makeMove = useCallback((move: { from: string; to: string; promotion?: string }) => {
    try {
      const gameCopy = new Chess(game.fen());
      const result = gameCopy.move(move);

      if (result) {
        setGame(gameCopy);
        // Append the new move to the existing history — gameCopy was constructed
        // from fen() so its internal history only contains this single move.
        setMoveHistory(prev => [...prev, result as Move]);

        const uciMove = result.from + result.to + (result.promotion || '');
        setUciHistory(prev => [...prev, uciMove]);

        if (gameCopy.isGameOver()) playSound('end');
        else if (gameCopy.isCheck()) playSound('check');
        else if (result.flags.includes('c') || result.flags.includes('e')) playSound('capture');
        else playSound('move');

        return true;
      }
    } catch (e) {
      return false;
    }
    return false;
  }, [game]);

  const resetGame = useCallback(() => {
    setGame(new Chess());
    setMoveHistory([]);
    setUciHistory([]);
  }, []);

  const undoMove = useCallback(() => {
    if (game.history().length < 2) return false;

    const gameCopy = new Chess(game.fen());
    gameCopy.undo(); // Undo engine move
    gameCopy.undo(); // Undo player move
    
    setGame(gameCopy);
    setMoveHistory(gameCopy.history({ verbose: true }) as Move[]);
    setUciHistory(prev => prev.slice(0, -2));
    return true;
  }, [game]);

  /**
   * Load a custom FEN position (Analysis Mode).
   * Returns true on success, false if the FEN is invalid.
   */
  const loadFen = useCallback((fen: string): boolean => {
    try {
      const newGame = new Chess(fen);
      setGame(newGame);
      setMoveHistory([]);
      setUciHistory([]);
      return true;
    } catch {
      return false;
    }
  }, []);

  const exportPgn = useCallback((): string => {
    return game.pgn();
  }, [game]);

  const loadPgn = useCallback((pgnString: string): string | false => {
    try {
      const newGame = new Chess();
      newGame.loadPgn(pgnString);
      setGame(newGame);
      setMoveHistory(newGame.history({ verbose: true }) as Move[]);
      
      // Reconstruct uciHistory
      const moves = newGame.history({ verbose: true }) as Move[];
      setUciHistory(moves.map(m => m.from + m.to + (m.promotion || '')));
      
      return newGame.fen();
    } catch {
      return false;
    }
  }, []);

  return {
    game,
    fen: game.fen(),
    moveHistory,
    uciHistory,
    makeMove,
    resetGame,
    undoMove,
    loadFen,
    exportPgn,
    loadPgn,
    isGameOver: game.isGameOver(),
    turn: game.turn() as PlayerColor
  };
}
