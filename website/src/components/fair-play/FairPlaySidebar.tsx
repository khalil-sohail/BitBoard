"use client";

import type { Move } from 'chess.js';
import type { ConnectionStatus } from '@/hooks/useEngine';
import type { PlayerColor } from '@/types/engine';
import { FairPlayActions } from './FairPlayActions';
import { FairPlayClockPanel } from './FairPlayClockPanel';
import { FairPlayCompletedSummary } from './FairPlayCompletedSummary';
import { FairPlayHistoryPanel } from './FairPlayHistoryPanel';
import { FairPlayOperationalStatus } from './FairPlayOperationalStatus';
import { deriveFairPlayStatus, type FairPlayLifecycleStatus } from './fair-play-presentation';
import styles from './FairPlaySidebar.module.css';

export interface FairPlaySidebarProps {
  lifecycle: FairPlayLifecycleStatus;
  gameOverMessage: string;
  connectionStatus: ConnectionStatus;
  queuePosition: number | null;
  searchRetryCount: number | null;
  waitingForSessionReady: boolean;
  currentTurn: PlayerColor;
  playerColor: PlayerColor;
  whiteMs: number;
  blackMs: number;
  activeClock: PlayerColor | null;
  clockRunning: boolean;
  timeControlDisabled: boolean;
  moves: Move[];
  canResign: boolean;
  onSetup: () => void;
  onNewGame: () => void;
  onFlipBoard: () => void;
  onResign: () => void;
}

export function FairPlaySidebar(props: FairPlaySidebarProps) {
  const status = deriveFairPlayStatus({
    lifecycle: props.lifecycle,
    connectionStatus: props.connectionStatus,
    queuePosition: props.queuePosition,
    searchRetryCount: props.searchRetryCount,
    waitingForSessionReady: props.waitingForSessionReady,
    currentTurn: props.currentTurn,
    playerColor: props.playerColor,
  });
  const isComplete = status.state === 'completed';
  const turnBadge = status.state === 'active'
    ? (props.currentTurn === props.playerColor ? 'Your turn' : 'Engine turn')
    : status.state === 'error'
      ? 'Game paused'
      : 'Clocks paused';

  if (status.state === 'idle') {
    return (
      <div className={styles.idleSidebar} data-fair-play-state="idle">
        <p className={styles.eyebrow}>Fair Play</p>
        <h2>Play under normal game conditions</h2>
        <p>No evaluation, suggested moves, or engine analysis is shown during the game.</p>
        <FairPlayActions state="idle" canResign={false} onSetup={props.onSetup} onNewGame={props.onNewGame} onFlipBoard={props.onFlipBoard} onResign={props.onResign} />
      </div>
    );
  }

  return (
    <div className={styles.sidebar} data-fair-play-state={status.state}>
      {isComplete ? (
        <FairPlayCompletedSummary message={props.gameOverMessage} playerColor={props.playerColor} plyCount={props.moves.length} />
      ) : (
        <header className={styles.sessionHeader}>
          <div>
            <p className={styles.eyebrow}>Fair Play · You are {props.playerColor === 'w' ? 'White' : 'Black'}</p>
            <h2>{status.headline}</h2>
          </div>
          <span className={styles.turnBadge}>{turnBadge}</span>
        </header>
      )}

      <FairPlayClockPanel
        whiteMs={props.whiteMs}
        blackMs={props.blackMs}
        activeSide={props.activeClock}
        isRunning={props.clockRunning}
        isGameActive={props.lifecycle === 'active'}
        isCompleted={isComplete}
        playerColor={props.playerColor}
        disabled={props.timeControlDisabled}
      />

      <FairPlayHistoryPanel moves={props.moves} />
      <FairPlayOperationalStatus status={status} />
      <footer className={styles.actionFooter}>
        <FairPlayActions state={status.state} canResign={props.canResign} onSetup={props.onSetup} onNewGame={props.onNewGame} onFlipBoard={props.onFlipBoard} onResign={props.onResign} />
      </footer>
    </div>
  );
}
