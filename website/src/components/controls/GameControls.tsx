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
    <div className="bg-surface rounded-lg border border-border p-4 flex flex-col">
      <div className="flex gap-2">
        <button 
          onClick={onNewGame}
          className="flex-1 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-semibold py-2 rounded-md transition-colors"
        >
          New Game
        </button>
        
        <button 
          onClick={onUndo}
          disabled={!canUndo}
          className="flex-1 bg-surface-elevated hover:bg-surface-elevated/80 disabled:opacity-50 disabled:cursor-not-allowed text-foreground text-sm font-semibold py-2 rounded-md transition-colors border border-border"
        >
          Undo
        </button>
        
        <button 
          onClick={onFlipBoard}
          className="flex-1 bg-surface-elevated hover:bg-surface-elevated/80 text-foreground text-sm font-semibold py-2 rounded-md transition-colors border border-border"
          title={`Current: ${orientation === 'w' ? 'White' : 'Black'}`}
        >
          Flip
        </button>
      </div>

      <div className="flex gap-2 items-center mt-3">
        <label className="text-xs font-semibold text-muted-foreground uppercase tracking-wider">Difficulty:</label>
        <select 
          value={difficulty} 
          onChange={(e) => onDifficultyChange(e.target.value as DifficultyLevel)}
          className="flex-1 bg-surface text-foreground text-sm py-1.5 px-2 rounded border border-border outline-none focus:border-accent"
        >
          <option value="blitz">Blitz (1s)</option>
          <option value="standard">Standard (3s)</option>
          <option value="deep">Deep Analysis (Depth 8)</option>
        </select>
      </div>
    </div>
  );
}
