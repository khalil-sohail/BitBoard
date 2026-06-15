"use client";

import { useEffect, useRef } from 'react';
import { Move } from 'chess.js';
import { GradedMove } from '../../types/grades';
import { MoveBadge } from '../ui/MoveBadge';

interface MoveHistoryProps {
  moves: Move[];
  grades?: GradedMove[];
  /** Only show grade badges in Training/Analysis modes */
  showGrades?: boolean;
}

export function MoveHistory({ moves, grades = [], showGrades = false }: MoveHistoryProps) {
  const bottomRef = useRef<HTMLDivElement>(null);

  // Auto-scroll to the latest move
  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [moves.length]);

  // Build lookup map: moveIndex → GradedMove
  // grades may be a sparse array (indices assigned directly), so filter undefined gaps first
  const gradeMap = new Map<number, GradedMove>();
  for (const g of grades) {
    if (g != null) gradeMap.set(g.moveIndex, g);
  }

  // Group moves into pairs (White, Black)
  const rows = [];
  for (let i = 0; i < moves.length; i += 2) {
    rows.push({
      moveNumber: i / 2 + 1,
      white: moves[i],
      whiteIndex: i,
      black: moves[i + 1] || null,
      blackIndex: i + 1,
    });
  }

  // Grade summary counts — only computed when badges are visible
  const blunders    = showGrades ? grades.filter(g => g?.grade === 'Blunder').length    : 0;
  const mistakes    = showGrades ? grades.filter(g => g?.grade === 'Mistake').length    : 0;
  const inaccuracies = showGrades ? grades.filter(g => g?.grade === 'Inaccuracy').length : 0;
  const hasSummary  = showGrades && (blunders + mistakes + inaccuracies) > 0;

  return (
    <div className="bg-surface rounded-lg border border-white/10 p-4 flex flex-col shrink-0 min-h-[160px] max-h-[280px] shadow-md">

      <div className="flex items-baseline justify-between mb-3 shrink-0">
        <h3 className="text-sm font-semibold text-foreground uppercase tracking-wider">
          Move History
        </h3>
        {/* Inline grade summary — visible only in Training/Analysis */}
        {hasSummary && (
          <span className="flex items-center gap-2 text-[10px] font-mono">
            {blunders > 0 && (
              <span className="text-red-400" title="Blunders">{blunders}??</span>
            )}
            {mistakes > 0 && (
              <span className="text-orange-400" title="Mistakes">{mistakes}?</span>
            )}
            {inaccuracies > 0 && (
              <span className="text-yellow-400" title="Inaccuracies">{inaccuracies}?!</span>
            )}
          </span>
        )}
      </div>

      <div className="flex-1 overflow-y-auto pr-1">
        {rows.length === 0 ? (
          <div className="h-full flex items-center justify-center text-muted text-sm italic">
            No moves yet
          </div>
        ) : (
          <table className="w-full text-sm font-mono text-left border-collapse">
            <tbody>
              {rows.map((row) => (
                <tr
                  key={row.moveNumber}
                  className="border-b border-border/40 hover:bg-surface-elevated/50 transition-colors"
                >
                  {/* Move number */}
                  <td className="w-8 py-1.5 text-muted/60 text-right pr-3 shrink-0 select-none align-middle">
                    {row.moveNumber}.
                  </td>

                  {/* White move + badge */}
                  <td className="py-1.5 pr-2 align-middle" style={{ width: '45%' }}>
                    <span className="flex items-center gap-1.5">
                      <span className="font-semibold text-foreground truncate">{row.white.san}</span>
                      {showGrades && gradeMap.has(row.whiteIndex) && (
                        <MoveBadge grade={gradeMap.get(row.whiteIndex)!.grade} compact />
                      )}
                    </span>
                  </td>

                  {/* Black move + badge */}
                  <td className="py-1.5 align-middle" style={{ width: '45%' }}>
                    {row.black ? (
                      <span className="flex items-center gap-1.5">
                        <span className="font-semibold text-foreground truncate">{row.black.san}</span>
                        {showGrades && gradeMap.has(row.blackIndex) && (
                          <MoveBadge grade={gradeMap.get(row.blackIndex)!.grade} compact />
                        )}
                      </span>
                    ) : null}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
        {/* Anchor for auto-scroll */}
        <div ref={bottomRef} />
      </div>
    </div>
  );
}
