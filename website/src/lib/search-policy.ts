import type { EngineDifficulty, SearchPurpose } from './engine-difficulty';
import { getDifficultyProfile } from './engine-difficulty';
import type { SearchLimit } from './engine-protocol';

export type SearchMode = 'fair' | 'training' | 'analysis';

export interface ClockContext {
  wtime: number;
  btime: number;
  winc: number;
  binc: number;
  movestogo?: number;
}

export interface SearchPolicyContext {
  mode: SearchMode;
  purpose: SearchPurpose;
  difficulty: EngineDifficulty;
  userMaxDepth: number;
  multiPv: number;
  trainingPonderEnabled: boolean;
  fairPonderEnabled?: boolean;
  clock?: ClockContext;
}

export type SearchPolicySource =
  | 'clock'
  | 'difficulty-profile'
  | 'user-max-depth'
  | 'training-opponent-profile'
  | 'training-review-profile'
  | 'training-hint-profile';

export interface ResolvedSearchPolicy {
  limit: SearchLimit;
  multiPv: number;
  ponder: boolean;
  source: SearchPolicySource;
}

export const TRAINING_REVIEW_DEPTH_CAP = 12;
export const TRAINING_HINT_DEPTH_CAP = 8;

function clampDepth(depth: number): number {
  if (!Number.isFinite(depth)) return 1;
  return Math.max(1, Math.min(64, Math.trunc(depth)));
}

function profileToLimit(profile: { depth?: number; movetimeMs?: number }): SearchLimit {
  if (profile.depth !== undefined) {
    return { mode: 'depth', depth: clampDepth(profile.depth) };
  }
  if (profile.movetimeMs !== undefined) {
    return { mode: 'movetime', movetimeMs: Math.max(1, Math.trunc(profile.movetimeMs)) };
  }
  return { mode: 'movetime', movetimeMs: 3000 };
}

function capDepthLimit(limit: SearchLimit, cap: number): SearchLimit {
  if (limit.mode !== 'depth') return limit;
  return { mode: 'depth', depth: Math.min(limit.depth, clampDepth(cap)) };
}

function clockLimit(clock: ClockContext): SearchLimit {
  return {
    mode: 'clock',
    wtime: Math.max(0, Math.trunc(clock.wtime)),
    btime: Math.max(0, Math.trunc(clock.btime)),
    winc: Math.max(0, Math.trunc(clock.winc)),
    binc: Math.max(0, Math.trunc(clock.binc)),
    ...(clock.movestogo !== undefined ? { movestogo: Math.max(1, Math.trunc(clock.movestogo)) } : {}),
  };
}

export function resolveSearchPolicy(context: SearchPolicyContext): ResolvedSearchPolicy {
  const userDepth = clampDepth(context.userMaxDepth);
  const requestedMultiPv = Math.max(1, Math.min(8, Math.trunc(context.multiPv)));

  if (context.mode === 'fair' && context.purpose === 'opponent' && context.clock) {
    return {
      limit: clockLimit(context.clock),
      multiPv: requestedMultiPv,
      ponder: context.fairPonderEnabled === true,
      source: 'clock',
    };
  }

  if (context.mode === 'training' && context.purpose === 'opponent') {
    const profile = getDifficultyProfile(context.difficulty, 'opponent');
    return {
      limit: capDepthLimit(profileToLimit(profile), userDepth),
      multiPv: profile.multiPv,
      ponder: context.trainingPonderEnabled === true,
      source: 'training-opponent-profile',
    };
  }

  if (context.purpose === 'opponent') {
    const profile = getDifficultyProfile(context.difficulty, 'opponent');
    return {
      limit: profileToLimit(profile),
      multiPv: requestedMultiPv,
      ponder: context.mode === 'fair' && context.fairPonderEnabled === true,
      source: 'difficulty-profile',
    };
  }

  if (context.purpose === 'training-root-review' || context.purpose === 'training-result-review') {
    return {
      limit: { mode: 'depth', depth: Math.min(userDepth, TRAINING_REVIEW_DEPTH_CAP) },
      multiPv: requestedMultiPv,
      ponder: false,
      source: 'training-review-profile',
    };
  }

  if (context.purpose === 'training-hint') {
    return {
      limit: { mode: 'depth', depth: Math.min(userDepth, TRAINING_HINT_DEPTH_CAP) },
      multiPv: 1,
      ponder: false,
      source: 'training-hint-profile',
    };
  }

  return {
    limit: { mode: 'depth', depth: userDepth },
    multiPv: requestedMultiPv,
    ponder: false,
    source: 'user-max-depth',
  };
}

