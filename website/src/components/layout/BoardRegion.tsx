import type { ReactNode } from 'react';
import styles from './ProductLayout.module.css';

interface BoardRegionProps {
  board: ReactNode;
  evaluationBar?: ReactNode;
}

export function BoardRegion({ board, evaluationBar }: BoardRegionProps) {
  return (
    <section className={styles.boardRegion} aria-label="Chessboard">
      <div className={`${styles.boardCluster} ${evaluationBar ? styles.boardClusterWithEvaluation : ''}`}>
        {evaluationBar && <div className={styles.evaluation}>{evaluationBar}</div>}
        <div className={styles.boardSurface}>{board}</div>
      </div>
    </section>
  );
}
