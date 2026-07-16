import type { AnalysisSnapshot } from '@/lib/board-arrows';
import { evaluationText } from './analysis-presentation';
import styles from './AnalysisSidebar.module.css';

export function PrincipalVariationPanel({ snapshot, requestedLines }: { snapshot: AnalysisSnapshot | null; requestedLines: number }) {
  const lines = [...(snapshot?.lines ?? [])].sort((left, right) => left.multipv - right.multipv);
  return <section className={styles.pvPanel} aria-labelledby="analysis-pv-title">
    <div className={styles.sectionHeading}><h3 id="analysis-pv-title">Principal variations</h3><span>{lines.length}/{requestedLines} lines</span></div>
    {lines.length ? <ol className={styles.pvList}>
      {lines.map(line => {
        const evaluation = evaluationText(line.evaluation ?? null);
        return <li key={`${snapshot?.requestId}:${line.multipv}`} data-primary={line.multipv === 1}>
          <div><strong>Line {line.multipv}</strong><span>{evaluation.score}</span>{snapshot?.reportedDepth ? <small>Depth {snapshot.reportedDepth}</small> : null}</div>
          <code>{line.pv.length ? line.pv.join(' ') : 'No continuation reported'}</code>
        </li>;
      })}
    </ol> : <p className={styles.emptyText}>Waiting for a principal variation for this position…</p>}
  </section>;
}
