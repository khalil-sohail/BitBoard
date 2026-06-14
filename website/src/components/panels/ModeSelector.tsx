"use client";

import { GameMode } from '../../types/engine';

interface ModeSelectorProps {
  mode: GameMode;
  onModeChange: (mode: GameMode) => void;
}

const MODES: { id: GameMode; label: string; icon: string; description: string }[] = [
  {
    id: 'fair',
    label: 'Fair Play',
    icon: '🛡️',
    description: 'Pure match — no hints',
  },
  {
    id: 'training',
    label: 'Training',
    icon: '🧠',
    description: 'Play with engine brain visible',
  },
  {
    id: 'analysis',
    label: 'Analysis',
    icon: '🔍',
    description: 'Free exploration & FEN loading',
  },
];

export function ModeSelector({ mode, onModeChange }: ModeSelectorProps) {
  return (
    <div className="bg-surface rounded-lg border border-white/10 p-1 shadow-md">
      <div className="flex gap-0.5">
        {MODES.map((m) => {
          const isActive = mode === m.id;
          return (
            <button
              key={m.id}
              onClick={() => onModeChange(m.id)}
              title={m.description}
              className={`
                relative flex-1 flex flex-col items-center gap-0.5 py-2 px-1 rounded-md text-[10px] font-semibold
                transition-all duration-200 select-none
                ${isActive
                  ? 'bg-primary/15 text-primary shadow-inner ring-1 ring-primary/30'
                  : 'text-muted hover:text-foreground hover:bg-white/5'
                }
              `}
            >
              {/* Active indicator bar */}
              {isActive && (
                <span className="absolute top-0 left-1/2 -translate-x-1/2 w-6 h-[2px] rounded-full bg-primary" />
              )}
              <span className="text-base leading-none">{m.icon}</span>
              <span className="uppercase tracking-wider leading-none">{m.label}</span>
            </button>
          );
        })}
      </div>
    </div>
  );
}
