import { EngineInfo } from '../../types/engine';

interface EnginePanelProps {
  info: EngineInfo | null;
  status: string;
  queuePosition: number | null;
}

export function EnginePanel({ info, status, queuePosition }: EnginePanelProps) {
  // Format score based on the best PV
  const bestPv = info?.pvs?.[0];
  let formattedScore = "0.00";
  let isMate = false;
  
  if (bestPv) {
    if (bestPv.mate !== undefined) {
      isMate = true;
      formattedScore = bestPv.mate > 0 ? `+M${bestPv.mate}` : `-M${Math.abs(bestPv.mate)}`;
    } else if (bestPv.score !== undefined) {
      formattedScore = (bestPv.score / 100).toFixed(2);
      if (bestPv.score > 0) formattedScore = "+" + formattedScore;
    }
  }

  // Format nodes
  const formatNodes = (n: number | undefined) => {
    if (n === undefined) return "-";
    if (n >= 1000000) return (n / 1000000).toFixed(2) + "M";
    if (n >= 1000) return (n / 1000).toFixed(1) + "k";
    return n.toString();
  };

  // Format NPS
  const formatNPS = (nodes: number | undefined, timeMs: number | undefined) => {
      if (nodes === undefined || timeMs === undefined || timeMs === 0) return "-";
      const nps = (nodes / timeMs) * 1000;
      if (nps >= 1000000) return (nps / 1000000).toFixed(2) + "M/s";
      if (nps >= 1000) return (nps / 1000).toFixed(1) + "k/s";
      return nps.toFixed(0) + "/s";
  }

  return (
    <div className="bg-surface rounded-lg border border-white/10 p-4 shadow-md">
      <div className="flex justify-between items-center mb-4">
        <h3 className="text-sm font-semibold text-foreground uppercase tracking-wider flex items-center gap-2">
          Engine Analysis
          {status === 'thinking' && (
            <span className="relative flex h-2 w-2">
              <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-accent opacity-75"></span>
              <span className="relative inline-flex rounded-full h-2 w-2 bg-accent"></span>
            </span>
          )}
        </h3>
        
        <div className="text-xs font-mono flex items-center gap-2">
            {status === 'connecting' && <span className="text-muted">Connecting...</span>}
            {status === 'queued' && <span className="text-amber-500">Queued #{queuePosition}</span>}
            {status === 'idle' && <span className="text-emerald-500">Idle</span>}
            {status === 'thinking' && <span className="text-accent">Thinking</span>}
            {status === 'error' && <span className="text-red-500">Error</span>}
            {status === 'session_expired' && (info ? <span className="text-amber-500">Expired (Last analysis)</span> : <span className="text-amber-500">Expired</span>)}
            {status === 'disconnected' && (info ? <span className="text-muted">Last analysis</span> : <span className="text-muted">Disconnected</span>)}
        </div>
      </div>

      <div className="grid grid-cols-[1fr_1.5fr] gap-5">
        
        {/* ── LEFT COLUMN: STATS ─────────────────────────────────────────── */}
        <div className="flex flex-col justify-between gap-4">
          <div className="grid grid-cols-2 gap-3">
            <div className="bg-background rounded p-3 border border-border flex flex-col justify-center items-center">
              <span className="text-xs text-muted mb-1 uppercase">Eval</span>
              <span className={`text-xl font-bold font-mono ${isMate ? 'text-accent' : 'text-foreground'}`}>
                {info ? formattedScore : '-.--'}
              </span>
            </div>
            <div className="bg-background rounded p-3 border border-border flex flex-col justify-center items-center">
              <span className="text-xs text-muted mb-1 uppercase">Depth</span>
              <span className="text-xl font-bold font-mono text-foreground">
                {info?.depth ?? '-'}
              </span>
            </div>
          </div>

          <div className="flex flex-col gap-1 text-xs sm:text-sm">
            <div className="flex justify-between border-b border-border pb-1">
                <span className="text-muted">Nodes:</span>
                <span className="font-mono text-foreground">{formatNodes(info?.nodes)}</span>
            </div>
            <div className="flex justify-between border-b border-border pb-1">
                <span className="text-muted">NPS:</span>
                <span className="font-mono text-foreground">{formatNPS(info?.nodes, info?.time)}</span>
            </div>
          </div>
        </div>

        {/* ── RIGHT COLUMN: PVs ─────────────────────────────────────────── */}
        <div className="rounded-md p-3 border border-white/10 h-full min-h-[130px] overflow-y-auto" style={{background: 'rgba(0,0,0,0.4)', boxShadow: 'inset 0 2px 6px rgba(0,0,0,0.5)'}}>
          <span className="text-[10px] text-muted/60 block mb-1.5 uppercase tracking-widest font-semibold">Principal Variations</span>
          {info?.pvs?.length ? (
            <div className="space-y-1.5">
              {info.pvs.map((pv, i) => {
                const labelColor = i === 0 ? 'text-green-400' : i === 1 ? 'text-blue-400' : 'text-yellow-400';
                const textColor = i === 0 ? 'text-emerald-300/80' : 'text-muted/80';
                const scoreStr = pv.mate !== undefined 
                  ? (pv.mate > 0 ? `+M${pv.mate}` : `-M${Math.abs(pv.mate)}`) 
                  : ((pv.score > 0 ? '+' : '') + (pv.score / 100).toFixed(2));
                  
                return (
                  <div key={pv.multipv} className="text-[11px] font-mono break-words leading-relaxed">
                    <span className={`font-bold mr-2 ${labelColor}`}>
                      {scoreStr}
                    </span>
                    <span className={textColor}>
                      {Array.isArray(pv.pv) ? pv.pv.join(' ') : (pv.pv || '')}
                    </span>
                  </div>
                );
              })}
            </div>
          ) : (
            <div className="text-xs font-mono text-muted/40">Waiting for analysis...</div>
          )}
        </div>

      </div>
    </div>
  );
}
