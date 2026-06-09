"use client";

import { useState, useEffect } from 'react';
import { LineChart, Line, XAxis, YAxis, Tooltip, ResponsiveContainer, ReferenceLine } from 'recharts';
import { EvalPoint } from '../../types/engine';

interface EvalGraphProps {
  data: EvalPoint[];
}

export function EvalGraph({ data }: EvalGraphProps) {
  const [mounted, setMounted] = useState(false);

  useEffect(() => {
    setMounted(true);
  }, []);

  // Process data to cap extreme values for better visualization
  const processedData = data.map(point => {
    let cappedEval = point.eval / 100; // Convert cp to pawns
    if (cappedEval > 10) cappedEval = 10;
    if (cappedEval < -10) cappedEval = -10;
    
    return {
      move: Math.ceil(point.moveNumber / 2),
      rawEval: point.eval,
      displayEval: cappedEval,
      turn: point.moveNumber % 2 !== 0 ? 'White' : 'Black'
    };
  });

  return (
    <div className="bg-surface rounded-lg border border-border p-4 w-full h-48 mt-4">
      <h3 className="text-sm font-semibold text-foreground uppercase tracking-wider mb-2">
        Evaluation History
      </h3>
      <div className="w-full h-32">
        {mounted && (
          <ResponsiveContainer width="100%" height="100%" minWidth={1} minHeight={1}>
            <LineChart data={processedData} margin={{ top: 5, right: 5, bottom: 5, left: -20 }}>
              <XAxis 
                dataKey="move" 
                tick={{ fontSize: 10, fill: 'var(--muted)' }} 
                axisLine={{ stroke: 'var(--border)' }}
                tickLine={false}
                minTickGap={20}
              />
              <YAxis 
                domain={[-10, 10]} 
                tick={{ fontSize: 10, fill: 'var(--muted)' }}
                axisLine={{ stroke: 'var(--border)' }}
                tickLine={false}
                ticks={[-10, -5, 0, 5, 10]}
              />
              <Tooltip 
                contentStyle={{ backgroundColor: 'var(--surface-elevated)', borderColor: 'var(--border)', borderRadius: '4px', fontSize: '12px' }}
                itemStyle={{ color: 'var(--foreground)' }}
                formatter={(value: any) => {
                  const numValue = Number(value);
                  return [`${numValue > 0 ? '+' : ''}${numValue.toFixed(1)}`, 'Eval'];
                }}
                labelFormatter={(label) => `Move ${label}`}
              />
              <ReferenceLine y={0} stroke="var(--border)" strokeDasharray="3 3" />
              <Line 
                type="monotone" 
                dataKey="displayEval" 
                stroke="var(--accent)" 
                strokeWidth={2}
                dot={false}
                activeDot={{ r: 4, fill: 'var(--accent)', stroke: 'var(--background)' }}
              />
            </LineChart>
          </ResponsiveContainer>
        )}
      </div>
    </div>
  );
}
