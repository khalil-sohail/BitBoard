export interface EngineInfo {
  depth: number;
  score: number; // centipawns
  mate?: number; // mate in N moves
  pv: string[]; // e.g. ["e2e4", "e7e5"]
  nodes?: number;
  time?: number; // ms
}

export interface EvalPoint {
  moveNumber: number;
  eval: number; // white's perspective
}

export type PlayerColor = 'w' | 'b';

export type DifficultyLevel = 'blitz' | 'standard' | 'deep';
