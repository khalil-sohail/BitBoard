import type { GameMode } from '../../types/engine';

export type CompactStatusTone = 'neutral' | 'healthy' | 'working' | 'warning' | 'error' | 'completed';
export type PanelAttention = 'normal' | 'completed' | 'error';

export interface CompactStatusValue {
  label: string;
  value: string;
}

export interface CompactSessionStatus {
  mode: GameMode;
  modeLabel: string;
  primary: string;
  secondary?: string;
  detail?: string;
  values?: readonly CompactStatusValue[];
  badge?: string;
  tone: CompactStatusTone;
  attention: PanelAttention;
}
