"use client";

import { useState } from 'react';
import type { FairPlaySidebarState } from './fair-play-presentation';
import styles from './FairPlaySidebar.module.css';

interface FairPlayActionsProps {
  state: FairPlaySidebarState;
  canResign: boolean;
  onSetup: () => void;
  onNewGame: () => void;
  onFlipBoard: () => void;
  onResign: () => void;
}

export function FairPlayActions(props: FairPlayActionsProps) {
  const [confirmingResign, setConfirmingResign] = useState(false);

  if (props.state === 'idle') {
    return <button type="button" className={styles.primaryAction} onClick={props.onSetup}>Set up game</button>;
  }

  if (props.state === 'completed' || props.state === 'error') {
    return (
      <div className={styles.actions}>
        <button type="button" className={styles.primaryAction} onClick={props.onNewGame}>New game</button>
        <button type="button" className={styles.secondaryAction} onClick={props.onFlipBoard}>Flip board</button>
      </div>
    );
  }

  if (props.state === 'starting' || props.state === 'connecting' || props.state === 'queued') {
    return <button type="button" className={styles.secondaryAction} onClick={props.onFlipBoard}>Flip board</button>;
  }

  if (confirmingResign) {
    return (
      <div className={styles.resignConfirmation} role="group" aria-label="Confirm resignation">
        <p>Resign this game?</p>
        <div>
          <button type="button" className={styles.secondaryAction} onClick={() => setConfirmingResign(false)}>Keep playing</button>
          <button type="button" className={styles.dangerAction} onClick={props.onResign}>Confirm resign</button>
        </div>
      </div>
    );
  }

  return (
    <div className={styles.actions}>
      <button type="button" className={styles.secondaryAction} onClick={props.onFlipBoard}>Flip board</button>
      {props.canResign && <button type="button" className={styles.dangerAction} onClick={() => setConfirmingResign(true)}>Resign</button>}
    </div>
  );
}
