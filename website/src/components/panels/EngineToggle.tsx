"use client";

import { useState } from 'react';
import { PlayerColor } from '../../types/engine';
import { Badge } from '../ui/Badge';
import { useToast } from '../ui/Toast';
import { useEngine } from '../../hooks/useEngine';

interface EngineToggleProps {
  currentVersion: string;
}

export function EngineToggle({ currentVersion }: EngineToggleProps) {
  const { addToast } = useToast();
  const { setEngineOption } = useEngine();
  
  const [useBook, setUseBook] = useState(true);
  const [bookDepth, setBookDepth] = useState(30);

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
    setBookDepth(val);
    setEngineOption('BookDepth', val);
  };

  return (
    <div className="bg-surface rounded-lg border border-white/10 p-4 shadow-md">
      <h3 className="text-sm font-semibold text-foreground mb-3 uppercase tracking-wider">Engine Configuration</h3>
      
      <div className="space-y-4">
        <div>
          <label className="text-xs text-muted mb-2 block">Evaluation Method</label>
          <div className="flex bg-background rounded-md p-1 border border-border">
            <button className="flex-1 text-sm py-1.5 px-3 rounded-sm bg-surface-elevated text-foreground shadow-sm font-medium transition-colors">
              Texel-Tuned HCE
            </button>
            <button 
              onClick={handleNNUEClick}
              className="flex-1 text-sm py-1.5 px-3 rounded-sm text-muted hover:text-foreground transition-colors flex items-center justify-center gap-2"
            >
              NNUE <Badge variant="accent">Soon</Badge>
            </button>
          </div>
        </div>

        <div>
          <label className="text-xs text-muted mb-2 block">Execution Environment</label>
          <div className="flex bg-background rounded-md p-1 border border-border">
            <button className="flex-1 text-sm py-1.5 px-3 rounded-sm bg-surface-elevated text-foreground shadow-sm font-medium transition-colors">
              Server C++
            </button>
            <button 
              onClick={handleWASMClick}
              className="flex-1 text-sm py-1.5 px-3 rounded-sm text-muted hover:text-foreground transition-colors flex items-center justify-center gap-2"
            >
              Browser WASM <Badge variant="accent">Soon</Badge>
            </button>
          </div>
        </div>

        <div className="border-t border-white/10 pt-4 mt-4">
          <h3 className="text-sm font-semibold text-foreground mb-3 uppercase tracking-wider">Book Settings</h3>
          
          <div className="space-y-4">
            <div className="flex items-center justify-between">
              <label className="text-sm text-muted">Use Opening Book</label>
              <button 
                onClick={handleToggleBook}
                className={`relative inline-flex h-5 w-9 items-center rounded-full transition-colors focus:outline-none ${useBook ? 'bg-primary' : 'bg-surface-elevated border border-border'}`}
              >
                <span 
                  className={`inline-block h-3 w-3 transform rounded-full bg-white transition-transform ${useBook ? 'translate-x-5' : 'translate-x-1'}`} 
                />
              </button>
            </div>

            <div>
              <div className="flex items-center justify-between mb-2">
                <label className="text-sm text-muted">Max Book Depth (ply)</label>
                <span className="text-sm font-medium text-foreground">{bookDepth}</span>
              </div>
              <input 
                type="range" 
                min="0" 
                max="100" 
                value={bookDepth} 
                onChange={handleDepthChange}
                disabled={!useBook}
                className={`w-full h-1.5 bg-surface-elevated rounded-lg appearance-none cursor-pointer ${!useBook ? 'opacity-50' : ''}`}
                style={{
                  background: useBook ? `linear-gradient(to right, var(--primary) ${bookDepth}%, var(--surface-elevated) ${bookDepth}%)` : ''
                }}
              />
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
