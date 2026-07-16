import type { AnalysisSidebarState } from './analysis-presentation';
import { ContextualActions } from '../live-data/ContextualActions';

export function AnalysisActions({ state, onSetup, onStop, onResume, onReset, onFlip }: { state: AnalysisSidebarState; onSetup: () => void; onStop: () => void; onResume: () => void; onReset: () => void; onFlip: () => void }) {
  if (state === 'idle') return <ContextualActions actions={[{ id: 'setup', label: 'Set up Analysis', variant: 'primary', onAction: onSetup }]} />;
  const stopped = state === 'stopped';
  const error = state === 'error' || state === 'reconnecting';
  return <ContextualActions actions={[
    { id: 'primary', label: error ? 'Choose another position' : stopped ? 'Resume analysis' : 'Stop analysis', variant: 'primary', onAction: error ? onSetup : stopped ? onResume : onStop },
    ...(!error ? [{ id: 'position', label: 'Change position', onAction: onSetup }] : []),
    { id: 'flip', label: 'Flip board', onAction: onFlip }, { id: 'reset', label: 'Reset to start', onAction: onReset },
  ]} />;
}
