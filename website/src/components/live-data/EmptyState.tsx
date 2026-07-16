import type { ReactNode } from 'react';
import styles from './LiveData.module.css';

export function EmptyState({ children, action }: { children: ReactNode; action?: ReactNode }) {
  return <div className={styles.emptyState}><p>{children}</p>{action}</div>;
}

export function LoadingState({ children }: { children: ReactNode }) {
  return <div className={styles.loadingState} role="status"><span aria-hidden="true" /><p>{children}</p></div>;
}
