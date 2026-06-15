"use client";

import { PlayerColor } from "../../types/engine";

// ── Material helpers ──────────────────────────────────────────────────────────

const PIECE_VALUES: Record<string, number> = {
  p: 1, n: 3, b: 3, r: 5, q: 9,
};

/** Piece icons for display (coloured Unicode chess symbols). */
const PIECE_ICONS: Record<string, { white: string; black: string }> = {
  q: { white: '♛', black: '♛' },
  r: { white: '♜', black: '♜' },
  b: { white: '♝', black: '♝' },
  n: { white: '♞', black: '♞' },
  p: { white: '♟', black: '♟' },
};

interface MaterialInfo {
  whiteMaterial: number;
  blackMaterial: number;
  whiteCaptures: Record<string, number>; // black pieces captured by white
  blackCaptures: Record<string, number>; // white pieces captured by black
}

/** Parse a FEN string and compute material imbalance. */
function computeMaterial(fen: string): MaterialInfo {
  const placement = fen.split(' ')[0];
  const counts: Record<string, number> = { P: 0, N: 0, B: 0, R: 0, Q: 0, p: 0, n: 0, b: 0, r: 0, q: 0 };
  const starting: Record<string, number> = { P: 8, N: 2, B: 2, R: 2, Q: 1, p: 8, n: 2, b: 2, r: 2, q: 1 };

  for (const ch of placement) {
    if (ch in counts) counts[ch]++;
  }

  // How many of each piece type have been captured
  const whiteCaptures: Record<string, number> = {}; // white captures = missing black pieces
  const blackCaptures: Record<string, number> = {}; // black captures = missing white pieces

  for (const [key, start] of Object.entries(starting)) {
    const missing = start - (counts[key] ?? 0);
    if (missing > 0) {
      if (key === key.toLowerCase()) {
        // Missing black piece → white captured it
        whiteCaptures[key] = missing;
      } else {
        // Missing white piece → black captured it
        blackCaptures[key.toLowerCase()] = missing;
      }
    }
  }

  let whiteMaterial = 0, blackMaterial = 0;
  for (const [piece, val] of Object.entries(PIECE_VALUES)) {
    whiteMaterial += (whiteCaptures[piece] ?? 0) * val;
    blackMaterial += (blackCaptures[piece] ?? 0) * val;
  }

  return { whiteMaterial, blackMaterial, whiteCaptures, blackCaptures };
}

function CapturedBar({
  captures,
  advantage,
  side,
}: {
  captures: Record<string, number>;
  advantage: number;
  side: 'w' | 'b';
}) {
  const pieces = Object.entries(captures).flatMap(([p, n]) => Array(n).fill(p));
  if (pieces.length === 0 && advantage <= 0) return null;

  return (
    <div className="flex items-center gap-0.5 min-h-[14px]">
      <span className="text-[11px] text-muted/70 leading-none tracking-tighter">
        {pieces.map((p, i) => (
          <span key={i} className="opacity-60">{PIECE_ICONS[p]?.[side === 'w' ? 'black' : 'white'] ?? ''}</span>
        ))}
      </span>
      {advantage > 0 && (
        <span className="text-[10px] font-bold text-primary/80 ml-0.5">+{advantage}</span>
      )}
    </div>
  );
}

// ── Clock formatting ──────────────────────────────────────────────────────────

function formatMs(ms: number): string {
  const totalSeconds = Math.ceil(ms / 1000);
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  if (minutes >= 60) {
    const hours = Math.floor(minutes / 60);
    const mins  = minutes % 60;
    return `${hours}:${String(mins).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
  }
  return `${minutes}:${String(seconds).padStart(2, '0')}`;
}

// ── ClockFace ────────────────────────────────────────────────────────────────

function ClockFace({
  ms,
  isActive,
  label,
  disabled,
  captures,
  advantage,
  side,
}: {
  ms: number;
  isActive: boolean;
  label: string;
  disabled?: boolean;
  captures: Record<string, number>;
  advantage: number;
  side: 'w' | 'b';
}) {
  const isLow      = ms < 10_000 && !disabled;
  const isCritical = ms < 5_000  && !disabled;

  return (
    <div
      className={`
        flex flex-col items-center justify-center rounded-lg px-3 py-2 min-w-[90px] transition-all duration-200 select-none
        ${disabled
          ? 'bg-white/5 border border-white/10 opacity-40'
          : isActive
            ? isCritical
              ? 'bg-red-600/30 border-2 border-red-500 shadow-lg shadow-red-900/40 animate-pulse'
              : isLow
                ? 'bg-amber-600/20 border-2 border-amber-500/70 shadow-md'
                : 'bg-primary/20 border-2 border-primary/60 shadow-md shadow-primary/20'
            : 'bg-white/5 border border-white/10 opacity-70'
        }
      `}
    >
      <span className="text-[9px] font-semibold uppercase tracking-widest text-muted mb-0.5">
        {label}
      </span>
      <span
        className={`
          font-mono font-bold tabular-nums leading-none
          ${disabled ? 'text-muted text-base' : isCritical ? 'text-red-400 text-lg' : isLow ? 'text-amber-400 text-lg' : isActive ? 'text-foreground text-lg' : 'text-muted text-base'}
        `}
      >
        {disabled ? '∞' : formatMs(ms)}
      </span>
      <span className={`mt-0.5 w-1.5 h-1.5 rounded-full transition-all duration-300 ${isActive && !disabled ? (isCritical ? 'bg-red-400 animate-ping' : 'bg-primary') : 'bg-transparent'}`} />
      <CapturedBar captures={captures} advantage={advantage} side={side} />
    </div>
  );
}

// ── ClockDisplay (public) ────────────────────────────────────────────────────

interface ClockDisplayProps {
  whiteMs: number;
  blackMs: number;
  activeSide: PlayerColor | null;
  /** The color the human player is playing as. */
  playerColor: PlayerColor;
  isRunning: boolean;
  /**
   * Only show the active (green/pulsing) styling when the game is live.
   * If false (idle / completed) both clocks render as neutral.
   */
  isGameActive: boolean;
  /** Current board FEN — used for captured piece computation. */
  fen?: string;
  /** True when no time control is active (∞ mode). */
  disabled?: boolean;
}

export function ClockDisplay({
  whiteMs,
  blackMs,
  activeSide,
  playerColor,
  isRunning,
  isGameActive,
  fen = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',
  disabled = false,
}: ClockDisplayProps) {
  const engineColor: PlayerColor = playerColor === 'w' ? 'b' : 'w';

  // Top = engine, bottom = player
  const topColor    = engineColor;
  const bottomColor = playerColor;

  const topMs    = topColor    === 'w' ? whiteMs : blackMs;
  const bottomMs = bottomColor === 'w' ? whiteMs : blackMs;

  // Material
  const mat = computeMaterial(fen);
  const whiteAdv = Math.max(0, mat.whiteMaterial - mat.blackMaterial);
  const blackAdv = Math.max(0, mat.blackMaterial - mat.whiteMaterial);

  const topCaptures    = topColor    === 'w' ? mat.whiteCaptures : mat.blackCaptures;
  const bottomCaptures = bottomColor === 'w' ? mat.whiteCaptures : mat.blackCaptures;
  const topAdv         = topColor    === 'w' ? whiteAdv          : blackAdv;
  const bottomAdv      = bottomColor === 'w' ? whiteAdv          : blackAdv;

  // Gate: clocks only show active styling when the game is live
  const topActive    = isGameActive && isRunning && activeSide === topColor;
  const bottomActive = isGameActive && isRunning && activeSide === bottomColor;

  return (
    <div
      id="chess-clock-display"
      className="flex items-center justify-between gap-2 px-3 py-2 rounded-xl bg-surface-elevated border border-white/10 shadow-inner"
    >
      {/* Engine clock */}
      <ClockFace
        ms={topMs}
        isActive={topActive}
        label={topColor === 'w' ? '♔ Engine' : '♚ Engine'}
        disabled={disabled}
        captures={topCaptures}
        advantage={topAdv}
        side={topColor}
      />

      {/* Centre divider */}
      <div className="flex flex-col items-center gap-1">
        <div className="w-px h-4 bg-white/15 rounded" />
        <span className="text-[9px] font-bold text-muted/50 uppercase tracking-widest">vs</span>
        <div className="w-px h-4 bg-white/15 rounded" />
      </div>

      {/* Player clock */}
      <ClockFace
        ms={bottomMs}
        isActive={bottomActive}
        label={bottomColor === 'w' ? '♔ You' : '♚ You'}
        disabled={disabled}
        captures={bottomCaptures}
        advantage={bottomAdv}
        side={bottomColor}
      />
    </div>
  );
}
