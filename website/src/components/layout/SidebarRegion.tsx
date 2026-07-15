import type { ReactNode } from 'react';
import styles from './ProductLayout.module.css';

export function SidebarRegion({ children }: { children: ReactNode }) {
  return (
    <aside className={styles.sidebar} aria-label="Session panel">
      <div className="flex flex-col gap-3">{children}</div>
    </aside>
  );
}
