"use client";

import { PlayerColor } from '../../types/engine';

interface GameControlsProps {
  /** Opens the New Game configuration modal */
  onNewGameClick: () => void;
  onUndo: () => void;
  onFlipBoard: () => void;
  onResign?: () => void;
  orientation: PlayerColor;
  canUndo: boolean;
  /** If false the Resign button is hidden (idle or completed games). */
  canResign?: boolean;
}

export function GameControls({
  onNewGameClick,
  onUndo,
  onFlipBoard,
  onResign,
  orientation,
  canUndo,
  canResign = false,
}: GameControlsProps) {
  return (
    <div className="bg-surface rounded-lg border border-white/10 p-4 flex flex-col gap-2 shadow-md">
      <div className="flex gap-0 bg-surface-elevated rounded-md shadow-sm border border-white/10">
        <button
          onClick={onNewGameClick}
          suppressHydrationWarning
          className="flex-1 text-primary hover:brightness-125 text-sm font-semibold py-2 rounded-l-md transition-all duration-150 border-r border-white/5 bg-transparent"
        >
          New Game
        </button>

        <button
          onClick={onUndo}
          disabled={Boolean(!canUndo)}
          suppressHydrationWarning
          className="flex-1 text-muted-foreground hover:text-foreground disabled:opacity-40 disabled:cursor-not-allowed text-sm font-semibold py-2 rounded-none transition-colors border-r border-white/5 bg-transparent"
        >
          Undo
        </button>

        <button
          onClick={onFlipBoard}
          suppressHydrationWarning
          className="flex-1 text-muted-foreground hover:text-foreground text-sm font-semibold py-2 rounded-r-md transition-colors bg-transparent"
          title={`Current: ${orientation === 'w' ? 'White' : 'Black'}`}
        >
          Flip
        </button>
      </div>

      {/* Resign button — only shown during an active game */}
      {canResign && onResign && (
        <button
          onClick={onResign}
          suppressHydrationWarning
          className="w-full py-1.5 rounded-md border border-red-500/30 text-red-400 hover:bg-red-500/10 hover:border-red-500/60 text-xs font-semibold transition-all duration-150 flex items-center justify-center gap-1.5"
        >
          <span>🏳️</span>
          <span>Resign</span>
        </button>
      )}
    </div>
  );
}
