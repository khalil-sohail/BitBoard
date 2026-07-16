import type { FairPlayStatusPresentation } from './fair-play-presentation';
import styles from './FairPlaySidebar.module.css';

export function FairPlayOperationalStatus({ status }: { status: FairPlayStatusPresentation }) {
  if (status.state === 'idle' || status.state === 'completed') return null;
  if (status.state === 'active' && (status.headline === 'Your turn' || status.headline === 'Engine thinking' || status.headline === 'Waiting for engine')) return null;
  const isError = status.tone === 'error';
  return (
    <section className={`${styles.operationalStatus} ${isError ? styles.operationalError : ''}`} data-tone={status.tone} role={isError ? 'alert' : 'status'} aria-live={isError ? 'assertive' : 'polite'} aria-atomic="true">
      <span className={styles.statusDot} aria-hidden="true" />
      <div>
        <h3>{status.headline}</h3>
        <p>{status.detail}</p>
      </div>
    </section>
  );
}
