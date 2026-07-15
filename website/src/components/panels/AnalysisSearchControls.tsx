"use client";

interface AnalysisSearchControlsProps {
  depth: number;
  multiPv: number;
  disabled: boolean;
  onDepthChange: (value: number) => void;
  onMultiPvChange: (value: number) => void;
}

export function AnalysisSearchControls({ depth, multiPv, disabled, onDepthChange, onMultiPvChange }: AnalysisSearchControlsProps) {
  return (
    <section className="shrink-0 rounded-lg border border-white/10 bg-surface p-4 shadow-md" aria-label="Analysis search settings">
      <div className="flex items-center gap-4">
        <label htmlFor="live-analysis-depth" className="shrink-0 text-xs font-semibold text-muted">Depth <span className="text-primary">{depth}</span></label>
        <input id="live-analysis-depth" type="range" min="2" max="30" value={depth} disabled={disabled} onChange={event => onDepthChange(Number(event.target.value))} className="h-2 min-w-0 flex-1 cursor-pointer appearance-none rounded-lg bg-white/10 accent-primary disabled:cursor-not-allowed disabled:opacity-50" />
        <div className="flex shrink-0 rounded-md border border-white/10 bg-background p-0.5" aria-label="Principal variation count">
          {[1, 2, 3].map(value => (
            <button key={value} type="button" disabled={disabled} aria-pressed={multiPv === value} onClick={() => onMultiPvChange(value)} className={`min-h-9 min-w-9 rounded px-2 text-xs font-bold disabled:cursor-not-allowed disabled:opacity-50 ${multiPv === value ? 'bg-primary text-primary-foreground' : 'text-muted hover:text-foreground'}`}>{value}</button>
          ))}
        </div>
      </div>
    </section>
  );
}
