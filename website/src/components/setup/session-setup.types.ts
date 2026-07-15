import type { DifficultyLevel, PlayerColor, TimeControl } from '@/types/engine';

export interface NewGameConfig {
  playerColor: PlayerColor | 'random';
  difficulty: DifficultyLevel;
  timeControl: TimeControl;
  maxDepth?: number;
}
