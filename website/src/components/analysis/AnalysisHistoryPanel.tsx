"use client";

import { useEffect, useRef } from 'react';
import type { Move } from 'chess.js';
import styles from './AnalysisSidebar.module.css';

export function AnalysisHistoryPanel({ moves, cursorPly, onNavigate }: { moves: Move[]; cursorPly: number; onNavigate: (ply: number) => void }) {
  const currentRef = useRef<HTMLButtonElement | null>(null);
  useEffect(() => { currentRef.current?.scrollIntoView({ block: 'nearest' }); }, [cursorPly]);
  const rows = Array.from({ length: Math.ceil(moves.length / 2) }, (_, index) => ({ number: index + 1, white: moves[index * 2], black: moves[index * 2 + 1], whitePly: index * 2 + 1, blackPly: index * 2 + 2 }));
  return <section className={styles.historyPanel} aria-labelledby="analysis-history-title">
    <div className={styles.sectionHeading}><h3 id="analysis-history-title">Move history</h3><span>{moves.length} plies</span></div>
    <div className={styles.historyScroll}>
      {!rows.length ? <p className={styles.emptyText}>Moves made on the board or loaded from PGN will appear here.</p> : <table>
        <caption className="sr-only">Analysis move history; select a move to inspect its position</caption>
        <thead className="sr-only"><tr><th>Move</th><th>White</th><th>Black</th></tr></thead>
        <tbody>{rows.map(row => <tr key={row.number}>
          <th scope="row">{row.number}.</th>
          <MoveButton move={row.white} ply={row.whitePly} selected={cursorPly === row.whitePly} onNavigate={onNavigate} currentRef={currentRef} />
          <MoveButton move={row.black} ply={row.blackPly} selected={cursorPly === row.blackPly} onNavigate={onNavigate} currentRef={currentRef} />
        </tr>)}</tbody>
      </table>}
    </div>
  </section>;
}

function MoveButton({ move, ply, selected, onNavigate, currentRef }: { move?: Move; ply: number; selected: boolean; onNavigate: (ply: number) => void; currentRef: React.MutableRefObject<HTMLButtonElement | null> }) {
  return <td>{move ? <button ref={selected ? currentRef : undefined} type="button" aria-current={selected ? 'step' : undefined} onClick={() => onNavigate(ply)}>{move.san}</button> : null}</td>;
}
