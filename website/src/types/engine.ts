export interface PVLine {
  multipv: number;
  score: number;
  mate?: number;
  pv: string[];
}

export interface EngineInfo {
  depth: number;
  nodes?: number;
  time?: number; // ms
  pvs: PVLine[];
}

export interface EvalPoint {
  moveNumber: number;
  eval: number; // white's perspective
}

export type PlayerColor = 'w' | 'b';

export type DifficultyLevel = 'blitz' | 'standard' | 'deep';

/**
 * The three operating modes for the chess platform.
 * - fair:     Pure game vs engine. Evaluation UI hidden.
 * - training: Game vs engine with full engine brain visible.
 * - analysis: Free exploration — both sides controlled by user, FEN loading enabled.
 */
export type GameMode = 'fair' | 'training' | 'analysis';
