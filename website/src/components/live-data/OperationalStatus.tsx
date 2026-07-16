import type { ReactNode } from 'react';
import type { LiveDataTone } from './live-data.types';
import styles from './LiveData.module.css';

export function OperationalStatus({ title, description, tone = 'neutral', action, announce = 'polite' }: { title: string; description?: string; tone?: LiveDataTone; action?: ReactNode; announce?: 'off' | 'polite' | 'assertive' }) {
  const error = tone === 'error';
  return <section className={styles.status} data-live-status data-tone={tone} role={error ? 'alert' : 'status'} aria-live={error ? 'assertive' : announce} aria-atomic="true">
    <span className={styles.statusMarker} aria-hidden="true" />
    <div><h3>{title}</h3>{description ? <p>{description}</p> : null}</div>{action ? <div className={styles.statusAction}>{action}</div> : null}
  </section>;
}
