"use client";

import { useState, useEffect, useCallback } from 'react';
import { DifficultyLevel, PlayerColor, TimeControl, TIME_CONTROLS } from '../../types/engine';

export interface NewGameConfig {
  playerColor: PlayerColor | 'random';
  difficulty: DifficultyLevel;
  timeControl: TimeControl;
}

interface NewGameModalProps {
  isOpen: boolean;
  defaultDifficulty: DifficultyLevel;
  defaultPlayerColor: PlayerColor;
  defaultTimeControl?: TimeControl;
  onStart: (config: NewGameConfig) => void;
  onCancel: () => void;
}

const COLOR_OPTIONS: { id: PlayerColor | 'random'; label: string; icon: string }[] = [
  { id: 'w',      label: 'White',  icon: '♔' },
  { id: 'random', label: 'Random', icon: '⚄' },
  { id: 'b',      label: 'Black',  icon: '♚' },
];

const DIFFICULTY_OPTIONS: { id: DifficultyLevel; label: string; sublabel: string }[] = [
  { id: 'blitz',    label: 'Blitz',    sublabel: '1 sec'  },
  { id: 'standard', label: 'Standard', sublabel: '3 sec'  },
  { id: 'deep',     label: 'Deep',     sublabel: 'D8'     },
];

export function NewGameModal({
  isOpen,
  defaultDifficulty,
  defaultPlayerColor,
  defaultTimeControl,
  onStart,
  onCancel,
}: NewGameModalProps) {
  const [playerColor, setPlayerColor] = useState<PlayerColor | 'random'>('random');
  const [difficulty, setDifficulty]   = useState<DifficultyLevel>(defaultDifficulty);
  const [timeControl, setTimeControl] = useState<TimeControl>(
    defaultTimeControl ?? TIME_CONTROLS[2] // default: 3+0
  );

  // Re-sync defaults whenever the modal opens
  useEffect(() => {
    if (isOpen) {
      setPlayerColor(defaultPlayerColor);
      setDifficulty(defaultDifficulty);
      if (defaultTimeControl) setTimeControl(defaultTimeControl);
    }
  }, [isOpen, defaultDifficulty, defaultPlayerColor, defaultTimeControl]);

  const handleStart = () => {
    onStart({ playerColor, difficulty, timeControl });
  };

  // Close on Escape / confirm on Enter
  const handleKeyDown = useCallback((e: KeyboardEvent) => {
    if (e.key === 'Escape') onCancel();
    if (e.key === 'Enter')  handleStart();
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [onCancel, playerColor, difficulty, timeControl]);

  useEffect(() => {
    if (!isOpen) return;
    document.addEventListener('keydown', handleKeyDown);
    return () => document.removeEventListener('keydown', handleKeyDown);
  }, [isOpen, handleKeyDown]);

  if (!isOpen) return null;

  const hasTC = timeControl.initialMs > 0;

  return (
    /* Backdrop */
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-sm"
      onClick={(e) => { if (e.target === e.currentTarget) onCancel(); }}
    >
      {/* Modal panel */}
      <div className="bg-surface-elevated border border-white/10 rounded-xl shadow-2xl w-full max-w-sm mx-4 overflow-hidden animate-in">

        {/* Header */}
        <div className="px-6 pt-6 pb-4 border-b border-white/10">
          <div className="flex items-center justify-between">
            <div>
              <h2 className="text-lg font-bold text-foreground tracking-tight">New Match</h2>
              <p className="text-xs text-muted mt-0.5">Configure your game settings.</p>
            </div>
            <button
              onClick={onCancel}
              className="w-7 h-7 rounded-full flex items-center justify-center text-muted hover:text-foreground hover:bg-white/10 transition-colors text-sm"
              aria-label="Close"
            >
              ✕
            </button>
          </div>
        </div>

        {/* Body */}
        <div className="px-6 py-5 space-y-5">

          {/* Play As */}
          <div>
            <label className="text-[10px] font-semibold text-muted/70 uppercase tracking-widest block mb-2">
              Play As
            </label>
            <div className="flex gap-2">
              {COLOR_OPTIONS.map((opt) => {
                const isActive = playerColor === opt.id;
                return (
                  <button
                    key={opt.id}
                    onClick={() => setPlayerColor(opt.id)}
                    className={`
                      flex-1 flex flex-col items-center gap-1 py-3 rounded-lg border text-sm font-semibold
                      transition-all duration-150 select-none
                      ${isActive
                        ? 'bg-primary/15 border-primary/40 text-primary shadow-inner'
                        : 'bg-background border-white/10 text-muted hover:text-foreground hover:border-white/25 hover:bg-white/5'
                      }
                    `}
                  >
                    <span className={`text-2xl leading-none ${opt.id === 'w' ? 'text-white' : opt.id === 'b' ? 'text-zinc-400' : 'text-muted'}`}>
                      {opt.icon}
                    </span>
                    <span className="text-[11px] font-medium">{opt.label}</span>
                  </button>
                );
              })}
            </div>
          </div>

          {/* Time Control */}
          <div>
            <label className="text-[10px] font-semibold text-muted/70 uppercase tracking-widest block mb-2">
              Time Control
            </label>
            <div className="grid grid-cols-4 gap-1.5">
              {TIME_CONTROLS.map((tc) => {
                const isActive = timeControl.label === tc.label;
                return (
                  <button
                    key={tc.label}
                    onClick={() => setTimeControl(tc)}
                    className={`
                      flex flex-col items-center justify-center gap-0.5 py-2 rounded-lg border
                      transition-all duration-150 select-none text-center
                      ${isActive
                        ? 'bg-primary/15 border-primary/40 text-primary shadow-inner'
                        : 'bg-background border-white/10 text-muted hover:text-foreground hover:border-white/25 hover:bg-white/5'
                      }
                    `}
                  >
                    <span className="text-[11px] font-bold leading-tight">{tc.label}</span>
                    {tc.initialMs === 0 && (
                      <span className="text-[8px] opacity-50 font-mono">No Clock</span>
                    )}
                  </button>
                );
              })}
            </div>
          </div>

          {/* Engine Strength (only shown when no time control — avoids contradiction) */}
          {!hasTC && (
            <div>
              <label className="text-[10px] font-semibold text-muted/70 uppercase tracking-widest block mb-2">
                Engine Strength
              </label>
              <div className="flex gap-2">
                {DIFFICULTY_OPTIONS.map((opt) => {
                  const isActive = difficulty === opt.id;
                  return (
                    <button
                      key={opt.id}
                      onClick={() => setDifficulty(opt.id)}
                      className={`
                        flex-1 flex flex-col items-center gap-0.5 py-2.5 rounded-lg border
                        transition-all duration-150 select-none
                        ${isActive
                          ? 'bg-primary/15 border-primary/40 text-primary shadow-inner'
                          : 'bg-background border-white/10 text-muted hover:text-foreground hover:border-white/25 hover:bg-white/5'
                        }
                      `}
                    >
                      <span className="text-xs font-bold">{opt.label}</span>
                      <span className="text-[9px] opacity-60 font-mono">{opt.sublabel}</span>
                    </button>
                  );
                })}
              </div>
              <p className="text-[9px] text-muted/50 mt-1.5 text-center">
                Engine strength controls movetime when no clock is active.
              </p>
            </div>
          )}
          {hasTC && (
            <p className="text-[9px] text-muted/50 text-center -mt-2">
              Engine uses the full clock budget — strength is set by time.
            </p>
          )}
        </div>

        {/* Footer actions */}
        <div className="px-6 pb-6 flex gap-3">
          <button
            onClick={onCancel}
            className="flex-1 py-2.5 rounded-lg border border-white/10 text-sm font-semibold text-muted hover:text-foreground hover:border-white/25 hover:bg-white/5 transition-all duration-150"
          >
            Cancel
          </button>
          <button
            onClick={handleStart}
            className="flex-1 py-2.5 rounded-lg bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold shadow-lg shadow-primary/20 transition-all duration-150 active:scale-[0.98]"
          >
            Start Match
          </button>
        </div>
      </div>
    </div>
  );
}
