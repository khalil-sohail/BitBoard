"use client";

import { useState } from 'react';
import type { FairPlaySidebarState } from './fair-play-presentation';
import { ContextualActions } from '../live-data/ContextualActions';
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
    return <ContextualActions actions={[{ id: 'setup', label: 'Set up game', variant: 'primary', onAction: props.onSetup }]} />;
  }

  if (props.state === 'completed' || props.state === 'error') {
    return (
      <ContextualActions actions={[{ id: 'new', label: 'New game', variant: 'primary', onAction: props.onNewGame }, { id: 'flip', label: 'Flip board', onAction: props.onFlipBoard }]} />
    );
  }

  if (props.state === 'starting' || props.state === 'connecting' || props.state === 'queued') {
    return <ContextualActions actions={[{ id: 'flip', label: 'Flip board', onAction: props.onFlipBoard }]} />;
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
    <ContextualActions actions={[{ id: 'flip', label: 'Flip board', onAction: props.onFlipBoard }, ...(props.canResign ? [{ id: 'resign', label: 'Resign', variant: 'destructive' as const, onAction: () => setConfirmingResign(true) }] : [])]} />
  );
}
