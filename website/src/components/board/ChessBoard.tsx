"use client";

import { useState, useMemo, type CSSProperties } from 'react';
import { Chessboard, type Arrow, type PieceDropHandlerArgs, type SquareHandlerArgs } from 'react-chessboard';
import { convertUciToArrow } from '../../lib/square-utils';
import { PVLine } from '../../types/engine';
import {
  PROMOTION_PIECES,
  buildPromotionMove,
  isPromotionCandidate,
  type PendingPromotion,
  type PromotionPiece,
} from '../../lib/promotion';

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
  onPromotionPending?: (promotion: PendingPromotion) => void;
  onPromotionSelected?: (piece: PromotionPiece) => void;
  onPromotionCancelled?: () => void;
  disabled?: boolean;
  orientation?: 'white' | 'black';
  checkSquare?: string | null;
  lastMove?: { from: string; to: string } | null;
}

const PROMOTION_LABELS: Record<PromotionPiece, string> = {
  q: 'Queen',
  r: 'Rook',
  b: 'Bishop',
  n: 'Knight',
};

const PROMOTION_SYMBOLS: Record<'w' | 'b', Record<PromotionPiece, string>> = {
  w: { q: '♕', r: '♖', b: '♗', n: '♘' },
  b: { q: '♛', r: '♜', b: '♝', n: '♞' },
};

export function ChessBoardComponent({
  fen,
  pvs,
  onMove,
  onPromotionPending,
  onPromotionSelected,
  onPromotionCancelled,
  disabled = false,
  orientation = 'white',
  checkSquare,
  lastMove,
}: BoardProps) {
  const [moveFrom, setMoveFrom] = useState<string | null>(null);
  const [pendingPromotion, setPendingPromotion] = useState<PendingPromotion | null>(null);
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
    if (disabled || pendingPromotion) return;
    setRightClickedSquares({});

    // From square
    if (!moveFrom) {
      setMoveFrom(square);
      return;
    }

    const promotion = isPromotionCandidate(fen, moveFrom, square);
    if (promotion) {
      onPromotionPending?.(promotion);
      setPendingPromotion(promotion);
      return;
    }

    const isValidMove = onMove({ from: moveFrom, to: square });
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

  function onDrop({ sourceSquare, targetSquare }: PieceDropHandlerArgs) {
    if (disabled || pendingPromotion) return false;
    if (!targetSquare) return false;

    const promotion = isPromotionCandidate(fen, sourceSquare, targetSquare);
    if (promotion) {
      onPromotionPending?.(promotion);
      setPendingPromotion(promotion);
      setMoveFrom(null);
      setRightClickedSquares({});
      return false;
    }

    const move = { from: sourceSquare, to: targetSquare };

    const isValidMove = onMove(move);
    if (isValidMove) {
      setMoveFrom(null);
      setRightClickedSquares({});
      return true;
    }
    return false;
  }

  function choosePromotion(piece: PromotionPiece) {
    if (!pendingPromotion) return;

    onPromotionSelected?.(piece);
    const isValidMove = onMove(buildPromotionMove(pendingPromotion, piece));
    if (isValidMove) {
      setMoveFrom(null);
      setRightClickedSquares({});
      setPendingPromotion(null);
    }
  }

  function cancelPromotion() {
    onPromotionCancelled?.();
    setPendingPromotion(null);
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
      {pendingPromotion && (
        <div className="absolute inset-0 z-20 flex items-center justify-center bg-black/45 backdrop-blur-sm">
          <div className="rounded-lg border border-white/15 bg-zinc-950/95 p-3 shadow-2xl">
            <div className="grid grid-cols-4 gap-2">
              {PROMOTION_PIECES.map((piece) => (
                <button
                  key={piece}
                  type="button"
                  onClick={() => choosePromotion(piece)}
                  className="flex h-14 w-14 items-center justify-center rounded-md border border-white/10 bg-zinc-900 text-3xl text-zinc-100 hover:border-primary/60 hover:bg-primary/20 focus:outline-none focus:ring-2 focus:ring-primary"
                  aria-label={`Promote to ${PROMOTION_LABELS[piece]}`}
                  title={`Promote to ${PROMOTION_LABELS[piece]}`}
                >
                  {PROMOTION_SYMBOLS[pendingPromotion.color][piece]}
                </button>
              ))}
            </div>
            <button
              type="button"
              onClick={cancelPromotion}
              className="mt-3 w-full rounded-md border border-white/10 px-3 py-1.5 text-xs font-semibold text-muted-foreground hover:bg-white/5 hover:text-foreground focus:outline-none focus:ring-2 focus:ring-primary"
            >
              Cancel
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
