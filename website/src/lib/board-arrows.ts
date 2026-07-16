import type { Arrow } from 'react-chessboard';
import type { Square } from 'chess.js';
import type { EngineInfo, GameMode, PVLine } from '../types/engine';
import type { SearchLimit } from './engine-protocol';
import type { ProgressiveHintView } from './training-hint';
import type { TrainingState } from './training-machine';
import { convertUciToArrow } from './square-utils';

export type ArrowKind = 'analysis' | 'hint' | 'bestmove' | 'last-move' | 'selection';

export interface BoardArrow {
  key: string;
  from: Square;
  to: Square;
  kind: ArrowKind;
  color: string;
  rank?: number;
  requestId?: number;
}

export interface AnalysisSnapshot {
  requestId: number;
  mode?: GameMode;
  purpose: EngineInfo['purpose'];
  fen: string;
  positionFen?: string;
  positionKey?: string;
  sessionId?: string;
  sessionGeneration?: number;
  requestedLimit?: SearchLimit;
  reportedDepth: number | null;
  selectiveDepth: number | null;
  multiPv: number;
  lines: PVLine[];
  status: 'live' | 'finalized';
  createdAt: number;
}

export interface AnalysisDisplayState {
  live: AnalysisSnapshot | null;
  finalized: AnalysisSnapshot | null;
}

export interface ComposeBoardArrowsInput {
  mode: GameMode;
  trainingState?: TrainingState;
  currentFen: string;
  engineInfo?: EngineInfo | null;
  analysis?: AnalysisDisplayState;
  hintView?: ProgressiveHintView | null;
}

const ANALYSIS_COLORS = [
  'rgba(34, 197, 94, 0.8)',
  'rgba(59, 130, 246, 0.8)',
  'rgba(234, 179, 8, 0.8)',
];

const KIND_PRIORITY: Record<ArrowKind, number> = {
  hint: 0,
  selection: 1,
  bestmove: 2,
  analysis: 3,
  'last-move': 4,
};

export function composeBoardArrows(input: ComposeBoardArrowsInput): BoardArrow[] {
  const arrows: BoardArrow[] = [];

  arrows.push(...composeAnalysisArrows(input));

  if (input.hintView?.from && input.hintView.to && input.hintView.level >= 2) {
    arrows.push(makeArrow({
      kind: 'hint',
      from: input.hintView.from as Square,
      to: input.hintView.to as Square,
      color: 'rgba(244, 114, 182, 0.9)',
    }));
  }

  return dedupeForBoard(arrows);
}

export function toChessboardArrows(arrows: readonly BoardArrow[]): Arrow[] {
  return arrows.map((arrow) => ({
    startSquare: arrow.from,
    endSquare: arrow.to,
    color: arrow.color,
  }));
}

function composeAnalysisArrows(input: ComposeBoardArrowsInput): BoardArrow[] {
  const snapshots = [
    input.analysis?.live,
    input.analysis?.finalized,
  ].filter((snapshot): snapshot is AnalysisSnapshot => snapshot !== null && snapshot !== undefined);

  if (snapshots.length > 0) {
    return snapshots.flatMap(snapshot => {
      if (snapshot.fen !== input.currentFen) return [];
      if (input.mode === 'training' && !trainingAllowsSnapshot(input.trainingState, snapshot)) return [];
      if (snapshot.purpose === 'training-hint') return [];
      return pvArrows(snapshot.lines, snapshot.requestId, snapshot.purpose === 'training-result-review' ? 'bestmove' : 'analysis');
    });
  }

  const info = input.engineInfo;
  if (!info || info.rootFen !== input.currentFen) return [];
  if (input.mode === 'training' && !trainingAllowsInfo(input.trainingState, info)) return [];
  if (info.purpose === 'training-hint') return [];
  if (info.source === 'book') return [];

  return pvArrows(info.pvs, info.requestId);
}

function trainingAllowsInfo(trainingState: TrainingState | undefined, info: EngineInfo): boolean {
  if (!trainingState) return false;
  if (trainingState.status !== 'waiting-player') return false;
  return info.purpose === 'training-root-review';
}

function trainingAllowsSnapshot(trainingState: TrainingState | undefined, snapshot: AnalysisSnapshot): boolean {
  if (!trainingState) return false;
  if (snapshot.purpose === 'training-root-review') {
    return trainingState.status === 'waiting-player';
  }
  if (snapshot.purpose === 'training-result-review') {
    return trainingState.status === 'reviewing-player-move' ||
      trainingState.status === 'showing-feedback' ||
      trainingState.status === 'waiting-player';
  }
  return false;
}

function pvArrows(pvs: PVLine[] | undefined, requestId: number | undefined, kind: ArrowKind = 'analysis'): BoardArrow[] {
  if (!pvs || pvs.length === 0) return [];

  const sorted = [...pvs].sort((a, b) => a.multipv - b.multipv).slice(0, 3);
  return sorted.flatMap((pv, index) => {
    const uciMove = pv.pv?.[0];
    if (!uciMove) return [];
    const converted = convertUciToArrow(uciMove, ANALYSIS_COLORS[index] ?? ANALYSIS_COLORS[ANALYSIS_COLORS.length - 1]);
    if (!converted) return [];
    return [makeArrow({
      kind,
      from: converted[0],
      to: converted[1],
      color: converted[2],
      rank: pv.multipv,
      requestId,
    })];
  });
}

function makeArrow(input: Omit<BoardArrow, 'key'>): BoardArrow {
  return {
    ...input,
    key: `${input.kind}-${input.requestId ?? 'static'}-${input.rank ?? 0}-${input.from}-${input.to}`,
  };
}

function dedupeForBoard(arrows: BoardArrow[]): BoardArrow[] {
  const semantic = new Map<string, BoardArrow>();
  for (const arrow of arrows) {
    if (!semantic.has(arrow.key)) {
      semantic.set(arrow.key, arrow);
    }
  }

  const byGeometry = new Map<string, BoardArrow>();
  for (const arrow of semantic.values()) {
    const geometryKey = `${arrow.from}-${arrow.to}`;
    const existing = byGeometry.get(geometryKey);
    if (!existing || compareArrows(arrow, existing) < 0) {
      byGeometry.set(geometryKey, arrow);
    }
  }

  return [...byGeometry.values()].sort(compareArrows);
}

function compareArrows(a: BoardArrow, b: BoardArrow): number {
  const kind = KIND_PRIORITY[a.kind] - KIND_PRIORITY[b.kind];
  if (kind !== 0) return kind;
  const request = (a.requestId ?? 0) - (b.requestId ?? 0);
  if (request !== 0) return request;
  const rank = (a.rank ?? 0) - (b.rank ?? 0);
  if (rank !== 0) return rank;
  return a.key.localeCompare(b.key);
}
