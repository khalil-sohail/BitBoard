import type { RefObject, ReactNode } from 'react';
import styles from './ResponsiveSession.module.css';

export function SessionBottomPanel({ id, title, expanded, compact, mobile, panelRef, closeRef, onClose, children }: {
  id: string; title: string; expanded: boolean; compact: boolean; mobile: boolean;
  panelRef: RefObject<HTMLDivElement | null>; closeRef: RefObject<HTMLButtonElement | null>;
  onClose: () => void; children: ReactNode;
}) {
  return <div
    ref={panelRef}
    id={id}
    className={styles.panel}
    data-expanded={expanded}
    data-mobile-overlay={mobile || undefined}
    role={mobile ? 'dialog' : 'region'}
    aria-modal={mobile ? true : undefined}
    aria-label={`${title} session details`}
    aria-hidden={compact && !expanded ? true : undefined}
    inert={compact && !expanded ? true : undefined}
  >
    <div className={styles.panelHeader}>
      <span className={styles.panelHandle} aria-hidden="true" />
      <strong>{title} session</strong>
      <button ref={closeRef} type="button" onClick={onClose} aria-label="Collapse session panel">Collapse</button>
    </div>
    <div className={styles.panelBody}>{children}</div>
  </div>;
}
