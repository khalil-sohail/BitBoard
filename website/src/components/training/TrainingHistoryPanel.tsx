"use client";

import { useMemo } from 'react';
import type { Move } from 'chess.js';
import type { GradedMove } from '@/types/grades';
import { LiveDataSection } from '../live-data/LiveDataSection';
import { MoveHistoryTable } from '../live-data/MoveHistoryTable';
import { buildTrainingHistoryEntries } from './training-live-data';

export function TrainingHistoryPanel({ moves, grades }: { moves: Move[]; grades: GradedMove[] }) {
  const entries = useMemo(() => buildTrainingHistoryEntries(moves, grades), [grades, moves]);
  return <LiveDataSection title="Graded history" meta={`${grades.length} reviewed`}><MoveHistoryTable entries={entries} caption="Training move history with grades for reviewed moves" emptyText="Your moves and grades will appear here." /></LiveDataSection>;
}
