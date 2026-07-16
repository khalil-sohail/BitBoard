import type { AnalysisSidebarState } from './analysis-presentation';
import styles from './AnalysisSidebar.module.css';

export function AnalysisActions({ state, onSetup, onStop, onResume, onReset, onFlip }: { state: AnalysisSidebarState; onSetup: () => void; onStop: () => void; onResume: () => void; onReset: () => void; onFlip: () => void }) {
  if (state === 'idle') return <button type="button" className={styles.primaryAction} onClick={onSetup}>Set up Analysis</button>;
  const stopped = state === 'stopped';
  const error = state === 'error' || state === 'reconnecting';
  return <div className={styles.actionGrid}>
    <button type="button" className={styles.primaryAction} onClick={error ? onSetup : stopped ? onResume : onStop}>{error ? 'Choose another position' : stopped ? 'Resume analysis' : 'Stop analysis'}</button>
    {!error ? <button type="button" onClick={onSetup}>Change position</button> : null}
    <button type="button" onClick={onFlip}>Flip board</button>
    <button type="button" onClick={onReset}>Reset to start</button>
  </div>;
}
