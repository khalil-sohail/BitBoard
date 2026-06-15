"use client";

import { useState } from 'react';
import { Badge } from '../ui/Badge';
import { useToast } from '../ui/Toast';
import { useEngine } from '../../hooks/useEngine';

interface EngineToggleProps {
  currentVersion?: string;
  maxDepth: number;
  onDepthChange: (depth: number) => void;
}

export function EngineToggle({ currentVersion = "Texel-Tuned HCE", maxDepth, onDepthChange }: EngineToggleProps) {
  const { addToast } = useToast();
  const { setEngineOption } = useEngine();
  
  const [useBook, setUseBook] = useState(true);

  const handleNNUEClick = () => {
    addToast('NNUE evaluation is currently under development.', 'info');
  };

  const handleWASMClick = () => {
    addToast('Server-side analysis only — WASM build coming soon', 'info');
  };

  const handleToggleBook = () => {
    const newVal = !useBook;
    setUseBook(newVal);
    setEngineOption('OwnBook', newVal);
  };

  const handleDepthChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const val = parseInt(e.target.value, 10);
    onDepthChange(val);
    setEngineOption('BookDepth', val);
  };

  return (
    <div className="grid grid-cols-[5fr_3fr] gap-6 bg-surface-elevated border border-white/10 rounded-xl p-5 shadow-md relative">
      
      {/* Vertical Divider using border on the first column */}
      {/* ── ENGINE CONFIGURATION ─────────────────────────────────────────── */}
      <div className="flex flex-col gap-5 border-r border-white/5 pr-6">
        <h3 className="text-xs font-bold text-foreground uppercase tracking-wider">
          Engine Configuration
        </h3>
        
        {/* Row 1: Evaluation Method */}
        <div className="flex flex-col gap-2">
          <label className="text-sm font-medium text-muted-foreground whitespace-nowrap">
            Evaluation Method
          </label>
          <div className="flex items-center bg-background border border-white/5 rounded-lg p-1 w-full">
            <button className="flex-1 px-2 py-1.5 bg-surface-elevated text-foreground text-xs rounded-md shadow-sm font-semibold whitespace-nowrap">
              {currentVersion}
            </button>
            <button 
              onClick={handleNNUEClick}
              className="flex-1 px-2 py-1.5 text-muted-foreground/50 hover:text-muted-foreground transition-colors text-xs flex items-center justify-center gap-1.5 font-semibold whitespace-nowrap"
            >
              NNUE <Badge variant="accent" className="text-[9px] px-1 py-0 h-4">Soon</Badge>
            </button>
          </div>
        </div>

        {/* Row 2: Execution Environment */}
        <div className="flex flex-col gap-2">
          <label className="text-sm font-medium text-muted-foreground whitespace-nowrap">
            Execution Environment
          </label>
          <div className="flex items-center bg-background border border-white/5 rounded-lg p-1 w-full">
            <button className="flex-1 px-2 py-1.5 bg-surface-elevated text-foreground text-xs rounded-md shadow-sm font-semibold whitespace-nowrap">
              Server C++
            </button>
            <button 
              onClick={handleWASMClick}
              className="flex-1 px-2 py-1.5 text-muted-foreground/50 hover:text-muted-foreground transition-colors text-xs flex items-center justify-center gap-1.5 font-semibold whitespace-nowrap"
            >
              Browser WASM <Badge variant="accent" className="text-[9px] px-1 py-0 h-4">Soon</Badge>
            </button>
          </div>
        </div>
      </div>

      {/* ── BOOK SETTINGS ────────────────────────────────────────────────── */}
      <div className="flex flex-col gap-5">
        <h3 className="text-xs font-bold text-foreground uppercase tracking-wider">
          Book Settings
        </h3>
        
        {/* Row 1: Use Opening Book */}
        <div className="flex flex-col gap-3">
          <label className="text-sm font-medium text-muted-foreground whitespace-nowrap">
            Use Opening Book
          </label>
          <div>
            <button
              onClick={handleToggleBook}
              className={`relative inline-flex h-6 w-11 flex-shrink-0 cursor-pointer items-center rounded-full transition-colors duration-200 ease-in-out focus:outline-none ${useBook ? 'bg-primary' : 'bg-white/10'}`}
            >
              <span className={`inline-block h-4 w-4 transform rounded-full bg-white transition duration-200 ease-in-out ${useBook ? 'translate-x-6' : 'translate-x-1'}`} />
            </button>
          </div>
        </div>

        {/* Row 2: Max Depth */}
        <div className={`flex flex-col gap-2 transition-opacity duration-200 ${useBook ? 'opacity-100' : 'opacity-40 pointer-events-none'}`}>
          <label className="text-sm font-medium text-muted-foreground whitespace-nowrap">
            Max Depth
          </label>
          <div className="flex items-center gap-4 w-full justify-between">
            <input 
              type="range" 
              min="2" 
              max="60" 
              step="1"
              value={maxDepth}
              onChange={handleDepthChange}
              className="w-full h-1.5 bg-white/10 rounded-lg appearance-none cursor-pointer accent-primary"
            />
            <div className="flex flex-col items-end min-w-[36px]">
              <span className="text-sm font-bold text-accent leading-none">{maxDepth}</span>
              <span className="text-[10px] font-semibold text-accent/60 uppercase tracking-widest mt-1">ply</span>
            </div>
          </div>
        </div>
      </div>

    </div>
  );
}