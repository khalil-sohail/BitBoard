import type { TrainingSidebarState } from './training-presentation';
import styles from './TrainingSidebar.module.css';

export function TrainingActions({ state, canUndo, canResign, onSetup, onNewGame, onUndo, onFlip, onResign }: {
  state: TrainingSidebarState;
  canUndo: boolean;
  canResign: boolean;
  onSetup: () => void;
  onNewGame: () => void;
  onUndo: () => void;
  onFlip: () => void;
  onResign: () => void;
}) {
  if (state === 'idle') {
    return <button type="button" className={styles.primaryAction} onClick={onSetup}>Set up Training</button>;
  }
  if (state === 'completed' || state === 'error') {
    return <div className={styles.actionGrid}>
      <button type="button" className={styles.primaryAction} onClick={onNewGame}>New Training Game</button>
      <button type="button" className={styles.secondaryAction} onClick={onFlip}>Flip board</button>
    </div>;
  }
  return <div className={styles.actionGrid}>
    <button type="button" className={styles.secondaryAction} onClick={onFlip}>Flip board</button>
    <button type="button" className={styles.secondaryAction} onClick={onUndo} disabled={!canUndo} title={!canUndo ? 'Undo is available only on your turn when no engine work is pending.' : undefined}>Undo last turn</button>
    <button type="button" className={styles.secondaryAction} onClick={onNewGame}>New game</button>
    {canResign ? <button type="button" className={styles.dangerAction} onClick={onResign}>Resign</button> : null}
  </div>;
}
