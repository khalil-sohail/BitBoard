import styles from './AnalysisSidebar.module.css';

export function AnalysisNavigationPanel({ cursorPly, historyLength, onNavigate }: { cursorPly: number; historyLength: number; onNavigate: (ply: number) => void }) {
  return <section className={styles.navigationPanel} aria-label="Position navigation">
    <div className={styles.sectionHeading}><h3>Position navigation</h3><span>Ply {cursorPly} of {historyLength}</span></div>
    <div className={styles.navigationButtons}>
      <button type="button" aria-label="Go to starting position" disabled={cursorPly === 0} onClick={() => onNavigate(0)}>⏮</button>
      <button type="button" aria-label="Go to previous move" disabled={cursorPly === 0} onClick={() => onNavigate(cursorPly - 1)}>←</button>
      <button type="button" aria-label="Go to next move" disabled={cursorPly === historyLength} onClick={() => onNavigate(cursorPly + 1)}>→</button>
      <button type="button" aria-label="Go to latest position" disabled={cursorPly === historyLength} onClick={() => onNavigate(historyLength)}>⏭</button>
    </div>
    {cursorPly < historyLength ? <p className={styles.cursorNotice}>Viewing history · return to the latest position to make a board move.</p> : null}
  </section>;
}
