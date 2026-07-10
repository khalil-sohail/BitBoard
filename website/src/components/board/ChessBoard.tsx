"use client";

import { useState, useMemo, type CSSProperties } from 'react';
import { Chessboard, type Arrow, type PieceDropHandlerArgs, type SquareHandlerArgs } from 'react-chessboard';
import { convertUciToArrow } from '../../lib/square-utils';
import { PVLine } from '../../types/engine';

// Classic chess.com wood palette — hard-coded for guaranteed contrast
// regardless of CSS variable resolution context.
const BOARD_LIGHT  = '#f0d9b5';
const BOARD_DARK   = '#b58863';
const HIGHLIGHT_BG = 'rgba(20, 85, 30, 0.5)';  // selected square
const RCLICK_BG    = 'rgba(235, 97, 80, 0.8)';  // right-click annotation

interface BoardProps {
  fen: string;
  pvs: PVLine[] | undefined;
  onMove: (move: { from: string; to: string; promotion?: string }) => boolean;
  orientation?: 'white' | 'black';
  checkSquare?: string | null;
  lastMove?: { from: string; to: string } | null;
}

export function ChessBoardComponent({ fen, pvs, onMove, orientation = 'white', checkSquare, lastMove }: BoardProps) {
  const [moveFrom, setMoveFrom] = useState<string | null>(null);
  const [rightClickedSquares, setRightClickedSquares] = useState<Record<string, CSSProperties>>({});

  // Draw custom arrows for top 3 PVs
  const customArrows = useMemo(() => {
     if (!pvs || pvs.length === 0) return [];
     
     const colors = [
       'rgba(34, 197, 94, 0.8)',  // MultiPV 1 (Best): Green
       'rgba(59, 130, 246, 0.8)', // MultiPV 2: Blue
       'rgba(234, 179, 8, 0.8)'   // MultiPV 3: Yellow/Orange
     ];
     
     const arrows: Arrow[] = [];
     for (let i = 0; i < Math.min(pvs.length, 3); i++) {
        const pvLine = pvs[i].pv;
        if (pvLine && pvLine.length > 0) {
            const uciMove = pvLine[0]; // just the first move
            const arrow = convertUciToArrow(uciMove, colors[i]);
            if (arrow) {
               arrows.push({
                   startSquare: arrow[0],
                   endSquare: arrow[1],
                   color: arrow[2]
               });
            }
        }
     }
     return arrows;
  }, [pvs]);

  function onSquareClick({ square }: SquareHandlerArgs) {
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

  function onSquareRightClick({ square }: SquareHandlerArgs) {
    setRightClickedSquares((prev) => {
      if (prev[square]?.backgroundColor === RCLICK_BG) {
        const rest = { ...prev };
        delete rest[square];
        return rest;
      }

      return {
        ...prev,
        [square]: { backgroundColor: RCLICK_BG },
      };
    });
  }

  function onDrop({ sourceSquare, targetSquare, piece }: PieceDropHandlerArgs) {
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
    <div className="w-full h-full relative rounded-xl overflow-hidden shadow-[0_25px_60px_rgba(0,0,0,0.7)] ring-1 ring-white/5">
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
