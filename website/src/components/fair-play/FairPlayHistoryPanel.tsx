"use client";

import { useEffect, useRef } from 'react';
import type { Move } from 'chess.js';
import styles from './FairPlaySidebar.module.css';

export function FairPlayHistoryPanel({ moves }: { moves: Move[] }) {
  const scrollRef = useRef<HTMLDivElement>(null);
  const followLatestRef = useRef(true);

  useEffect(() => {
    const container = scrollRef.current;
    if (!container || !followLatestRef.current) return;
    container.scrollTo({ top: container.scrollHeight, behavior: 'smooth' });
  }, [moves.length]);

  const rows = [];
  for (let index = 0; index < moves.length; index += 2) {
    rows.push({ moveNumber: index / 2 + 1, white: moves[index], black: moves[index + 1] ?? null, whiteIndex: index, blackIndex: index + 1 });
  }

  return (
    <section className={styles.historyPanel} aria-labelledby="fair-play-history-title">
      <div className={styles.sectionHeading}>
        <h3 id="fair-play-history-title">Moves</h3>
        <span>{moves.length === 0 ? 'No moves' : `${Math.ceil(moves.length / 2)} full move${moves.length > 2 ? 's' : ''}`}</span>
      </div>
      <div
        ref={scrollRef}
        className={styles.historyScroll}
        onScroll={event => {
          const element = event.currentTarget;
          followLatestRef.current = element.scrollHeight - element.scrollTop - element.clientHeight < 32;
        }}
      >
        {rows.length === 0 ? (
          <p className={styles.emptyHistory}>Moves will appear here after the game begins.</p>
        ) : (
          <table className={styles.moveTable}>
            <caption className="sr-only">Fair Play move history</caption>
            <thead className="sr-only"><tr><th>Move</th><th>White</th><th>Black</th></tr></thead>
            <tbody>
              {rows.map(row => {
                const whiteLatest = row.whiteIndex === moves.length - 1;
                const blackLatest = row.blackIndex === moves.length - 1;
                return (
                  <tr key={row.moveNumber}>
                    <th scope="row">{row.moveNumber}.</th>
                    <td><span aria-current={whiteLatest ? 'step' : undefined} className={whiteLatest ? styles.latestMove : undefined}>{row.white.san}{whiteLatest && <span className="sr-only">, latest move</span>}</span></td>
                    <td>{row.black && <span aria-current={blackLatest ? 'step' : undefined} className={blackLatest ? styles.latestMove : undefined}>{row.black.san}{blackLatest && <span className="sr-only">, latest move</span>}</span>}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        )}
      </div>
    </section>
  );
}
