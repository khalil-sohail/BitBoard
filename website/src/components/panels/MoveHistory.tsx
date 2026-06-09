import { Move } from 'chess.js';

interface MoveHistoryProps {
  moves: Move[];
}

export function MoveHistory({ moves }: MoveHistoryProps) {
  // Group moves into pairs (White, Black)
  const rows = [];
  for (let i = 0; i < moves.length; i += 2) {
    rows.push({
      moveNumber: i / 2 + 1,
      white: moves[i],
      black: moves[i + 1] || null
    });
  }

  // Auto-scroll to bottom behavior would be handled by a ref on a wrapper div,
  // but for simplicity we'll just let it flow.

  return (
    <div className="bg-surface rounded-lg border border-border p-4 mb-4 flex flex-col h-64">
      <h3 className="text-sm font-semibold text-foreground uppercase tracking-wider mb-3 shrink-0">
        Move History
      </h3>
      
      <div className="flex-1 overflow-y-auto pr-2 custom-scrollbar">
        {rows.length === 0 ? (
          <div className="h-full flex items-center justify-center text-muted text-sm italic">
            No moves yet
          </div>
        ) : (
          <table className="w-full text-sm font-mono text-left">
            <tbody className="block">
              {rows.map((row, i) => (
                <tr key={i} className="flex border-b border-border/50 hover:bg-surface-elevated/50 transition-colors">
                  <td className="w-12 py-1.5 text-muted/70 text-right pr-4 shrink-0 select-none">
                    {row.moveNumber}.
                  </td>
                  <td className="flex-1 py-1.5 font-semibold text-foreground">
                    {row.white.san}
                  </td>
                  <td className="flex-1 py-1.5 font-semibold text-foreground">
                    {row.black ? row.black.san : ''}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}
