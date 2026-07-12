import type { NormalizedEvaluation } from '../lib/engine-evaluation';
import type { EngineDifficulty, SearchPurpose } from '../lib/engine-difficulty';

export interface PVLine {
  multipv: number;
  score?: number;
  mate?: number;
  evaluation?: NormalizedEvaluation;
  pv: string[];
}

export interface EngineInfo {
  requestId?: number;
  rootFen?: string;
  purpose?: SearchPurpose;
  depth: number;
  nodes?: number;
  time?: number; // ms
  pvs: PVLine[];
}

export interface EngineTerminalResult {
  reason: 'checkmate' | 'stalemate' | 'draw' | 'no-legal-move' | 'unknown';
  winner?: 'white' | 'black';
}

export interface EngineTerminalCompletion {
  requestId: number;
  rootFen?: string;
  purpose?: string;
  terminal: EngineTerminalResult;
}

export interface EvalPoint {
  moveNumber: number;
  eval: number; // white's perspective
}

export type PlayerColor = 'w' | 'b';

export type DifficultyLevel = EngineDifficulty;

/**
 * The three operating modes for the chess platform.
 * - fair:     Pure game vs engine. Evaluation UI hidden.
 * - training: Game vs engine with full engine brain visible.
 * - analysis: Free exploration — both sides controlled by user, FEN loading enabled.
 */
export type GameMode = 'fair' | 'training' | 'analysis';

/**
 * A competitive time control configuration.
 * initialMs: starting clock time in milliseconds.
 * incMs:     per-move increment in milliseconds.
 * label:     display label, e.g. "3+2".
 */
export interface TimeControl {
  label: string;
  initialMs: number;
  incMs: number;
}

/** All available time controls including an analysis (unlimited) option. */
export const TIME_CONTROLS: TimeControl[] = [
  { label: '1+0',       initialMs:  60_000, incMs:     0 },
  { label: '1+1',       initialMs:  60_000, incMs: 1_000 },
  { label: '3+0',       initialMs: 180_000, incMs:     0 },
  { label: '3+2',       initialMs: 180_000, incMs: 2_000 },
  { label: '5+0',       initialMs: 300_000, incMs:     0 },
  { label: '10+0',      initialMs: 600_000, incMs:     0 },
  { label: '∞ / Free',  initialMs:       0, incMs:     0 },
];
