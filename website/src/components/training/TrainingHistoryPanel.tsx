"use client";

import { useEffect, useRef } from 'react';
import type { Move } from 'chess.js';
import type { GradedMove } from '@/types/grades';
import styles from './TrainingSidebar.module.css';

export function TrainingHistoryPanel({ moves, grades }: { moves: Move[]; grades: GradedMove[] }) {
  const scrollRef = useRef<HTMLDivElement>(null);
  const followLatestRef = useRef(true);
  const gradeMap = new Map(grades.filter(Boolean).map(grade => [grade.moveIndex, grade]));
  const rows = Array.from({ length: Math.ceil(moves.length / 2) }, (_, index) => ({
    number: index + 1,
    white: moves[index * 2],
    black: moves[index * 2 + 1],
    whiteIndex: index * 2,
    blackIndex: index * 2 + 1,
  }));

  useEffect(() => {
    const container = scrollRef.current;
    if (container && followLatestRef.current) container.scrollTo({ top: container.scrollHeight, behavior: 'smooth' });
  }, [moves.length]);

  return (
    <section className={styles.historyPanel} aria-labelledby="training-history-title">
      <div className={styles.sectionHeading}>
        <h3 id="training-history-title">Graded history</h3>
        <span>{grades.length} reviewed</span>
      </div>
      <div
        className={styles.historyScroll}
        ref={scrollRef}
        onScroll={event => {
          const target = event.currentTarget;
          followLatestRef.current = target.scrollHeight - target.scrollTop - target.clientHeight < 32;
        }}
      >
        {rows.length === 0 ? <p className={styles.emptyHistory}>Your moves and grades will appear here.</p> : (
          <table className={styles.moveTable}>
            <caption className="sr-only">Training move history with grades for reviewed moves</caption>
            <thead className="sr-only"><tr><th>Move</th><th>White</th><th>Black</th></tr></thead>
            <tbody>{rows.map(row => (
              <tr key={row.number}>
                <th scope="row">{row.number}.</th>
                <MoveCell move={row.white} grade={gradeMap.get(row.whiteIndex)} latest={row.whiteIndex === moves.length - 1} />
                <MoveCell move={row.black} grade={gradeMap.get(row.blackIndex)} latest={row.blackIndex === moves.length - 1} />
              </tr>
            ))}</tbody>
          </table>
        )}
      </div>
    </section>
  );
}

function MoveCell({ move, grade, latest }: { move?: Move; grade?: GradedMove; latest: boolean }) {
  return (
    <td>
      {move ? <span className={latest ? styles.latestMove : undefined} aria-current={latest ? 'true' : undefined}>
        <strong>{move.san}</strong>
        {grade ? <span className={styles.gradeText} data-grade={grade.grade.toLowerCase()}>{grade.grade}</span> : null}
        {grade?.hintLevelUsed ? <span className={styles.hintMark}>Hint {grade.hintLevelUsed}</span> : null}
      </span> : null}
    </td>
  );
}
