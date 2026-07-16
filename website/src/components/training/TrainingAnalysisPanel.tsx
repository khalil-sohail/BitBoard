import type { EngineInfo } from '@/types/engine';
import { displayScore, type NormalizedEvaluation } from '@/lib/engine-evaluation';
import { formatCompactCount, formatNps } from './training-presentation';
import styles from './TrainingSidebar.module.css';

export function TrainingAnalysisPanel({ info, reviewing }: { info: EngineInfo | null; reviewing: boolean }) {
  const evaluation = info?.pvs?.[0]?.evaluation ?? null;
  const currentReview = !reviewing || info?.purpose === 'training-result-review';
  const visibleInfo = currentReview ? info : null;
  const visibleEval = currentReview ? evaluation : null;
  return (
    <section className={styles.analysisPanel} aria-labelledby="training-analysis-title">
      <div className={styles.analysisHeader}>
        <div>
          <p className={styles.sectionEyebrow}>Position review</p>
          <h3 id="training-analysis-title">Evaluation &amp; continuation</h3>
        </div>
        <div className={styles.evaluation} aria-label={evaluationLabel(visibleEval)}>
          <strong>{visibleInfo ? displayScore(visibleEval) : '—'}</strong>
          <span>{advantageLabel(visibleEval)}</span>
        </div>
      </div>
      <div className={styles.pvLines}>
        {visibleInfo?.pvs?.length ? visibleInfo.pvs.map((line, index) => (
          <div className={styles.pvLine} key={`${visibleInfo.requestId ?? 0}:${line.multipv}`}>
            <span>{index === 0 ? 'Main line' : `Line ${index + 1}`}</span>
            <code>{line.pv.join(' ') || 'No continuation reported'}</code>
          </div>
        )) : <p className={styles.emptyText}>{reviewing ? 'Waiting for the current move review…' : 'Analysis will appear for the current training position.'}</p>}
      </div>
      <details className={styles.metrics}>
        <summary>Engine details</summary>
        <dl>
          <div><dt>Depth</dt><dd>{visibleInfo?.reportedDepth ?? visibleInfo?.depth ?? '—'}</dd></div>
          <div><dt>Selective</dt><dd>{visibleInfo?.selectiveDepth ?? '—'}</dd></div>
          <div><dt>Nodes</dt><dd>{formatCompactCount(visibleInfo?.nodes)}</dd></div>
          <div><dt>NPS</dt><dd>{formatNps(visibleInfo?.nodes, visibleInfo?.time)}</dd></div>
        </dl>
      </details>
    </section>
  );
}

function advantageLabel(evaluation: NormalizedEvaluation | null): string {
  if (!evaluation) return 'Waiting';
  if (evaluation.kind === 'mate') return `${evaluation.winner === 'white' ? 'White' : 'Black'} has mate`;
  if (evaluation.kind === 'terminal') return 'Terminal position';
  if (Math.abs(evaluation.value) < 20) return 'Approximately equal';
  return evaluation.value > 0 ? 'White advantage' : 'Black advantage';
}

function evaluationLabel(evaluation: NormalizedEvaluation | null): string {
  return `White-perspective evaluation: ${displayScore(evaluation)}, ${advantageLabel(evaluation)}`;
}
