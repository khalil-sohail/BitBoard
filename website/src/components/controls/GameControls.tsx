"use client";

import { PlayerColor, DifficultyLevel } from '../../types/engine';

interface GameControlsProps {
  onNewGame: () => void;
  onUndo: () => void;
  onFlipBoard: () => void;
  orientation: PlayerColor;
  canUndo: boolean;
  difficulty: DifficultyLevel;
  onDifficultyChange: (d: DifficultyLevel) => void;
}

export function GameControls({ onNewGame, onUndo, onFlipBoard, orientation, canUndo, difficulty, onDifficultyChange }: GameControlsProps) {
  return (
    <div className="bg-surface rounded-lg border border-white/10 p-4 flex flex-col shadow-md">
      <div className="flex gap-0 bg-surface-elevated rounded-md shadow-sm border border-white/10">
        <button 
          onClick={onNewGame}
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

      <div className="mt-4 flex flex-col gap-1.5">
        <label className="text-[10px] font-semibold text-muted/70 uppercase tracking-widest">Difficulty</label>
        <select 
          value={difficulty} 
          onChange={(e) => onDifficultyChange(e.target.value as DifficultyLevel)}
          className="w-full bg-background/80 text-foreground text-sm py-2 px-3 rounded-md border border-white/10 outline-none focus:border-primary/50 focus:ring-1 focus:ring-primary/30 transition-all"
        >
          <option value="blitz">Blitz (1s)</option>
          <option value="standard">Standard (3s)</option>
          <option value="deep">Deep Analysis (Depth 8)</option>
        </select>
      </div>
    </div>
  );
}
