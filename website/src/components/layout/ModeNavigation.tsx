"use client";

import type { GameMode } from '@/types/engine';
import styles from './ProductLayout.module.css';

interface ModeNavigationProps {
  mode: GameMode;
  onModeChange: (mode: GameMode) => void;
}

const MODES: { id: GameMode; label: string; icon: string; description: string }[] = [
  { id: 'fair', label: 'Fair Play', icon: '🛡️', description: 'Pure match — no hints' },
  { id: 'training', label: 'Training', icon: '🧠', description: 'Play with engine brain visible' },
  { id: 'analysis', label: 'Analysis', icon: '🔍', description: 'Free exploration & FEN loading' },
];

export function ModeNavigation({ mode, onModeChange }: ModeNavigationProps) {
  return (
    <nav className={`${styles.modeNavigation} bg-surface rounded-lg border border-white/10 p-1 shadow-md`} aria-label="Session mode">
      <div className="flex gap-0.5">
        {MODES.map((item) => {
          const active = mode === item.id;
          return (
            <button
              key={item.id}
              type="button"
              onClick={() => onModeChange(item.id)}
              title={item.description}
              aria-current={active ? 'page' : undefined}
              className={`relative min-h-11 flex-1 flex flex-col items-center justify-center gap-0.5 py-2 px-1 rounded-md text-[10px] font-semibold transition-all duration-200 select-none ${active ? 'bg-primary/15 text-primary shadow-inner ring-1 ring-primary/30' : 'text-muted hover:text-foreground hover:bg-white/5'}`}
            >
              {active && <span className="absolute top-0 left-1/2 -translate-x-1/2 w-6 h-[2px] rounded-full bg-primary" />}
              <span className="text-base leading-none" aria-hidden="true">{item.icon}</span>
              <span className="uppercase tracking-wider leading-none">{item.label}</span>
            </button>
          );
        })}
      </div>
    </nav>
  );
}
