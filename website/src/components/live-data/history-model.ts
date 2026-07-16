import type { MoveHistoryEntry } from './live-data.types';

export interface MoveHistoryRow {
  number: number;
  white?: MoveHistoryEntry;
  black?: MoveHistoryEntry;
}

export function groupMoveHistoryEntries(entries: readonly MoveHistoryEntry[]): MoveHistoryRow[] {
  return Array.from({ length: Math.ceil(entries.length / 2) }, (_, index) => ({ number: index + 1, white: entries[index * 2], black: entries[index * 2 + 1] }));
}
