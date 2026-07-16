import { AreaChart, Area, XAxis, YAxis, Tooltip, ResponsiveContainer } from 'recharts';

interface EvalGraphProps {
  data: { ply: number; score: number }[];
}

export function EvalGraph({ data }: EvalGraphProps) {
  if (!data || data.length === 0) {
    return (
      <div className="shrink-0 min-h-[120px] w-full bg-surface rounded-lg border border-white/10 overflow-hidden shadow-md flex items-center justify-center">
        <span className="text-xs text-muted uppercase tracking-widest font-mono">No evaluation data</span>
      </div>
    );
  }

  const max = Math.max(...data.map(d => d.score), 0);
  const min = Math.min(...data.map(d => d.score), 0);
  
  let offset = 0.5;
  if (max > min) {
    offset = max / (max - min);
  }

  return (
    <div role="img" aria-label={`Evaluation history chart with ${data.length} positions. Latest evaluation ${data.at(-1)?.score.toFixed(2) ?? '0.00'} pawns from White's perspective.`} className="shrink-0 h-[120px] min-h-[120px] w-full min-w-[100px] bg-surface rounded-lg border border-white/10 overflow-hidden shadow-md relative group mt-4">
      <div className="absolute top-2 left-3 z-10 pointer-events-none">
          <span className="text-[10px] text-foreground uppercase tracking-widest font-semibold">Eval History</span>
      </div>
      
      <ResponsiveContainer width="100%" height={120} minWidth={100}>
        <AreaChart data={data} margin={{ top: 0, right: 0, left: 0, bottom: 0 }}>
          <defs>
            <linearGradient id="splitColor" x1="0" y1="0" x2="0" y2="1">
              <stop offset={offset} stopColor="#e5e7eb" stopOpacity={0.9} />
              <stop offset={offset} stopColor="#374151" stopOpacity={0.8} />
            </linearGradient>
          </defs>
          <XAxis dataKey="ply" hide />
          <YAxis domain={[-10, 10]} hide />
          <Tooltip 
            contentStyle={{ backgroundColor: 'rgba(20,20,20,0.9)', border: '1px solid rgba(255,255,255,0.1)', borderRadius: '6px', fontSize: '11px', padding: '4px 8px' }}
            itemStyle={{ color: '#e5e7eb', fontWeight: 600 }}
            labelStyle={{ display: 'none' }}
            formatter={(val: number | string | readonly (number | string)[] | undefined) => {
              const numVal = Number(val);
              if (isNaN(numVal)) return ['0.00', 'Eval'];
              const sign = numVal > 0 ? '+' : '';
              return [sign + numVal.toFixed(2), 'Eval'];
            }}
            isAnimationActive={false}
            cursor={{ stroke: 'rgba(255,255,255,0.2)', strokeWidth: 1 }}
          />
          <Area 
            type="stepAfter" 
            dataKey="score" 
            stroke="none" 
            fill="url(#splitColor)" 
            baseValue={0}
            isAnimationActive={false}
          />
        </AreaChart>
      </ResponsiveContainer>
    </div>
  );
}
