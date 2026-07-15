import type { ReactNode } from 'react';
import styles from './SessionSetup.module.css';

interface SetupSectionProps {
  title: string;
  description?: string;
  children: ReactNode;
}

export function SetupSection({ title, description, children }: SetupSectionProps) {
  return (
    <section className={styles.section}>
      <div className="mb-3">
        <h3 className="text-sm font-semibold text-foreground">{title}</h3>
        {description && <p className="mt-1 text-xs leading-relaxed text-muted">{description}</p>}
      </div>
      {children}
    </section>
  );
}
