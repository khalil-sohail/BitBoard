import { memo } from 'react';
import type { CompactSessionStatus } from './responsive-session.types';
import styles from './ResponsiveSession.module.css';

export const CompactStatusStrip = memo(function CompactStatusStrip({ status, expanded, controls, onToggle }: {
  status: CompactSessionStatus; expanded: boolean; controls: string; onToggle: () => void;
}) {
  return <button
    type="button"
    className={styles.compactStrip}
    data-compact-status={status.mode}
    data-tone={status.tone}
    aria-expanded={expanded}
    aria-controls={controls}
    aria-label={`${expanded ? 'Collapse' : 'Open'} ${status.modeLabel} session panel. ${status.primary}`}
    onClick={onToggle}
  >
    <span className={styles.statusMarker} aria-hidden="true" />
    <span className={styles.statusCopy}>
      <small>{status.modeLabel}</small><strong>{status.primary}</strong>
      {status.secondary ? <span>{status.secondary}</span> : null}
      {status.detail ? <code>{status.detail}</code> : null}
    </span>
    {status.values?.length ? <span className={styles.compactValues} aria-label="Compact session values">
      {status.values.map(item => <span key={item.label}><small>{item.label}</small><strong>{item.value}</strong></span>)}
    </span> : null}
    {status.badge ? <span className={styles.statusBadge}>{status.badge}</span> : null}
    <span className={styles.chevron} aria-hidden="true">{expanded ? '⌄' : '⌃'}</span>
  </button>;
});
