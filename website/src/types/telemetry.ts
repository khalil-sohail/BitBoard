import type { GameMode, PlayerColor } from './engine';
import type { SearchLimit } from '../lib/engine-protocol';
import type { MoveGrade } from './grades';

export const ENGINE_GAME_RECORD_SCHEMA_VERSION = 1;

export interface EngineGameRecord {
  schemaVersion: typeof ENGINE_GAME_RECORD_SCHEMA_VERSION;
  gameId: string;
  startedAt: string;
  mode: Extract<GameMode, 'fair' | 'training'>;
  result: '1-0' | '0-1' | '1/2-1/2' | '*';
  engineColor: 'white' | 'black';
  timeControl?: {
    initialMs: number;
    incrementMs: number;
  };
  engineVersion: string;
  tuningProfileId: string;
  openingBookEnabled: boolean;
  trainingPonderEnabled?: boolean;
  moves: EngineMoveRecord[];
}

export interface EngineMoveRecord {
  ply: number;
  fenBefore: string;
  move: string;
  source: 'book' | 'search' | 'ponder';
  purpose: string;
  requestedLimit: SearchLimit;
  completedDepth: number | null;
  selectiveDepth: number | null;
  nodes: number | null;
  elapsedMs: number | null;
  remainingClockMs: number | null;
  evaluationBefore?: number;
  evaluationAfter?: number;
  principalVariation?: string[];
  ponderEnabled?: boolean;
  ponderPrediction?: string;
  ponderHit?: boolean;
  ponderElapsedMs?: number;
  trainingGrade?: MoveGrade;
  hintLevelUsed?: number;
  tuningSnapshotHash: string;
}

export interface EngineDerivedLabels {
  schemaVersion: typeof ENGINE_GAME_RECORD_SCHEMA_VERSION;
  gameId: string;
  gameResult?: EngineGameRecord['result'];
  centipawnLoss?: number;
  bestMoveAgreement?: boolean;
  evaluationSwing?: number;
  timeUsageRatio?: number;
  flagOccurrence?: boolean;
  timeoutOccurrence?: boolean;
  ponderHitRate?: number;
  depthEfficiency?: number;
  nodesPerMillisecond?: number;
  openingOutcome?: EngineGameRecord['result'];
  gamePhase?: 'opening' | 'middlegame' | 'endgame';
  hintDependency?: number;
  gradeDistribution?: Partial<Record<MoveGrade, number>>;
}

export interface TuningExperimentRecord {
  profileId: string;
  parentProfileId: string;
  parameterDiff: Record<string, number | boolean>;
  datasetVersion: string;
  experimentSeed: number;
  trainingResult: string;
  validationResult: string;
  testResult?: string;
}

export function engineColorName(color: PlayerColor): EngineGameRecord['engineColor'] {
  return color === 'w' ? 'white' : 'black';
}

