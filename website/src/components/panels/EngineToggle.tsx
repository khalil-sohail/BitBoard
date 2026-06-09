"use client";

import { PlayerColor } from '../../types/engine';
import { Badge } from '../ui/Badge';
import { useToast } from '../ui/Toast';

interface EngineToggleProps {
  currentVersion: string;
}

export function EngineToggle({ currentVersion }: EngineToggleProps) {
  const { addToast } = useToast();

  const handleNNUEClick = () => {
    addToast('NNUE evaluation is currently under development.', 'info');
  };

  const handleWASMClick = () => {
    addToast('Server-side analysis only — WASM build coming soon', 'info');
  };

  return (
    <div className="bg-surface rounded-lg border border-border p-4 mb-4">
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
      </div>
    </div>
  );
}
