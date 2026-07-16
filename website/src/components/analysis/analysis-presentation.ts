import type { ConnectionStatus } from '../../hooks/useEngine';
import type { AnalysisSnapshot } from '../../lib/board-arrows';
import type { NormalizedEvaluation } from '../../lib/engine-evaluation';
import { formatEvaluation } from '../live-data/evaluation-format';

export type AnalysisSidebarState =
  | 'idle'
  | 'loading-position'
  | 'starting-analysis'
  | 'analyzing'
  | 'stopped'
  | 'position-changed'
  | 'reconnecting'
  | 'error';

export interface AnalysisPresentation {
  state: AnalysisSidebarState;
  label: string;
  detail: string;
  tone: 'normal' | 'working' | 'stopped' | 'error';
}

export function deriveAnalysisPresentation(input: {
  lifecycle: 'idle' | 'active' | 'completed';
  connectionStatus: ConnectionStatus;
  paused: boolean;
  snapshot: AnalysisSnapshot | null;
}): AnalysisPresentation {
  if (input.lifecycle === 'idle') {
    return { state: 'idle', label: 'Ready to analyze', detail: 'Choose a source position to begin.', tone: 'normal' };
  }
  if (input.connectionStatus === 'error' || input.connectionStatus === 'session_expired') {
    return { state: 'error', label: 'Engine unavailable', detail: 'The current position is preserved. Retry or choose another position.', tone: 'error' };
  }
  if (input.connectionStatus === 'disconnected') {
    return { state: 'reconnecting', label: 'Reconnecting…', detail: 'Waiting for an engine session for this position.', tone: 'working' };
  }
  if (input.connectionStatus === 'connecting' || input.connectionStatus === 'queued') {
    return { state: 'loading-position', label: input.connectionStatus === 'queued' ? 'Waiting for an engine session…' : 'Connecting…', detail: 'The position is loaded; analysis will start when the engine is ready.', tone: 'working' };
  }
  if (input.paused) {
    return { state: 'stopped', label: 'Analysis stopped', detail: input.snapshot ? 'Showing the last correlated snapshot.' : 'Resume to analyze this position.', tone: 'stopped' };
  }
  if (input.connectionStatus === 'analyzing') {
    return input.snapshot
      ? { state: 'analyzing', label: 'Analyzing', detail: 'Evaluation and lines belong to the current position.', tone: 'working' }
      : { state: 'starting-analysis', label: 'Starting analysis…', detail: 'Waiting for the first correlated engine update.', tone: 'working' };
  }
  if (!input.snapshot) {
    return { state: 'position-changed', label: 'Position updated', detail: 'Waiting for analysis of the current position.', tone: 'working' };
  }
  return { state: 'analyzing', label: 'Analysis complete', detail: 'Showing the final snapshot at the requested limit.', tone: 'normal' };
}

export function sourceLabel(source: 'default' | 'fen' | 'pgn' | 'board'): string {
  if (source === 'fen') return 'FEN position';
  if (source === 'pgn') return 'PGN game';
  if (source === 'board') return 'Board position';
  return 'Starting position';
}

export function evaluationText(evaluation: NormalizedEvaluation | null): { score: string; meaning: string; accessible: string } {
  return formatEvaluation(evaluation);
}
