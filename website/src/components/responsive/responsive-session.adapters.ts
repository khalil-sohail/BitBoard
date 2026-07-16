import type { AnalysisSnapshot } from '../../lib/board-arrows';
import type { ConnectionStatus } from '../../hooks/useEngine';
import type { TrainingState } from '../../lib/training-machine';
import type { PlayerColor } from '../../types/engine';
import type { MoveGrade } from '../../types/grades';
import { deriveAnalysisPresentation } from '../analysis/analysis-presentation';
import { deriveFairPlayStatus, formatClockMs } from '../fair-play/fair-play-presentation';
import { formatEvaluation } from '../live-data/evaluation-format';
import { formatPrincipalVariation } from '../live-data/move-notation';
import { deriveTrainingPresentation } from '../training/training-presentation';
import type { CompactSessionStatus } from './responsive-session.types';

export function buildFairPlayCompactStatus(input: {
  lifecycle: 'idle' | 'active' | 'completed'; connectionStatus: ConnectionStatus; queuePosition: number | null;
  searchRetryCount: number | null; waitingForSessionReady: boolean; currentTurn: PlayerColor; playerColor: PlayerColor;
  whiteMs: number; blackMs: number; untimed: boolean;
}): CompactSessionStatus {
  const status = deriveFairPlayStatus(input);
  const playerMs = input.playerColor === 'w' ? input.whiteMs : input.blackMs;
  const engineMs = input.playerColor === 'w' ? input.blackMs : input.whiteMs;
  return {
    mode: 'fair', modeLabel: 'Fair Play', primary: status.headline, secondary: status.detail,
    values: [
      { label: 'You', value: input.untimed ? '∞' : formatClockMs(playerMs) },
      { label: 'Engine', value: input.untimed ? '∞' : formatClockMs(engineMs) },
    ],
    tone: status.tone === 'error' ? 'error' : status.state === 'completed' ? 'completed' : status.tone === 'healthy' ? 'healthy' : status.tone === 'waiting' ? 'working' : 'neutral',
    attention: status.tone === 'error' ? 'error' : status.state === 'completed' ? 'completed' : 'normal',
  };
}

export function buildTrainingCompactStatus(input: {
  lifecycle: 'idle' | 'active' | 'completed'; state: TrainingState; connectionStatus: ConnectionStatus;
  currentTurn: PlayerColor; playerColor: PlayerColor; latestGrade?: MoveGrade; hasLatestFeedback: boolean;
}): CompactSessionStatus {
  const presentation = deriveTrainingPresentation({
    lifecycle: input.lifecycle, trainingState: input.state, connectionStatus: input.connectionStatus,
    currentTurn: input.currentTurn, playerColor: input.playerColor, hasLatestFeedback: input.hasLatestFeedback,
  });
  const hintStatus = input.state.status === 'waiting-player' ? input.state.hint?.status : undefined;
  const badge = hintStatus === 'searching' ? 'Hint loading' : hintStatus === 'available' ? 'Hint ready' : input.latestGrade;
  return {
    mode: 'training', modeLabel: 'Training', primary: presentation.headline,
    secondary: presentation.instruction, detail: presentation.operationalLabel, badge,
    tone: presentation.tone === 'error' ? 'error' : presentation.tone === 'working' ? 'working' : presentation.tone === 'success' ? 'completed' : 'neutral',
    attention: presentation.state === 'error' ? 'error' : presentation.state === 'completed' ? 'completed' : 'normal',
  };
}

export function buildAnalysisCompactStatus(input: {
  lifecycle: 'idle' | 'active' | 'completed'; connectionStatus: ConnectionStatus; paused: boolean;
  snapshot: AnalysisSnapshot | null;
}): CompactSessionStatus {
  const presentation = deriveAnalysisPresentation(input);
  const evaluation = formatEvaluation(input.snapshot?.lines[0]?.evaluation ?? null);
  const primaryLine = input.snapshot?.lines.find(line => line.multipv === 1) ?? input.snapshot?.lines[0];
  const formatted = primaryLine ? formatPrincipalVariation(input.snapshot?.fen, primaryLine.pv.slice(0, 3)) : null;
  return {
    mode: 'analysis', modeLabel: 'Analysis',
    primary: input.snapshot ? `${evaluation.score} · ${evaluation.meaning}` : presentation.label,
    secondary: input.snapshot ? presentation.label : presentation.detail,
    detail: formatted?.moves.length ? `${formatted.notation}: ${formatted.moves.join(' ')}` : undefined,
    badge: input.snapshot?.reportedDepth ? `Depth ${input.snapshot.reportedDepth}` : undefined,
    tone: presentation.tone === 'error' ? 'error' : presentation.tone === 'working' ? 'working' : presentation.tone === 'stopped' ? 'warning' : 'neutral',
    attention: presentation.state === 'error' ? 'error' : 'normal',
  };
}
