import type { PlayerColor } from '@/types/engine';
import { deriveCompletedHeadline } from './fair-play-presentation';
import styles from './FairPlaySidebar.module.css';

export function FairPlayCompletedSummary({ message, playerColor, plyCount }: { message: string; playerColor: PlayerColor; plyCount: number }) {
  const fullMoveCount = Math.ceil(plyCount / 2);
  return (
    <section className={styles.completedSummary} role="status" aria-live="assertive" aria-atomic="true">
      <p className={styles.eyebrow}>Final result</p>
      <h2>{deriveCompletedHeadline(message, playerColor)}</h2>
      <p className={styles.termination}>{message}</p>
      <dl>
        <div><dt>You played</dt><dd>{playerColor === 'w' ? 'White' : 'Black'}</dd></div>
        <div><dt>Length</dt><dd>{fullMoveCount} move{fullMoveCount === 1 ? '' : 's'}</dd></div>
      </dl>
    </section>
  );
}
