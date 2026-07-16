"use client";

import { useId, type ReactNode } from 'react';
import styles from './LiveData.module.css';

export function LiveDataSection({ title, eyebrow, meta, actions, children, collapsible = false, defaultOpen = false, className }: {
  title: string; eyebrow?: string; meta?: ReactNode; actions?: ReactNode; children: ReactNode;
  collapsible?: boolean; defaultOpen?: boolean; className?: string;
}) {
  const headingId = useId();
  const header = <div className={styles.sectionHeader}>
    <div>{eyebrow ? <p className={styles.eyebrow}>{eyebrow}</p> : null}<h3 id={headingId}>{title}</h3></div>
    {meta ? <div className={styles.sectionMeta}>{meta}</div> : null}
    {actions ? <div className={styles.sectionHeaderActions}>{actions}</div> : null}
  </div>;
  if (collapsible) return <details className={`${styles.section} ${styles.disclosure} ${className ?? ''}`} open={defaultOpen}>
    <summary><span className={styles.disclosureHeader}><span id={headingId} role="heading" aria-level={3}>{title}</span>{meta ? <span className={styles.sectionMeta}>{meta}</span> : null}</span></summary><div className={styles.sectionBody}>{children}</div>
  </details>;
  return <section className={`${styles.section} ${className ?? ''}`} aria-labelledby={headingId}>{header}<div className={styles.sectionBody}>{children}</div></section>;
}
