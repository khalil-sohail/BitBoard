"use client";

import { useState } from 'react';
import { Chess } from 'chess.js';

interface FenInputProps {
  onLoadFen: (fen: string) => void;
  onReset: () => void;
}

const STARTING_FEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';

function isValidFen(fen: string): boolean {
  try {
    new Chess(fen);
    return true;
  } catch {
    return false;
  }
}

export function FenInput({ onLoadFen, onReset }: FenInputProps) {
  const [value, setValue] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [loaded, setLoaded] = useState(false);

  const handleLoad = () => {
    const trimmed = value.trim();
    if (!trimmed) {
      setError('Enter a FEN string.');
      return;
    }
    if (!isValidFen(trimmed)) {
      setError('Invalid FEN — check piece placement, castling rights, and en-passant square.');
      setLoaded(false);
      return;
    }
    setError(null);
    setLoaded(true);
    onLoadFen(trimmed);
  };

  const handleReset = () => {
    setValue('');
    setError(null);
    setLoaded(false);
    onReset();
  };

  const handleKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter') handleLoad();
    if (e.key === 'Escape') handleReset();
  };

  return (
    <div className="bg-surface rounded-lg border border-white/10 p-4 shadow-md">
      <h3 className="text-sm font-semibold text-foreground mb-3 uppercase tracking-wider flex items-center gap-2">
        <span>🔍</span>
        Position Setup
      </h3>

      <div className="space-y-2">
        <div className="relative">
          <input
            type="text"
            value={value}
            onChange={(e) => { setValue(e.target.value); setError(null); setLoaded(false); }}
            onKeyDown={handleKeyDown}
            placeholder={STARTING_FEN}
            spellCheck={false}
            className={`
              w-full bg-background text-foreground text-[11px] font-mono
              py-2 px-3 rounded-md border outline-none
              placeholder:text-muted/40 transition-all duration-150
              ${error
                ? 'border-red-500/60 focus:border-red-500 focus:ring-1 focus:ring-red-500/30'
                : loaded
                  ? 'border-primary/50 focus:border-primary focus:ring-1 focus:ring-primary/30'
                  : 'border-white/10 focus:border-primary/50 focus:ring-1 focus:ring-primary/20'
              }
            `}
          />
          {/* Status icon inside input */}
          {(error || loaded) && (
            <span className={`absolute right-2.5 top-1/2 -translate-y-1/2 text-sm ${error ? 'text-red-400' : 'text-primary'}`}>
              {error ? '✗' : '✓'}
            </span>
          )}
        </div>

        {/* Error message */}
        {error && (
          <p className="text-[10px] text-red-400 leading-snug px-0.5">{error}</p>
        )}

        {/* Action buttons */}
        <div className="flex gap-2">
          <button
            onClick={handleLoad}
            className="flex-1 text-xs font-semibold py-1.5 px-3 rounded-md bg-primary/20 text-primary hover:bg-primary/30 border border-primary/20 transition-colors duration-150"
          >
            Load Position
          </button>
          <button
            onClick={handleReset}
            className="text-xs font-semibold py-1.5 px-3 rounded-md bg-surface-elevated text-muted hover:text-foreground border border-white/10 transition-colors duration-150"
          >
            Reset
          </button>
        </div>

        <p className="text-[9px] text-muted/50 leading-snug">
          Press <kbd className="font-mono bg-surface-elevated px-1 py-0.5 rounded text-muted/70">Enter</kbd> to load,{' '}
          <kbd className="font-mono bg-surface-elevated px-1 py-0.5 rounded text-muted/70">Esc</kbd> to reset.
        </p>
      </div>
    </div>
  );
}
