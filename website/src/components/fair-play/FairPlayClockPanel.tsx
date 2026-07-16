"use client";

import type { PlayerColor } from '@/types/engine';
import { formatClockMs } from './fair-play-presentation';
import styles from './FairPlaySidebar.module.css';

interface FairPlayClockPanelProps {
  whiteMs: number;
  blackMs: number;
  activeSide: PlayerColor | null;
  isRunning: boolean;
  isGameActive: boolean;
  isCompleted: boolean;
  playerColor: PlayerColor;
  disabled: boolean;
}

function Clock({
  label,
  color,
  ms,
  active,
  disabled,
}: {
  label: string;
  color: PlayerColor;
  ms: number;
  active: boolean;
  disabled: boolean;
}) {
  const displayed = disabled ? '∞' : formatClockMs(ms);
  const colorLabel = color === 'w' ? 'White' : 'Black';
  const isLow = !disabled && ms > 0 && ms < 10_000;
  return (
    <div className={`${styles.clock} ${active ? styles.clockActive : ''} ${isLow ? styles.clockLow : ''}`} aria-label={`${label}, ${colorLabel}, ${displayed}${active ? ', active clock' : ''}`}>
      <div className={styles.clockLabel}>
        <span aria-hidden="true">{color === 'w' ? '♔' : '♚'}</span>
        <span>{label}</span>
      </div>
      <time className={styles.clockTime}>{displayed}</time>
      <span className={styles.clockState}>{active ? 'Clock running' : colorLabel}</span>
    </div>
  );
}

export function FairPlayClockPanel(props: FairPlayClockPanelProps) {
  const engineColor: PlayerColor = props.playerColor === 'w' ? 'b' : 'w';
  const playerMs = props.playerColor === 'w' ? props.whiteMs : props.blackMs;
  const engineMs = engineColor === 'w' ? props.whiteMs : props.blackMs;
  const clockStatus = props.disabled
    ? 'Untimed game'
    : props.isCompleted
      ? 'Final clocks'
      : props.isRunning
        ? 'Clock running'
        : 'Clocks paused';
  const active = (side: PlayerColor) => props.isGameActive && props.isRunning && props.activeSide === side;

  return (
    <section className={styles.clockPanel} aria-labelledby="fair-play-clocks-title">
      <div className={styles.sectionHeading}>
        <h3 id="fair-play-clocks-title">Clocks</h3>
        <span>{clockStatus}</span>
      </div>
      <div className={styles.clockGrid}>
        <Clock label="You" color={props.playerColor} ms={playerMs} active={active(props.playerColor)} disabled={props.disabled} />
        <Clock label="Engine" color={engineColor} ms={engineMs} active={active(engineColor)} disabled={props.disabled} />
      </div>
    </section>
  );
}
