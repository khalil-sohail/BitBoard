"use client";

import { memo, useCallback, useEffect, useId, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import styles from './ProductLayout.module.css';

const FOCUSABLE = 'button:not([disabled]), [href], input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';

export const ProductHeader = memo(function ProductHeader() {
  const [isArchOpen, setIsArchOpen] = useState(false);
  const architectureTriggerRef = useRef<HTMLButtonElement>(null);
  const closeArchitecture = useCallback(() => setIsArchOpen(false), []);

  return (
    <>
      <header className={`${styles.header} border-b border-border bg-surface px-4 sm:px-6 flex items-center z-10`}>
        <div className={`${styles.headerInner} flex flex-wrap justify-between items-center gap-3 py-2`}>
          <div className="flex flex-col">
            <h1 className="text-xl font-mono font-bold tracking-tight text-foreground uppercase leading-none mb-1">
              BIT<span className="text-primary">BOARD</span>
            </h1>
            <span className="text-[10px] text-muted-foreground font-mono uppercase tracking-wider hidden sm:inline-block">
              C++ chess engine · WebSocket analysis · UCI bridge
            </span>
          </div>
          <nav className="flex items-center gap-4" aria-label="Product links">
            <a href="https://github.com/khalil-sohail/chess-engine" target="_blank" rel="noreferrer" className="min-h-11 inline-flex items-center text-sm font-medium text-muted hover:text-foreground transition-colors">
              GitHub
            </a>
            <button ref={architectureTriggerRef} type="button" onClick={() => setIsArchOpen(true)} className="min-h-11 text-sm font-medium text-muted hover:text-foreground transition-colors">
              Architecture
            </button>
          </nav>
        </div>
      </header>

      {isArchOpen ? <ArchitectureDialog onClose={closeArchitecture} returnFocusRef={architectureTriggerRef} /> : null}
    </>
  );
});

function ArchitectureDialog({ onClose, returnFocusRef }: { onClose: () => void; returnFocusRef: React.RefObject<HTMLButtonElement | null> }) {
  const titleId = useId();
  const dialogRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const appShell = document.querySelector<HTMLElement>('[data-product-app-shell]');
    const returnFocus = returnFocusRef.current;
    const previousOverflow = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    if (appShell) appShell.inert = true;
    const frame = requestAnimationFrame(() => dialogRef.current?.querySelector<HTMLButtonElement>('[data-architecture-close]')?.focus());
    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        event.preventDefault();
        onClose();
        return;
      }
      if (event.key !== 'Tab' || !dialogRef.current) return;
      const focusable = [...dialogRef.current.querySelectorAll<HTMLElement>(FOCUSABLE)].filter(element => element.getClientRects().length > 0);
      if (!focusable.length) return;
      const first = focusable[0];
      const last = focusable[focusable.length - 1];
      if (event.shiftKey && document.activeElement === first) {
        event.preventDefault();
        last.focus();
      } else if (!event.shiftKey && document.activeElement === last) {
        event.preventDefault();
        first.focus();
      }
    };
    document.addEventListener('keydown', handleKeyDown);
    return () => {
      cancelAnimationFrame(frame);
      document.removeEventListener('keydown', handleKeyDown);
      document.body.style.overflow = previousOverflow;
      if (appShell) appShell.inert = false;
      returnFocus?.focus();
    };
  }, [onClose, returnFocusRef]);

  return createPortal(
    <div className="fixed inset-0 z-[70] flex items-center justify-center p-4 bg-background/80 backdrop-blur-sm" onMouseDown={event => event.target === event.currentTarget && onClose()}>
      <div ref={dialogRef} role="dialog" aria-modal="true" aria-labelledby={titleId} className="bg-surface-elevated border border-border w-full max-w-2xl rounded-xl shadow-2xl overflow-hidden flex flex-col max-h-[90dvh]">
        <div className="flex justify-between items-center p-4 border-b border-border">
          <h2 id={titleId} className="text-lg font-bold font-mono text-foreground">Architecture</h2>
          <button data-architecture-close type="button" aria-label="Close architecture" onClick={onClose} className="min-h-11 min-w-11 grid place-items-center text-muted hover:text-foreground transition-colors">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true"><line x1="18" y1="6" x2="6" y2="18" /><line x1="6" y1="6" x2="18" y2="18" /></svg>
          </button>
        </div>
        <div className="p-6 overflow-y-auto text-sm text-muted space-y-6">
          <div className="space-y-4">
            <p className="text-foreground font-medium">Data Pipeline</p>
            <div className="font-mono text-xs p-4 bg-background border border-white/5 rounded-lg space-y-2">
              <div className="text-primary">React / Next.js UI</div>
              <div className="text-muted pl-4">↓ WebSocket JSON</div>
              <div className="text-foreground">Node.js WebSocket Server / Engine Session Manager</div>
              <div className="text-muted pl-4">↓ child_process stdin/stdout</div>
              <div className="text-accent">C++23 UCI Chess Engine</div>
              <div className="text-muted pl-4">↓ Search / Eval / Best Move / PV</div>
              <div className="text-primary">Streamed real-time analysis back to UI</div>
            </div>
          </div>
          <div className="space-y-3">
            <h3 className="text-foreground font-medium">Core Components</h3>
            <ul className="space-y-2 list-disc pl-4 marker:text-primary">
              <li><strong className="text-foreground">C++23 Engine:</strong> Custom bitboard chess engine featuring a deterministic search/evaluation pipeline.</li>
              <li><strong className="text-foreground">UCI Protocol:</strong> Standardized communication interface between the engine and the server.</li>
              <li><strong className="text-foreground">WebSocket Bridge:</strong> Low-latency bidirectional connection enabling real-time analysis streaming.</li>
              <li><strong className="text-foreground">Node.js Session Manager:</strong> Orchestrates engine processes, manages lifecycle, and handles concurrent analysis sessions safely.</li>
            </ul>
          </div>
        </div>
      </div>
    </div>,
    document.body,
  );
}
