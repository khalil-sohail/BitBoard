"use client";

import type { Move } from 'chess.js';
import type { ConnectionStatus } from '@/hooks/useEngine';
import type { ProgressiveHintView } from '@/lib/training-hint';
import type { TrainingState } from '@/lib/training-machine';
import type { EngineInfo, PlayerColor } from '@/types/engine';
import type { GradedMove } from '@/types/grades';
import { EvalGraph } from '@/components/panels/EvalGraph';
import { OperationalStatus } from '@/components/live-data/OperationalStatus';
import { TrainingActions } from './TrainingActions';
import { TrainingAnalysisPanel } from './TrainingAnalysisPanel';
import { TrainingCompletedSummary } from './TrainingCompletedSummary';
import { TrainingFeedbackPanel } from './TrainingFeedbackPanel';
import { TrainingHintPanel } from './TrainingHintPanel';
import { TrainingHistoryPanel } from './TrainingHistoryPanel';
import { TrainingTaskPanel } from './TrainingTaskPanel';
import { deriveTrainingPresentation } from './training-presentation';
import styles from './TrainingSidebar.module.css';

export interface TrainingSidebarProps {
  lifecycle: 'idle' | 'active' | 'completed';
  gameOverMessage: string;
  state: TrainingState;
  connectionStatus: ConnectionStatus;
  currentTurn: PlayerColor;
  playerColor: PlayerColor;
  difficulty: string;
  analysisInfo: EngineInfo | null;
  hintView: ProgressiveHintView | null;
  canRequestHint: boolean;
  moves: Move[];
  grades: GradedMove[];
  evaluationGraph: { ply: number; score: number }[];
  canUndo: boolean;
  canResign: boolean;
  onRequestHint: () => void;
  onSetup: () => void;
  onNewGame: () => void;
  onUndo: () => void;
  onFlip: () => void;
  onResign: () => void;
}

export function TrainingSidebar(props: TrainingSidebarProps) {
  const presentation = deriveTrainingPresentation({
    lifecycle: props.lifecycle,
    trainingState: props.state,
    connectionStatus: props.connectionStatus,
    currentTurn: props.currentTurn,
    playerColor: props.playerColor,
    hasLatestFeedback: props.grades.length > 0,
  });
  const hint = props.state.status === 'waiting-player' ? props.state.hint : undefined;
  const reviewing = props.state.status === 'reviewing-player-move';
  const playerCanAct = presentation.state === 'player-turn' && hint?.status !== 'searching';

  if (presentation.state === 'idle') {
    return <div className={styles.idleSidebar} data-training-state="idle">
      <p className={styles.sectionEyebrow}>Training</p>
      <h2>{presentation.headline}</h2>
      <p>{presentation.instruction}</p>
      <TrainingActions state="idle" canUndo={false} canResign={false} onSetup={props.onSetup} onNewGame={props.onNewGame} onUndo={props.onUndo} onFlip={props.onFlip} onResign={props.onResign} />
    </div>;
  }

  return <div className={styles.sidebar} data-training-state={presentation.state}>
    <header className={styles.sessionHeader}>
      <div><p className={styles.sectionEyebrow}>Training · You are {props.playerColor === 'w' ? 'White' : 'Black'}</p><h2>{presentation.operationalLabel}</h2></div>
      <span>{props.difficulty}</span>
    </header>
    {presentation.state === 'completed' ? <TrainingCompletedSummary result={props.gameOverMessage} grades={props.grades} plyCount={props.moves.length} /> : <TrainingTaskPanel presentation={presentation} />}
    <TrainingFeedbackPanel moves={props.moves} grades={props.grades} reviewing={reviewing} />
    {presentation.state !== 'completed' && <TrainingHintPanel hint={hint} hintView={props.hintView} canRequest={props.canRequestHint} onRequest={props.onRequestHint} />}
    <TrainingAnalysisPanel info={props.analysisInfo} reviewing={reviewing} />
    <TrainingHistoryPanel moves={props.moves} grades={props.grades} />
    <details className={styles.graphSection} open={presentation.state === 'completed'}>
      <summary>Evaluation history</summary>
      <EvalGraph data={props.evaluationGraph} />
    </details>
    <OperationalStatus title={presentation.operationalLabel} tone={presentation.tone === 'error' ? 'error' : presentation.tone === 'working' ? 'working' : presentation.tone === 'success' ? 'healthy' : 'neutral'} />
    <footer className={styles.actionFooter}><TrainingActions state={presentation.state} canUndo={props.canUndo && playerCanAct} canResign={props.canResign} onSetup={props.onSetup} onNewGame={props.onNewGame} onUndo={props.onUndo} onFlip={props.onFlip} onResign={props.onResign} /></footer>
  </div>;
}
