"use client";

import { useSessionController } from '@/session/useSessionController';
import { AnalysisSetupForm } from './AnalysisSetupForm';
import { FairPlaySetupForm } from './FairPlaySetupForm';
import { TrainingSetupForm } from './TrainingSetupForm';

export function SessionSetupHost() {
  const session = useSessionController();
  if (!session.setup.isOpen) return null;
  if (session.mode.isAnalysis) return <AnalysisSetupForm session={session} />;
  if (session.mode.isTraining) return <TrainingSetupForm session={session} />;
  return <FairPlaySetupForm session={session} />;
}
