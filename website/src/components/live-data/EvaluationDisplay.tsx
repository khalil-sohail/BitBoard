import type { NormalizedEvaluation } from '../../lib/engine-evaluation';
import { formatEvaluation } from './evaluation-format';
import styles from './LiveData.module.css';

export function EvaluationDisplay({ evaluation, state = 'live', compact = false }: { evaluation: NormalizedEvaluation | null; state?: 'loading' | 'live' | 'stopped'; compact?: boolean }) {
  const display = formatEvaluation(evaluation);
  return <div className={styles.evaluation} data-live-evaluation data-state={state} data-compact={compact || undefined} aria-label={display.accessible}>
    <strong>{display.score}</strong><span>{display.meaning}</span>
    {state === 'stopped' && evaluation ? <small>Stopped snapshot · values are no longer updating</small> : null}
  </div>;
}
