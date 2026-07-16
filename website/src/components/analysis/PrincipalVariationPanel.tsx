"use client";

import { useMemo } from 'react';
import type { AnalysisSnapshot } from '@/lib/board-arrows';
import { LiveDataSection } from '../live-data/LiveDataSection';
import { PrincipalVariationList } from '../live-data/PrincipalVariationList';
import { analysisPvLines } from './analysis-live-data';

export function PrincipalVariationPanel({ snapshot, requestedLines }: { snapshot: AnalysisSnapshot | null; requestedLines: number }) {
  const lines = useMemo(() => analysisPvLines(snapshot), [snapshot]);
  return <LiveDataSection title="Principal variations"><PrincipalVariationList lines={lines} rootFen={snapshot?.fen} requestId={snapshot?.requestId} requestedLines={requestedLines} loading={!snapshot} emptyText="Waiting for a principal variation for this position…" /></LiveDataSection>;
}
