import styles from './AnalysisSidebar.module.css';

export function AnalysisSearchControls({ depth, multiPv, disabled, onDepthChange, onMultiPvChange }: { depth: number; multiPv: number; disabled: boolean; onDepthChange: (value: number) => void; onMultiPvChange: (value: number) => void }) {
  return <section className={styles.searchControls} aria-label="Live analysis controls">
    <label htmlFor="live-analysis-depth"><span>Depth</span><strong>{depth}</strong></label>
    <input id="live-analysis-depth" type="range" min="2" max="30" value={depth} disabled={disabled} onChange={event => onDepthChange(Number(event.target.value))} />
    <fieldset disabled={disabled}><legend>Lines</legend><div>{[1, 2, 3].map(value => <button key={value} type="button" aria-pressed={multiPv === value} onClick={() => onMultiPvChange(value)}>{value}</button>)}</div></fieldset>
  </section>;
}
