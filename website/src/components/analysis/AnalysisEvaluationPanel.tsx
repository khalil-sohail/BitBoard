import type { AnalysisSnapshot } from '@/lib/board-arrows';
import { EvaluationDisplay } from '../live-data/EvaluationDisplay';
import { LiveDataSection } from '../live-data/LiveDataSection';

export function AnalysisEvaluationPanel({ snapshot, stopped }: { snapshot: AnalysisSnapshot | null; stopped: boolean }) {
  return <LiveDataSection title="Current evaluation" eyebrow="White perspective" meta={<EvaluationDisplay evaluation={snapshot?.lines[0]?.evaluation ?? null} state={stopped ? 'stopped' : snapshot ? 'live' : 'loading'} />}><span className="sr-only">Evaluation values are always shown from White&apos;s perspective.</span></LiveDataSection>;
}
