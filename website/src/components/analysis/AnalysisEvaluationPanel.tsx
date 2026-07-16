import type { AnalysisSnapshot } from '@/lib/board-arrows';
import { evaluationText } from './analysis-presentation';
import styles from './AnalysisSidebar.module.css';

export function AnalysisEvaluationPanel({ snapshot, stopped }: { snapshot: AnalysisSnapshot | null; stopped: boolean }) {
  const display = evaluationText(snapshot?.lines[0]?.evaluation ?? null);
  return <section className={styles.evaluationPanel} aria-labelledby="analysis-evaluation-title">
    <div><p className={styles.eyebrow}>White perspective</p><h3 id="analysis-evaluation-title">Current evaluation</h3></div>
    <div className={styles.evaluationValue} aria-label={display.accessible}>
      <strong>{display.score}</strong><span>{display.meaning}</span>
    </div>
    {stopped && snapshot ? <p className={styles.snapshotNote}>Stopped snapshot · values are no longer updating</p> : null}
  </section>;
}
