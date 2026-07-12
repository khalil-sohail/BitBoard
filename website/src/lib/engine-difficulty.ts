export const ENGINE_DIFFICULTIES = ['blitz', 'standard', 'deep'] as const;

export type EngineDifficulty = typeof ENGINE_DIFFICULTIES[number];

export type SearchPurpose =
  | 'opponent'
  | 'training-root-review'
  | 'training-result-review'
  | 'training-hint'
  | 'analysis';

export interface SearchConfiguration {
  depth?: number;
  movetimeMs?: number;
  multiPv: number;
  openingSelection?: OpeningSelectionConfiguration;
}

export type OpeningSelectionConfiguration =
  | { mode: 'weighted' }
  | { mode: 'top-n-weighted'; maxCandidates: number }
  | { mode: 'best' };

export interface DifficultyOption {
  id: EngineDifficulty;
  label: string;
  sublabel: string;
}

export const DIFFICULTY_OPTIONS: readonly DifficultyOption[] = Object.freeze([
  { id: 'blitz', label: 'Blitz', sublabel: '1 sec' },
  { id: 'standard', label: 'Standard', sublabel: '3 sec' },
  { id: 'deep', label: 'Deep', sublabel: 'D8' },
]);

const OPPONENT_PROFILES: Readonly<Record<EngineDifficulty, SearchConfiguration>> = Object.freeze({
  blitz: Object.freeze({ movetimeMs: 1000, multiPv: 1, openingSelection: Object.freeze({ mode: 'weighted' }) }),
  standard: Object.freeze({ movetimeMs: 3000, multiPv: 1, openingSelection: Object.freeze({ mode: 'top-n-weighted', maxCandidates: 4 }) }),
  deep: Object.freeze({ depth: 8, multiPv: 1, openingSelection: Object.freeze({ mode: 'best' }) }),
});

const DEFAULT_REVIEW_DEPTH = 12;
const DEFAULT_ANALYSIS_DEPTH = 30;
const DEFAULT_ANALYSIS_MULTIPV = 3;

export function isEngineDifficulty(value: unknown): value is EngineDifficulty {
  return typeof value === 'string' && ENGINE_DIFFICULTIES.includes(value as EngineDifficulty);
}

export function getDifficultyProfile(
  difficulty: EngineDifficulty,
  purpose: SearchPurpose,
  explicit: { depth?: number; multiPv?: number } = {},
): SearchConfiguration {
  if (purpose === 'opponent') {
    return OPPONENT_PROFILES[difficulty];
  }

  if (purpose === 'analysis') {
    return {
      depth: explicit.depth ?? DEFAULT_ANALYSIS_DEPTH,
      multiPv: explicit.multiPv ?? DEFAULT_ANALYSIS_MULTIPV,
    };
  }

  return {
    depth: explicit.depth ?? DEFAULT_REVIEW_DEPTH,
    multiPv: explicit.multiPv ?? DEFAULT_ANALYSIS_MULTIPV,
  };
}
