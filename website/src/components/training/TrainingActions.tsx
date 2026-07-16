import type { TrainingSidebarState } from './training-presentation';
import { ContextualActions } from '../live-data/ContextualActions';

export function TrainingActions({ state, canUndo, canResign, onSetup, onNewGame, onUndo, onFlip, onResign }: {
  state: TrainingSidebarState;
  canUndo: boolean;
  canResign: boolean;
  onSetup: () => void;
  onNewGame: () => void;
  onUndo: () => void;
  onFlip: () => void;
  onResign: () => void;
}) {
  if (state === 'idle') {
    return <ContextualActions actions={[{ id: 'setup', label: 'Set up Training', variant: 'primary', onAction: onSetup }]} />;
  }
  if (state === 'completed' || state === 'error') {
    return <ContextualActions actions={[{ id: 'new', label: 'New Training Game', variant: 'primary', onAction: onNewGame }, { id: 'flip', label: 'Flip board', onAction: onFlip }]} />;
  }
  return <ContextualActions actions={[
    { id: 'flip', label: 'Flip board', onAction: onFlip },
    { id: 'undo', label: 'Undo last turn', onAction: onUndo, disabled: !canUndo, disabledReason: 'Undo is available only on your turn when no engine work is pending.' },
    { id: 'new', label: 'New game', onAction: onNewGame },
    ...(canResign ? [{ id: 'resign', label: 'Resign', variant: 'destructive' as const, onAction: onResign }] : []),
  ]} />;
}
