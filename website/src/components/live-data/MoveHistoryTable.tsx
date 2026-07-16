"use client";

import { memo, useEffect, useMemo, useRef } from 'react';
import type { MoveHistoryEntry } from './live-data.types';
import { groupMoveHistoryEntries } from './history-model';
import { EmptyState } from './EmptyState';
import styles from './LiveData.module.css';

export const MoveHistoryTable = memo(function MoveHistoryTable({ entries, caption, emptyText, onSelect }: { entries: readonly MoveHistoryEntry[]; caption: string; emptyText: string; onSelect?: (ply: number) => void }) {
  const scrollRef = useRef<HTMLDivElement>(null);
  const selectedRef = useRef<HTMLButtonElement | null>(null);
  const followLatestRef = useRef(true);
  const selectedPly = entries.find(entry => entry.selected)?.ply;
  const rows = useMemo(() => groupMoveHistoryEntries(entries), [entries]);
  useEffect(() => { if (followLatestRef.current) scrollRef.current?.scrollTo({ top: scrollRef.current.scrollHeight, behavior: 'smooth' }); }, [entries.length]);
  useEffect(() => { selectedRef.current?.scrollIntoView({ block: 'nearest' }); }, [selectedPly]);
  return <div ref={scrollRef} className={styles.historyScroll} data-live-history onScroll={event => { const element = event.currentTarget; followLatestRef.current = element.scrollHeight - element.scrollTop - element.clientHeight < 32; }}>
    {!rows.length ? <EmptyState>{emptyText}</EmptyState> : <table className={styles.historyTable}>
      <caption className="sr-only">{caption}</caption><thead className="sr-only"><tr><th>Move</th><th>White</th><th>Black</th></tr></thead>
      <tbody>{rows.map(row => <tr key={row.number}><th scope="row">{row.number}.</th><MoveCell entry={row.white} latest={row.white?.ply === entries.length} selectedRef={selectedRef} onSelect={onSelect} /><MoveCell entry={row.black} latest={row.black?.ply === entries.length} selectedRef={selectedRef} onSelect={onSelect} /></tr>)}</tbody>
    </table>}
  </div>;
});

function MoveCell({ entry, latest, selectedRef, onSelect }: { entry?: MoveHistoryEntry; latest: boolean; selectedRef: React.MutableRefObject<HTMLButtonElement | null>; onSelect?: (ply: number) => void }) {
  if (!entry) return <td />;
  const content = <><strong>{entry.notation}</strong>{entry.markers?.map(marker => <span key={`${entry.ply}:${marker.text}`} className={styles.historyMarker} data-tone={marker.tone ?? 'neutral'}>{marker.text}{marker.accessibleLabel ? <span className="sr-only">, {marker.accessibleLabel}</span> : null}</span>)}{latest ? <span className="sr-only">, latest move</span> : null}</>;
  const current = entry.selected ? 'step' : latest ? 'true' : undefined;
  return <td>{entry.selectable && onSelect ? <button ref={entry.selected ? selectedRef : undefined} type="button" aria-current={current} aria-label={entry.accessibleLabel} onClick={() => onSelect(entry.ply)}>{content}</button> : <span aria-current={current} aria-label={entry.accessibleLabel}>{content}</span>}</td>;
}
