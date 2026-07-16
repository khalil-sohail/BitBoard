"use client";

import { useMemo } from 'react';
import type { Move } from 'chess.js';
import { LiveDataSection } from '../live-data/LiveDataSection';
import { MoveHistoryTable } from '../live-data/MoveHistoryTable';
import { buildAnalysisHistoryEntries } from './analysis-live-data';

export function AnalysisHistoryPanel({ moves, cursorPly, onNavigate }: { moves: Move[]; cursorPly: number; onNavigate: (ply: number) => void }) {
  const entries = useMemo(() => buildAnalysisHistoryEntries(moves, cursorPly), [cursorPly, moves]);
  return <LiveDataSection title="Move history" meta={`${moves.length} plies`}><MoveHistoryTable entries={entries} caption="Analysis move history; select a move to inspect its position" emptyText="Moves made on the board or loaded from PGN will appear here." onSelect={onNavigate} /></LiveDataSection>;
}
