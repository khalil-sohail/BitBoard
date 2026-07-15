"use client";

import type { DifficultyLevel, PlayerColor, TimeControl } from '@/types/engine';
import { TIME_CONTROLS } from '@/types/engine';
import { DIFFICULTY_OPTIONS } from '@/lib/engine-difficulty';

const COLOR_OPTIONS: { value: PlayerColor | 'random'; label: string; icon: string }[] = [
  { value: 'w', label: 'White', icon: '♔' },
  { value: 'random', label: 'Random', icon: '⚄' },
  { value: 'b', label: 'Black', icon: '♚' },
];

export function PlayerColorControl({ value, onChange }: { value: PlayerColor | 'random'; onChange: (value: PlayerColor | 'random') => void }) {
  return (
    <fieldset>
      <legend className="mb-2 text-xs font-semibold uppercase tracking-wider text-muted">Play as</legend>
      <div className="grid grid-cols-3 gap-2">
        {COLOR_OPTIONS.map((option, index) => (
          <button
            key={option.value}
            type="button"
            data-setup-autofocus={index === 0 ? '' : undefined}
            aria-pressed={value === option.value}
            onClick={() => onChange(option.value)}
            className={`min-h-16 rounded-lg border text-sm font-semibold transition-colors ${value === option.value ? 'border-primary/50 bg-primary/15 text-primary' : 'border-white/10 bg-background text-muted hover:text-foreground'}`}
          >
            <span className="mr-2 text-lg" aria-hidden="true">{option.icon}</span>{option.label}
          </button>
        ))}
      </div>
    </fieldset>
  );
}

export function TimeControlControl({ value, onChange }: { value: TimeControl; onChange: (value: TimeControl) => void }) {
  return (
    <fieldset>
      <legend className="mb-2 text-xs font-semibold uppercase tracking-wider text-muted">Time control</legend>
      <div className="grid grid-cols-2 gap-2 sm:grid-cols-4">
        {TIME_CONTROLS.map(option => (
          <button
            key={option.label}
            type="button"
            aria-pressed={value.label === option.label}
            onClick={() => onChange(option)}
            className={`min-h-12 rounded-lg border px-2 text-xs font-bold transition-colors ${value.label === option.label ? 'border-primary/50 bg-primary/15 text-primary' : 'border-white/10 bg-background text-muted hover:text-foreground'}`}
          >
            {option.label}
          </button>
        ))}
      </div>
    </fieldset>
  );
}

export function DifficultyControl({ value, onChange }: { value: DifficultyLevel; onChange: (value: DifficultyLevel) => void }) {
  return (
    <fieldset>
      <legend className="mb-2 text-xs font-semibold uppercase tracking-wider text-muted">Opponent strength</legend>
      <div className="grid grid-cols-3 gap-2">
        {DIFFICULTY_OPTIONS.map(option => (
          <button
            key={option.id}
            type="button"
            aria-pressed={value === option.id}
            onClick={() => onChange(option.id)}
            className={`min-h-14 rounded-lg border px-2 transition-colors ${value === option.id ? 'border-primary/50 bg-primary/15 text-primary' : 'border-white/10 bg-background text-muted hover:text-foreground'}`}
          >
            <span className="block text-xs font-bold">{option.label}</span>
            <span className="mt-0.5 block text-[9px] opacity-60">{option.sublabel}</span>
          </button>
        ))}
      </div>
    </fieldset>
  );
}

export function RangeControl({ id, label, value, min, max, onChange }: { id: string; label: string; value: number; min: number; max: number; onChange: (value: number) => void }) {
  return (
    <div>
      <label htmlFor={id} className="mb-2 flex justify-between text-xs font-semibold uppercase tracking-wider text-muted">
        <span>{label}</span><span className="text-primary">{value}</span>
      </label>
      <input id={id} type="range" min={min} max={max} value={value} onChange={event => onChange(Number(event.target.value))} className="h-2 w-full cursor-pointer appearance-none rounded-lg bg-white/10 accent-primary" />
    </div>
  );
}

export function ToggleControl({ label, description, checked, onChange }: { label: string; description: string; checked: boolean; onChange: (value: boolean) => void }) {
  return (
    <label className="flex cursor-pointer items-start justify-between gap-4">
      <span>
        <span className="block text-sm font-medium text-foreground">{label}</span>
        <span className="mt-1 block text-xs leading-relaxed text-muted">{description}</span>
      </span>
      <input type="checkbox" checked={checked} onChange={event => onChange(event.target.checked)} className="mt-1 h-5 w-5 shrink-0 accent-primary" />
    </label>
  );
}
