"use client";

import { useMemo } from 'react';
import type { EngineInfo } from '@/types/engine';
import { EngineMetrics } from '../live-data/EngineMetrics';
import { EvaluationDisplay } from '../live-data/EvaluationDisplay';
import { LiveDataSection } from '../live-data/LiveDataSection';
import { PrincipalVariationList } from '../live-data/PrincipalVariationList';
import { selectTrainingReviewInfo, trainingPvLines } from './training-live-data';

export function TrainingAnalysisPanel({ info, reviewing }: { info: EngineInfo | null; reviewing: boolean }) {
  const visibleInfo = selectTrainingReviewInfo(info, reviewing);
  const evaluation = visibleInfo?.pvs[0]?.evaluation ?? null;
  const lines = useMemo(() => trainingPvLines(visibleInfo), [visibleInfo]);
  const emptyText = reviewing ? 'Waiting for the current move review…' : 'Analysis will appear for the current training position.';
  return <>
    <LiveDataSection title="Evaluation & continuation" eyebrow="Position review" meta={<EvaluationDisplay evaluation={evaluation} state={visibleInfo ? 'live' : 'loading'} compact />}>
      <PrincipalVariationList lines={lines} rootFen={visibleInfo?.rootFen} requestId={visibleInfo?.requestId} loading={!visibleInfo} emptyText={emptyText} />
    </LiveDataSection>
    <LiveDataSection title="Engine details" collapsible>
      <EngineMetrics compact metrics={{ depth: visibleInfo?.reportedDepth ?? visibleInfo?.depth, selectiveDepth: visibleInfo?.selectiveDepth, nodes: visibleInfo?.nodes, timeMs: visibleInfo?.time }} />
    </LiveDataSection>
  </>;
}
