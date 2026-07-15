"use client";

import { useId, useState, type ReactNode } from 'react';

interface AdvancedSettingsProps {
  children: ReactNode;
  defaultExpanded?: boolean;
}

export function AdvancedSettings({ children, defaultExpanded = false }: AdvancedSettingsProps) {
  const [expanded, setExpanded] = useState(defaultExpanded);
  const contentId = useId();

  return (
    <div className="rounded-xl border border-white/10 bg-background/40">
      <button
        type="button"
        className="flex min-h-12 w-full items-center justify-between px-4 text-left text-sm font-semibold text-muted hover:text-foreground"
        aria-expanded={expanded}
        aria-controls={contentId}
        onClick={() => setExpanded(value => !value)}
      >
        <span>Advanced settings</span>
        <span aria-hidden="true" className="text-xs">{expanded ? '▲' : '▼'}</span>
      </button>
      <div id={contentId} hidden={!expanded} className="space-y-4 border-t border-white/10 p-4">
        {children}
      </div>
    </div>
  );
}
