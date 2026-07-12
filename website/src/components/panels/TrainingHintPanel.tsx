"use client";

import type { HintContext } from '../../lib/training-machine';
import type { ProgressiveHintView } from '../../lib/training-hint';

interface TrainingHintPanelProps {
  hint?: HintContext;
  hintView: ProgressiveHintView | null;
  canRequest: boolean;
  onRequest: () => void;
}

export function TrainingHintPanel({ hint, hintView, canRequest, onRequest }: TrainingHintPanelProps) {
  const isSearching = hint?.status === 'searching';
  const label = hint?.level === 1 ? 'Next hint' : hint?.level === 2 ? 'Reveal move' : 'Hint';
  const levelText = hint?.level ? `Level ${hint.level}/3` : 'No hint';
  const buttonText = isSearching ? 'Finding...' : label;

  return (
    <div className="bg-surface rounded-lg border border-white/10 p-4 shadow-md">
      <div className="flex items-center justify-between gap-3">
        <div>
          <h3 className="text-sm font-semibold text-foreground uppercase tracking-wider">Training Hint</h3>
          <p className="mt-1 text-xs text-muted-foreground">{levelText}</p>
        </div>
        <button
          type="button"
          onClick={onRequest}
          disabled={!canRequest || isSearching}
          aria-label="Request training hint"
          className="rounded-md bg-primary px-4 py-2 text-xs font-bold text-primary-foreground transition-colors hover:bg-primary/90 disabled:cursor-not-allowed disabled:bg-white/10 disabled:text-muted-foreground"
        >
          {buttonText}
        </button>
      </div>
      <div className="mt-3 min-h-8 text-sm text-foreground">
        {isSearching ? (
          <span className="text-muted-foreground">Analyzing this position...</span>
        ) : hintView ? (
          <span>{hintView.text}</span>
        ) : hint?.status === 'error' ? (
          <span className="text-orange-300">{hint.message ?? 'No legal hint is available.'}</span>
        ) : (
          <span className="text-muted-foreground">Request a hint for the current position.</span>
        )}
      </div>
    </div>
  );
}
