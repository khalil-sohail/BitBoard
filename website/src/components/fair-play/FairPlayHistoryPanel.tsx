"use client";

import { useMemo } from 'react';
import type { Move } from 'chess.js';
import { LiveDataSection } from '../live-data/LiveDataSection';
import { MoveHistoryTable } from '../live-data/MoveHistoryTable';
import { buildFairPlayHistoryEntries } from './fair-play-live-data';

export function FairPlayHistoryPanel({ moves }: { moves: Move[] }) {
  const entries = useMemo(() => buildFairPlayHistoryEntries(moves), [moves]);
  const meta = moves.length === 0 ? 'No moves' : `${Math.ceil(moves.length / 2)} full move${moves.length > 2 ? 's' : ''}`;
  return <LiveDataSection title="Moves" meta={meta}><MoveHistoryTable entries={entries} caption="Fair Play move history" emptyText="Moves will appear here after the game begins." /></LiveDataSection>;
}
