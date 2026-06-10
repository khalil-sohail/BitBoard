import { EngineInfo } from '../../types/engine';

interface EnginePanelProps {
  info: EngineInfo | null;
  status: string;
  queuePosition: number | null;
}

export function EnginePanel({ info, status, queuePosition }: EnginePanelProps) {
  // Format score
  let formattedScore = "0.00";
  let isMate = false;
  
  if (info) {
    if (info.mate !== undefined) {
      isMate = true;
      formattedScore = info.mate > 0 ? `+M${info.mate}` : `-M${Math.abs(info.mate)}`;
    } else if (info.score !== undefined) {
      formattedScore = (info.score / 100).toFixed(2);
      if (info.score > 0) formattedScore = "+" + formattedScore;
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
        
        <div className="text-xs font-mono">
            {status === 'connecting' && <span className="text-muted">Connecting...</span>}
            {status === 'queued' && <span className="text-amber-500">Queued #{queuePosition}</span>}
            {status === 'ready' && <span className="text-emerald-500">Ready</span>}
            {status === 'error' && <span className="text-red-500">Error</span>}
            {status === 'session_expired' && <span className="text-amber-500">Expired</span>}
            {status === 'disconnected' && <span className="text-muted">Disconnected</span>}
        </div>
      </div>

      <div className="grid grid-cols-2 gap-4 mb-4">
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

      <div className="grid grid-cols-2 gap-4 text-sm mb-4">
          <div className="flex justify-between border-b border-border pb-1">
              <span className="text-muted">Nodes:</span>
              <span className="font-mono text-foreground">{formatNodes(info?.nodes)}</span>
          </div>
          <div className="flex justify-between border-b border-border pb-1">
              <span className="text-muted">NPS:</span>
              <span className="font-mono text-foreground">{formatNPS(info?.nodes, info?.time)}</span>
          </div>
      </div>

      <div className="rounded-md p-3 border border-white/10 h-[5.5rem] overflow-y-auto" style={{background: 'rgba(0,0,0,0.4)', boxShadow: 'inset 0 2px 6px rgba(0,0,0,0.5)'}}>
        <span className="text-[10px] text-muted/60 block mb-1.5 uppercase tracking-widest font-semibold">Principal Variation</span>
        <div className="text-xs font-mono text-emerald-300/80 break-words leading-relaxed">
          {info?.pv?.join(' ') || <span className="text-muted/40">Waiting for analysis...</span>}
        </div>
      </div>
    </div>
  );
}
