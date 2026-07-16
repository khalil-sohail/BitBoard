"use client";

import type { Move } from 'chess.js';
import type { ConnectionStatus } from '@/hooks/useEngine';
import type { AnalysisSnapshot } from '@/lib/board-arrows';
import type { EngineInfo, PlayerColor } from '@/types/engine';
import { AnalysisActions } from './AnalysisActions';
import { AnalysisEvaluationPanel } from './AnalysisEvaluationPanel';
import { AnalysisHistoryPanel } from './AnalysisHistoryPanel';
import { AnalysisMetricsPanel } from './AnalysisMetricsPanel';
import { AnalysisNavigationPanel } from './AnalysisNavigationPanel';
import { AnalysisSearchControls } from './AnalysisSearchControls';
import { PrincipalVariationPanel } from './PrincipalVariationPanel';
import { OperationalStatus } from '../live-data/OperationalStatus';
import { deriveAnalysisPresentation, sourceLabel } from './analysis-presentation';
import styles from './AnalysisSidebar.module.css';

export interface AnalysisSidebarProps {
  lifecycle: 'idle' | 'active' | 'completed'; connectionStatus: ConnectionStatus; queuePosition: number | null;
  paused: boolean; source: 'default' | 'fen' | 'pgn' | 'board'; positionTurn: PlayerColor; positionFen: string;
  snapshot: AnalysisSnapshot | null; engineInfo: EngineInfo | null; moves: Move[]; cursorPly: number; historyLength: number;
  depth: number; multiPv: number; controlsDisabled: boolean;
  onNavigate: (ply: number) => void; onDepthChange: (value: number) => void; onMultiPvChange: (value: number) => void;
  onSetup: () => void; onStop: () => void; onResume: () => void; onReset: () => void; onFlip: () => void;
}

export function AnalysisSidebar(props: AnalysisSidebarProps) {
  const presentation = deriveAnalysisPresentation({ lifecycle: props.lifecycle, connectionStatus: props.connectionStatus, paused: props.paused, snapshot: props.snapshot });
  if (presentation.state === 'idle') return <div className={styles.idleSidebar} data-analysis-state="idle">
    <p className={styles.eyebrow}>Analysis</p><h2>Explore any position</h2><p>Load a position and inspect engine lines without moving pieces automatically.</p>
    <AnalysisActions state="idle" onSetup={props.onSetup} onStop={props.onStop} onResume={props.onResume} onReset={props.onReset} onFlip={props.onFlip} />
  </div>;
  return <div
    className={styles.sidebar}
    data-analysis-state={presentation.state}
    data-analysis-request-id={props.snapshot?.requestId}
    data-analysis-snapshot-fen={props.snapshot?.fen}
  >
    <header className={styles.sessionHeader}>
      <div><p className={styles.eyebrow}>Analysis · {sourceLabel(props.source)}</p><h2>{presentation.label}</h2></div>
      <span>{props.positionTurn === 'w' ? 'White' : 'Black'} to move</span>
    </header>
    <AnalysisEvaluationPanel snapshot={props.snapshot} stopped={presentation.state === 'stopped'} />
    <PrincipalVariationPanel snapshot={props.snapshot} requestedLines={props.multiPv} />
    <AnalysisNavigationPanel cursorPly={props.cursorPly} historyLength={props.historyLength} onNavigate={props.onNavigate} />
    <AnalysisSearchControls depth={props.depth} multiPv={props.multiPv} disabled={props.controlsDisabled} onDepthChange={props.onDepthChange} onMultiPvChange={props.onMultiPvChange} />
    <AnalysisHistoryPanel moves={props.moves} cursorPly={props.cursorPly} onNavigate={props.onNavigate} />
    <AnalysisMetricsPanel snapshot={props.snapshot} info={props.engineInfo} />
    <OperationalStatus title={presentation.label} description={`${props.connectionStatus === 'queued' && props.queuePosition ? `Queue position ${props.queuePosition}. ` : ''}${presentation.detail}`} tone={presentation.tone === 'error' ? 'error' : presentation.tone === 'working' ? 'working' : presentation.tone === 'stopped' ? 'paused' : 'neutral'} />
    <footer><AnalysisActions state={presentation.state} onSetup={props.onSetup} onStop={props.onStop} onResume={props.onResume} onReset={props.onReset} onFlip={props.onFlip} /></footer>
    <span className="sr-only">Current position identity: {props.positionFen}</span>
  </div>;
}
