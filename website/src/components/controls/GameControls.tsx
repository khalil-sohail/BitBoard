"use client";

import { PlayerColor } from '../../types/engine';

interface GameControlsProps {
  /** Opens the New Game configuration modal */
  onNewGameClick: () => void;
  onUndo: () => void;
  onFlipBoard: () => void;
  orientation: PlayerColor;
  canUndo: boolean;
}

export function GameControls({ onNewGameClick, onUndo, onFlipBoard, orientation, canUndo }: GameControlsProps) {
  return (
    <div className="bg-surface rounded-lg border border-white/10 p-4 flex flex-col shadow-md">
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
    </div>
  );
}
