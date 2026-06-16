"use client";

import { useState } from 'react';
import { Chess } from 'chess.js';
import { useToast } from '../ui/Toast';

interface PositionSetupProps {
  currentFen: string;
  onLoadFen: (fen: string) => void;
  onReset: () => void;
  exportPgn: () => string;
  loadPgn: (pgn: string) => string | false;
  onImportSuccess?: (fen: string) => void;
}

const STARTING_FEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';

function isValidFen(fen: string): boolean {
  try { new Chess(fen); return true; } catch { return false; }
}

type Tab = 'fen' | 'pgn';

export function PositionSetup({
  currentFen,
  onLoadFen,
  onReset,
  exportPgn,
  loadPgn,
  onImportSuccess,
}: PositionSetupProps) {
  const [activeTab, setActiveTab] = useState<Tab>('fen');

  // FEN state
  const [fenValue, setFenValue] = useState('');
  const [fenError, setFenError] = useState<string | null>(null);
  const [fenLoaded, setFenLoaded] = useState(false);

  // PGN state
  const [pgnValue, setPgnValue] = useState('');

  const { addToast } = useToast();

  // ── FEN handlers ────────────────────────────────────────────────────────────
  const handleFenLoad = () => {
    const trimmed = fenValue.trim();
    if (!trimmed) { setFenError('Enter a FEN string.'); return; }
    if (!isValidFen(trimmed)) {
      setFenError('Invalid FEN — check piece placement, castling rights, and en-passant square.');
      setFenLoaded(false);
      return;
    }
    setFenError(null);
    setFenLoaded(true);
    onLoadFen(trimmed);
  };

  const handleFenReset = () => {
    setFenValue('');
    setFenError(null);
    setFenLoaded(false);
    onReset();
  };

  // ── PGN handlers ────────────────────────────────────────────────────────────
  const handlePgnImport = () => {
    if (!pgnValue.trim()) return;
    const finalFen = loadPgn(pgnValue);
    if (finalFen === false) {
      addToast('Invalid PGN format.', 'error');
    } else {
      setPgnValue('');
      addToast('PGN loaded successfully.', 'info');
      onImportSuccess?.(finalFen);
    }
  };

  const handlePgnCopy = () => {
    const pgn = exportPgn();
    if (!pgn?.trim()) { addToast('No moves to copy.', 'warning'); return; }
    navigator.clipboard.writeText(pgn)
      .then(() => addToast('PGN copied to clipboard!', 'info'))
      .catch(() => addToast('Failed to copy PGN.', 'error'));
  };

  return (
    <div className="bg-surface rounded-lg border border-white/10 shadow-md overflow-hidden shrink-0">
      {/* Tab bar */}
      <div className="flex border-b border-white/10">
        {(['fen', 'pgn'] as Tab[]).map(tab => (
          <button
            key={tab}
            onClick={() => setActiveTab(tab)}
            className={`
              flex-1 py-2 text-[11px] font-bold uppercase tracking-widest transition-colors duration-150
              ${activeTab === tab
                ? 'text-primary border-b-2 border-primary bg-primary/5'
                : 'text-muted hover:text-foreground border-b-2 border-transparent'}
            `}
          >
            {tab === 'fen' ? '🧩 FEN' : '📋 PGN'}
          </button>
        ))}
      </div>

      <div className="p-5 space-y-3">
        {/* ── FEN Tab ─────────────────────────────────────────────────────── */}
        {activeTab === 'fen' && (
          <>
            <div className="relative">
              <textarea
                value={fenValue}
                onChange={e => { setFenValue(e.target.value.replace(/\n/g, '')); setFenError(null); setFenLoaded(false); }}
                onKeyDown={e => { 
                  if (e.key === 'Enter') { e.preventDefault(); handleFenLoad(); }
                  if (e.key === 'Escape') handleFenReset(); 
                }}
                placeholder={STARTING_FEN}
                spellCheck={false}
                className={`
                  w-full min-h-[100px] resize-none bg-background text-foreground text-[11px] font-mono
                  py-2 px-3 rounded-md border outline-none
                  placeholder:text-muted/40 transition-all duration-150
                  ${fenError
                    ? 'border-red-500/60 focus:border-red-500 focus:ring-1 focus:ring-red-500/30'
                    : fenLoaded
                      ? 'border-primary/50 focus:border-primary focus:ring-1 focus:ring-primary/30'
                      : 'border-white/10 focus:border-primary/50 focus:ring-1 focus:ring-primary/20'
                  }
                `}
              />
              {(fenError || fenLoaded) && (
                <span className={`absolute right-2.5 top-1/2 -translate-y-1/2 text-sm ${fenError ? 'text-red-400' : 'text-primary'}`}>
                  {fenError ? '✗' : '✓'}
                </span>
              )}
            </div>
            <p className="text-[9px] text-muted/50 leading-snug">
              Press <kbd className="font-mono bg-surface-elevated px-1 py-0.5 rounded text-muted/70">Enter</kbd> to load,{' '}
              <kbd className="font-mono bg-surface-elevated px-1 py-0.5 rounded text-muted/70">Esc</kbd> to reset.
            </p>
            {fenError && <p className="text-[10px] text-red-400 leading-snug">{fenError}</p>}
            <div className="flex gap-2">
              <button
                onClick={handleFenLoad}
                className="flex-[2] text-xs font-semibold py-1.5 px-3 rounded-md bg-primary/20 text-primary hover:bg-primary/30 border border-primary/20 transition-colors duration-150"
              >
                Load Position
              </button>
              <button
                onClick={() => {
                  navigator.clipboard.writeText(currentFen)
                    .then(() => addToast('FEN copied to clipboard!', 'info'))
                    .catch(() => addToast('Failed to copy FEN.', 'error'));
                }}
                className="flex-[2] text-xs font-semibold py-1.5 px-3 rounded-md bg-surface-elevated text-muted hover:text-foreground border border-white/10 transition-colors duration-150"
              >
                Copy FEN
              </button>
              <button
                onClick={handleFenReset}
                className="flex-1 text-xs font-semibold py-1.5 px-3 rounded-md bg-surface-elevated text-red-400/80 hover:text-red-400 border border-white/10 transition-colors duration-150"
              >
                Reset
              </button>
            </div>
          </>
        )}

        {/* ── PGN Tab ─────────────────────────────────────────────────────── */}
        {activeTab === 'pgn' && (
          <>
            <textarea
              value={pgnValue}
              onChange={e => setPgnValue(e.target.value)}
              placeholder="Paste PGN here…"
              spellCheck={false}
              className="w-full min-h-[120px] bg-background border border-white/10 rounded-md px-3 py-2 text-[11px] font-mono text-foreground placeholder:text-muted/40 focus:outline-none focus:border-primary/50 transition-colors resize-none"
            />
            <div className="flex gap-2">
              <button
                onClick={handlePgnImport}
                disabled={!pgnValue.trim()}
                className="flex-1 text-xs font-semibold py-1.5 px-3 rounded-md bg-primary/20 text-primary hover:bg-primary/30 border border-primary/20 transition-colors duration-150 disabled:opacity-40 disabled:cursor-not-allowed"
              >
                Import PGN
              </button>
              <button
                onClick={handlePgnCopy}
                className="text-xs font-semibold py-1.5 px-3 rounded-md bg-surface-elevated text-muted hover:text-foreground border border-white/10 transition-colors duration-150"
              >
                Copy PGN
              </button>
            </div>
          </>
        )}
      </div>
    </div>
  );
}
