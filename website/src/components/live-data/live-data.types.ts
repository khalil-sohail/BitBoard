import type { NormalizedEvaluation } from '../../lib/engine-evaluation';

export type LiveDataTone = 'neutral' | 'healthy' | 'loading' | 'working' | 'paused' | 'warning' | 'error' | 'completed';

export interface MoveHistoryMarker {
  text: string;
  accessibleLabel?: string;
  tone?: 'neutral' | 'positive' | 'warning' | 'negative' | 'hint';
}

export interface MoveHistoryEntry {
  ply: number;
  notation: string;
  accessibleLabel?: string;
  markers?: readonly MoveHistoryMarker[];
  selected?: boolean;
  selectable?: boolean;
}

export interface PrincipalVariationView {
  rank: number;
  evaluation: NormalizedEvaluation | null;
  moves: readonly string[];
  depth?: number | null;
}

export interface EngineMetricsView {
  depth?: number | null;
  selectiveDepth?: number | null;
  nodes?: number;
  timeMs?: number;
}

export interface ContextualAction {
  id: string;
  label: string;
  onAction: () => void;
  variant?: 'primary' | 'secondary' | 'destructive';
  disabled?: boolean;
  disabledReason?: string;
  loading?: boolean;
}
