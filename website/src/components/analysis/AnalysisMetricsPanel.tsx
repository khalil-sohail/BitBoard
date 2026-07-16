import type { AnalysisSnapshot } from '@/lib/board-arrows';
import type { EngineInfo } from '@/types/engine';
import { compactCount } from './analysis-presentation';
import styles from './AnalysisSidebar.module.css';

export function AnalysisMetricsPanel({ snapshot, info }: { snapshot: AnalysisSnapshot | null; info: EngineInfo | null }) {
  const current = info && info.requestId === snapshot?.requestId && info.rootFen === snapshot?.fen ? info : null;
  const nodes = current?.nodes;
  const nps = nodes !== undefined && current?.time ? Math.round(nodes * 1000 / current.time) : undefined;
  return <details className={styles.metricsPanel}>
    <summary>Engine details</summary>
    <dl>
      <div><dt>Depth</dt><dd>{snapshot?.reportedDepth ?? '—'}</dd></div>
      <div><dt>Selective</dt><dd>{snapshot?.selectiveDepth ?? '—'}</dd></div>
      <div><dt>Nodes</dt><dd>{compactCount(nodes)}</dd></div>
      <div><dt>NPS</dt><dd>{nps === undefined ? '—' : `${compactCount(nps)}/s`}</dd></div>
    </dl>
  </details>;
}
