import type { AnalysisSnapshot } from '@/lib/board-arrows';
import type { EngineInfo } from '@/types/engine';
import { EngineMetrics } from '../live-data/EngineMetrics';
import { LiveDataSection } from '../live-data/LiveDataSection';

export function AnalysisMetricsPanel({ snapshot, info }: { snapshot: AnalysisSnapshot | null; info: EngineInfo | null }) {
  const current = info && info.requestId === snapshot?.requestId && info.rootFen === snapshot?.fen ? info : null;
  return <LiveDataSection title="Engine details" collapsible><EngineMetrics compact metrics={{ depth: snapshot?.reportedDepth, selectiveDepth: snapshot?.selectiveDepth, nodes: current?.nodes, timeMs: current?.time }} /></LiveDataSection>;
}
