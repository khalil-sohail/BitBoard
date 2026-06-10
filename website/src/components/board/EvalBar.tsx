"use client";

import { useState, useEffect } from 'react';

export function EvalBar({ evalScore, mate, turn, orientation = 'w' }: { evalScore: number, mate?: number, turn: 'w' | 'b', orientation?: 'w' | 'b' }) {
  const [fillHeight, setFillHeight] = useState(50);
  
  useEffect(() => {
    if (mate !== undefined) {
       setFillHeight(mate > 0 ? 100 : 0);
       return;
    }
    
    // Convert centipawns to pawns and cap at +/- 5.0
    const pawns = Math.max(-5, Math.min(5, evalScore / 100));
    
    // Scale linear: 0 -> 50%, 5 -> 100%, -5 -> 0%
    const percentage = 50 + (pawns / 5) * 50;
    
    setFillHeight(percentage);
  }, [evalScore, mate]);

  const displayHeight = orientation === 'w' ? fillHeight : 100 - fillHeight;

  return (
    <div className={`w-5 h-full bg-[hsl(222,30%,14%)] rounded-lg overflow-hidden flex ${orientation === 'w' ? 'flex-col justify-end' : 'flex-col-reverse justify-end'} border border-white/10 relative select-none shadow-lg`}>
      {/* Dynamic Fill (White portion) */}
      <div 
        className={`w-full bg-gradient-to-b from-[#e8e8e8] to-[#d0d0d0] transition-all duration-500 ease-in-out relative flex justify-center`}
        style={{ height: `${displayHeight}%` }}
      >
        {/* Label for white winning */}
        {fillHeight >= 50 && (
           <span className={`text-[9px] font-mono font-bold absolute ${orientation === 'w' ? 'top-1' : 'bottom-1'} ${fillHeight > 82 ? 'text-[#2a2a2a]' : 'text-transparent'} leading-none`}>
             {mate !== undefined ? (mate > 0 ? `M${mate}` : `-M${Math.abs(mate)}`) : (evalScore/100).toFixed(1)}
           </span>
        )}
      </div>
      
      {/* Label for black winning */}
      {fillHeight < 50 && (
        <span className={`text-[9px] font-mono font-bold absolute ${orientation === 'w' ? 'bottom-1' : 'top-1'} w-full text-center ${fillHeight < 18 ? 'text-[#ddd]' : 'text-transparent'} leading-none`}>
          {mate !== undefined ? (mate > 0 ? `M${mate}` : `-M${Math.abs(mate)}`) : Math.abs(evalScore/100).toFixed(1)}
        </span>
      )}
    </div>
  );
}
