import type { ContextualAction } from './live-data.types';
import styles from './LiveData.module.css';

export function ContextualActions({ actions, label = 'Session actions' }: { actions: readonly ContextualAction[]; label?: string }) {
  return <div className={styles.actions} data-live-actions role="group" aria-label={label}>{actions.map(action => <button
    key={action.id} type="button" data-variant={action.variant ?? 'secondary'} disabled={action.disabled || action.loading}
    title={action.disabled ? action.disabledReason : undefined} aria-busy={action.loading || undefined} onClick={action.onAction}
  >{action.loading ? `${action.label}…` : action.label}</button>)}</div>;
}
