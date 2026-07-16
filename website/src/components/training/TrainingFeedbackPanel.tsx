import type { Move } from 'chess.js';
import type { GradedMove } from '@/types/grades';
import { MoveBadge } from '@/components/ui/MoveBadge';
import styles from './TrainingSidebar.module.css';

export function TrainingFeedbackPanel({ moves, grades, reviewing }: { moves: Move[]; grades: GradedMove[]; reviewing: boolean }) {
  const latest = grades.reduce<GradedMove | null>((current, grade) => !current || grade.moveIndex > current.moveIndex ? grade : current, null);
  const move = latest ? moves[latest.moveIndex] : undefined;
  const loss = latest ? Math.max(0, -latest.delta) : 0;

  return (
    <section className={styles.feedbackPanel} aria-labelledby="training-feedback-title">
      <div className={styles.sectionHeading}>
        <h3 id="training-feedback-title">Latest feedback</h3>
        {latest && <MoveBadge grade={latest.grade} />}
      </div>
      {reviewing ? (
        <div className={styles.feedbackPending} role="status">Reviewing the move you just played…</div>
      ) : latest && move ? (
        <div className={styles.feedbackContent} role="status" aria-live="polite">
          <p className={styles.reviewedMove}><span>Reviewed move</span><strong>{move.san}</strong></p>
          <p>{feedbackExplanation(latest.grade, loss)}</p>
          {latest.hintLevelUsed ? <p className={styles.hintUsed}>Hint level {latest.hintLevelUsed} was used for this move.</p> : null}
        </div>
      ) : (
        <p className={styles.emptyText}>Your latest move grade and explanation will remain here after review.</p>
      )}
    </section>
  );
}

function feedbackExplanation(grade: GradedMove['grade'], loss: number): string {
  if (grade === 'Book') return 'This move follows the opening book.';
  if (grade === 'Forced') return 'This was the only legal continuation.';
  if (grade === 'Best') return 'This matched the engine’s best decision.';
  if (grade === 'Good') return loss > 0 ? `A sound choice; the estimated loss was ${loss} centipawns.` : 'A sound choice that preserved the position.';
  return `${grade}: the estimated loss was ${loss} centipawns.`;
}
