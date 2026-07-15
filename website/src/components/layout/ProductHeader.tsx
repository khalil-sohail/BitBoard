"use client";

import { memo, useState } from 'react';
import styles from './ProductLayout.module.css';

export const ProductHeader = memo(function ProductHeader() {
  const [isArchOpen, setIsArchOpen] = useState(false);

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
            <button type="button" onClick={() => setIsArchOpen(true)} className="min-h-11 text-sm font-medium text-muted hover:text-foreground transition-colors">
              Architecture
            </button>
          </nav>
        </div>
      </header>

      {isArchOpen && (
        <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-background/80 backdrop-blur-sm">
          <div className="bg-surface-elevated border border-border w-full max-w-2xl rounded-xl shadow-2xl overflow-hidden flex flex-col max-h-[90dvh]">
            <div className="flex justify-between items-center p-4 border-b border-border">
              <h2 className="text-lg font-bold font-mono text-foreground">Architecture</h2>
              <button type="button" aria-label="Close architecture" onClick={() => setIsArchOpen(false)} className="min-h-11 min-w-11 grid place-items-center text-muted hover:text-foreground transition-colors">
                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true"><line x1="18" y1="6" x2="6" y2="18" /><line x1="6" y1="6" x2="18" y2="18" /></svg>
              </button>
            </div>
            <div className="p-6 overflow-y-auto text-sm text-muted-foreground space-y-6">
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
        </div>
      )}
    </>
  );
});
