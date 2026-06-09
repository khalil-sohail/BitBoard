"use client";

import { useState, useMemo } from 'react';
import { Chessboard } from 'react-chessboard';
import { generatePVArrows } from '../../lib/square-utils';

// Classic chess.com wood palette — hard-coded for guaranteed contrast
// regardless of CSS variable resolution context.
const BOARD_LIGHT  = '#f0d9b5';
const BOARD_DARK   = '#b58863';
const HIGHLIGHT_BG = 'rgba(20, 85, 30, 0.5)';  // selected square
const RCLICK_BG    = 'rgba(235, 97, 80, 0.8)';  // right-click annotation

interface BoardProps {
  fen: string;
  pv: string[] | undefined;
  onMove: (move: { from: string; to: string; promotion?: string }) => boolean;
  orientation?: 'white' | 'black';
  checkSquare?: string | null;
  lastMove?: { from: string; to: string } | null;
}

export function ChessBoardComponent({ fen, pv, onMove, orientation = 'white', checkSquare, lastMove }: BoardProps) {
  const [moveFrom, setMoveFrom] = useState<string | null>(null);
  const [rightClickedSquares, setRightClickedSquares] = useState<any>({});

  const pvArrows = useMemo(() => generatePVArrows(pv), [pv]);
  
  // Combine custom drawn arrows and PV arrows
  const customArrows = useMemo(() => {
     return pvArrows.map(arrow => ({
         startSquare: arrow[0],
         endSquare: arrow[1],
         color: arrow[2]
     }));
  }, [pvArrows]);

  function onSquareClick({ square }: { square: string }) {
    setRightClickedSquares({});

    // From square
    if (!moveFrom) {
      setMoveFrom(square);
      return;
    }

    // To square
    const move = {
      from: moveFrom,
      to: square,
      promotion: 'q', // always promote to queen for simplicity UI-wise, typical in simple integrations
    };

    const isValidMove = onMove(move);
    if (!isValidMove) {
      setMoveFrom(square); // allow selecting a different piece
    } else {
      setMoveFrom(null);
    }
  }

  function onSquareRightClick({ square }: { square: string }) {
    setRightClickedSquares((prev: any) => ({
      ...prev,
      [square]:
        prev[square]?.backgroundColor === RCLICK_BG
          ? undefined
          : { backgroundColor: RCLICK_BG },
    }));
  }

  function onDrop({ sourceSquare, targetSquare, piece }: { sourceSquare: string, targetSquare: string | null, piece: any }) {
    if (!targetSquare) return false;
    
    const move = {
      from: sourceSquare,
      to: targetSquare,
      promotion: piece.pieceType[1].toLowerCase() ?? 'q',
    };

    const isValidMove = onMove(move);
    if (isValidMove) {
      setMoveFrom(null);
      setRightClickedSquares({});
      return true;
    }
    return false;
  }

  return (
    <div className="w-full aspect-square relative rounded-md overflow-hidden shadow-2xl ring-1 ring-black/40">
      <Chessboard
        options={{
          position: fen,
          onPieceDrop: onDrop,
          onSquareClick: onSquareClick,
          onSquareRightClick: onSquareRightClick,
          boardOrientation: orientation,
          /* Hard-coded classic wood palette for guaranteed visibility */
          darkSquareStyle:  { backgroundColor: BOARD_DARK },
          lightSquareStyle: { backgroundColor: BOARD_LIGHT },
          arrows: customArrows,
          squareStyles: {
            ...rightClickedSquares,
            ...(lastMove && {
              [lastMove.from]: { backgroundColor: 'rgba(255, 255, 0, 0.4)' },
              [lastMove.to]: { backgroundColor: 'rgba(255, 255, 0, 0.4)' },
            }),
            ...(moveFrom && {
              [moveFrom]: { backgroundColor: HIGHLIGHT_BG },
            }),
            ...(checkSquare && {
              [checkSquare]: { 
                background: 'radial-gradient(circle, rgba(255,0,0,0.8) 0%, rgba(255,0,0,0) 80%)' 
              },
            }),
          },
        }}
      />
    </div>
  );
}
