"use client";

import { useEffect, useId, useMemo, useRef, useState, type CSSProperties } from 'react';
import { Chessboard, defaultPieces, type Arrow, type PieceDropHandlerArgs, type PieceRenderObject, type SquareHandlerArgs } from 'react-chessboard';
import type { ProgressiveHintView } from '../../lib/training-hint';
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
  arrows?: Arrow[];
  onMove: (move: { from: string; to: string; promotion?: string }) => boolean;
  onPromotionPending?: (promotion: PendingPromotion) => void;
  onPromotionSelected?: (piece: PromotionPiece) => void;
  onPromotionCancelled?: () => void;
  disabled?: boolean;
  orientation?: 'white' | 'black';
  checkSquare?: string | null;
  lastMove?: { from: string; to: string } | null;
  hintView?: ProgressiveHintView | null;
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

const PIECE_NAMES: Record<string, string> = { P: 'pawn', N: 'knight', B: 'bishop', R: 'rook', Q: 'queen', K: 'king' };
const ACCESSIBLE_PIECES = Object.fromEntries(Object.entries(defaultPieces).map(([pieceCode, renderPiece]) => [
  pieceCode,
  (props?: { fill?: string; square?: string; svgStyle?: CSSProperties }) => <>
    {renderPiece(props)}
    <span className="sr-only">{pieceCode[0] === 'w' ? 'White' : 'Black'} {PIECE_NAMES[pieceCode[1]]} on {props?.square ?? 'the board'}</span>
  </>,
])) as PieceRenderObject;

export function ChessBoardComponent({
  fen,
  arrows = [],
  onMove,
  onPromotionPending,
  onPromotionSelected,
  onPromotionCancelled,
  disabled = false,
  orientation = 'white',
  checkSquare,
  lastMove,
  hintView,
}: BoardProps) {
  const [moveFrom, setMoveFrom] = useState<string | null>(null);
  const [pendingPromotion, setPendingPromotion] = useState<PendingPromotion | null>(null);
  const [rightClickedSquares, setRightClickedSquares] = useState<Record<string, CSSProperties>>({});
  const promotionTitleId = useId();
  const boardInteractionRef = useRef<HTMLDivElement>(null);
  const promotionDialogRef = useRef<HTMLDivElement>(null);
  const promotionTriggerRef = useRef<HTMLElement | null>(null);

  useEffect(() => {
    if (!pendingPromotion) return;
    const outsideRegions = [
      document.querySelector<HTMLElement>('[data-product-app-shell] > header'),
      document.querySelector<HTMLElement>('[data-responsive-session-panel]'),
      document.querySelector<HTMLElement>('[data-product-app-shell] > footer'),
    ].filter((element): element is HTMLElement => Boolean(element));
    const boardInteraction = boardInteractionRef.current;
    const previousOverflow = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    if (boardInteraction) boardInteraction.inert = true;
    for (const region of outsideRegions) region.inert = true;
    const frame = requestAnimationFrame(() => promotionDialogRef.current?.querySelector<HTMLButtonElement>('button')?.focus());
    return () => {
      cancelAnimationFrame(frame);
      document.body.style.overflow = previousOverflow;
      if (boardInteraction) boardInteraction.inert = false;
      for (const region of outsideRegions) region.inert = false;
      if (promotionTriggerRef.current?.isConnected) promotionTriggerRef.current.focus();
    };
  }, [pendingPromotion]);

  const hintSquareStyles = useMemo<Record<string, CSSProperties>>(() => {
    if (!hintView?.from) return {};
    const styles: Record<string, CSSProperties> = {
      [hintView.from]: { boxShadow: 'inset 0 0 0 4px rgba(244, 114, 182, 0.85)' },
    };
    if (hintView.to && hintView.level >= 2) {
      styles[hintView.to] = { boxShadow: 'inset 0 0 0 4px rgba(244, 114, 182, 0.7)' };
    }
    return styles;
  }, [hintView]);

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
      promotionTriggerRef.current = document.activeElement instanceof HTMLElement ? document.activeElement : null;
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
      promotionTriggerRef.current = document.activeElement instanceof HTMLElement ? document.activeElement : null;
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
      <div ref={boardInteractionRef} className="h-full w-full">
        <Chessboard
          options={{
          pieces: ACCESSIBLE_PIECES,
          position: fen,
          onPieceDrop: onDrop,
          onSquareClick: onSquareClick,
          onSquareRightClick: onSquareRightClick,
          boardOrientation: orientation,
          /* Hard-coded classic wood palette for guaranteed visibility */
          darkSquareStyle:  { backgroundColor: BOARD_DARK },
          lightSquareStyle: { backgroundColor: BOARD_LIGHT },
          arrows,
          squareStyles: {
            ...rightClickedSquares,
            ...(lastMove && {
              [lastMove.from]: { backgroundColor: 'rgba(255, 255, 0, 0.4)' },
              [lastMove.to]: { backgroundColor: 'rgba(255, 255, 0, 0.4)' },
            }),
            ...(moveFrom && {
              [moveFrom]: { backgroundColor: HIGHLIGHT_BG },
            }),
            ...hintSquareStyles,
            ...(checkSquare && {
              [checkSquare]: { 
                background: 'radial-gradient(circle, rgba(255,0,0,0.8) 0%, rgba(255,0,0,0) 80%)' 
              },
            }),
          },
          }}
        />
      </div>
      {pendingPromotion && (
        <div className="absolute inset-0 z-20 flex items-center justify-center bg-black/45 backdrop-blur-sm">
          <div ref={promotionDialogRef} role="dialog" aria-modal="true" aria-labelledby={promotionTitleId} onKeyDown={event => {
            if (event.key === 'Escape') {
              event.preventDefault();
              cancelPromotion();
              return;
            }
            if (event.key !== 'Tab' || !promotionDialogRef.current) return;
            const focusable = [...promotionDialogRef.current.querySelectorAll<HTMLButtonElement>('button:not([disabled])')];
            const first = focusable[0];
            const last = focusable[focusable.length - 1];
            if (event.shiftKey && document.activeElement === first) {
              event.preventDefault();
              last?.focus();
            } else if (!event.shiftKey && document.activeElement === last) {
              event.preventDefault();
              first?.focus();
            }
          }} className="rounded-lg border border-white/15 bg-zinc-950/95 p-3 shadow-2xl">
            <h2 id={promotionTitleId} className="sr-only">Choose promotion piece</h2>
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
              className="mt-3 min-h-11 w-full rounded-md border border-white/10 px-3 py-1.5 text-xs font-semibold text-muted hover:bg-white/5 hover:text-foreground focus:outline-none focus:ring-2 focus:ring-primary"
            >
              Cancel
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
