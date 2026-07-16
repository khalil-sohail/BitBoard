import type { EngineMetricsView } from './live-data.types';
import { formatCompactCount } from './metrics-format';
import styles from './LiveData.module.css';

export function EngineMetrics({ metrics, compact = false }: { metrics: EngineMetricsView; compact?: boolean }) {
  const nps = metrics.nodes !== undefined && metrics.timeMs ? Math.round(metrics.nodes * 1000 / metrics.timeMs) : undefined;
  const values = [
    ['Depth', metrics.depth ?? '—'], ['Selective', metrics.selectiveDepth ?? '—'],
    ['Nodes', formatCompactCount(metrics.nodes)], ['NPS', nps === undefined ? '—' : `${formatCompactCount(nps)}/s`],
    ...(!compact ? [['Time', metrics.timeMs === undefined ? '—' : `${(metrics.timeMs / 1000).toFixed(2)}s`]] : []),
  ];
  return <dl className={styles.metrics} data-live-metrics data-compact={compact || undefined}>{values.map(([label, value]) => <div key={label}><dt>{label}</dt><dd>{value}</dd></div>)}</dl>;
}
