import type { HintContext } from '@/lib/training-machine';
import type { ProgressiveHintView } from '@/lib/training-hint';
import styles from './TrainingSidebar.module.css';

export function TrainingHintPanel({ hint, hintView, canRequest, onRequest }: {
  hint?: HintContext;
  hintView: ProgressiveHintView | null;
  canRequest: boolean;
  onRequest: () => void;
}) {
  const searching = hint?.status === 'searching';
  const label = hint?.level === 1 ? 'Next hint' : hint?.level === 2 ? 'Reveal move' : 'Request hint';
  return (
    <section className={styles.hintPanel} aria-labelledby="training-hint-title">
      <div className={styles.sectionHeading}>
        <div>
          <h3 id="training-hint-title">Hint</h3>
          <span>{hint?.level ? `Level ${hint.level} of 3` : 'Progressive guidance'}</span>
        </div>
        <button type="button" onClick={onRequest} disabled={!canRequest || searching} aria-label={searching ? 'Training hint is loading' : label}>
          {searching ? 'Finding…' : label}
        </button>
      </div>
      <div className={styles.hintResult} role="status" aria-live="polite">
        {searching ? 'Preparing a hint for this position…' : hintView?.text ?? (hint?.status === 'error' ? hint.message ?? 'No legal hint is available.' : 'Hints guide you without moving a piece.')}
      </div>
    </section>
  );
}
