"use client";

import { useEffect, useId, useRef, type FormEvent, type ReactNode } from 'react';
import styles from './SessionSetup.module.css';

interface SessionSetupDialogProps {
  modeLabel: string;
  description: string;
  submitLabel: string;
  canSubmit?: boolean;
  onCancel: () => void;
  onSubmit: () => void;
  children: ReactNode;
}

const FOCUSABLE = 'button:not([disabled]), input:not([disabled]), textarea:not([disabled]), select:not([disabled]), [tabindex]:not([tabindex="-1"])';

export function SessionSetupDialog({
  modeLabel,
  description,
  submitLabel,
  canSubmit = true,
  onCancel,
  onSubmit,
  children,
}: SessionSetupDialogProps) {
  const titleId = useId();
  const descriptionId = useId();
  const dialogRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const previouslyFocused = document.activeElement instanceof HTMLElement ? document.activeElement : null;
    const appShell = document.querySelector<HTMLElement>('[data-product-app-shell]');
    const previousOverflow = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    if (appShell) appShell.inert = true;

    const frame = requestAnimationFrame(() => {
      const preferred = dialogRef.current?.querySelector<HTMLElement>('[data-setup-autofocus]');
      (preferred ?? dialogRef.current?.querySelector<HTMLElement>(FOCUSABLE))?.focus();
    });

    return () => {
      cancelAnimationFrame(frame);
      document.body.style.overflow = previousOverflow;
      if (appShell) appShell.inert = false;
      previouslyFocused?.focus();
    };
  }, []);

  const handleKeyDown = (event: React.KeyboardEvent<HTMLDivElement>) => {
    if (event.key === 'Escape') {
      event.preventDefault();
      onCancel();
      return;
    }
    if (event.key !== 'Tab') return;
    const focusable = Array.from(dialogRef.current?.querySelectorAll<HTMLElement>(FOCUSABLE) ?? []);
    if (focusable.length === 0) return;
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

  const handleSubmit = (event: FormEvent) => {
    event.preventDefault();
    if (canSubmit) onSubmit();
  };

  return (
    <div className={styles.backdrop} onMouseDown={event => event.target === event.currentTarget && onCancel()}>
      <div
        ref={dialogRef}
        className={styles.dialog}
        role="dialog"
        aria-modal="true"
        aria-labelledby={titleId}
        aria-describedby={descriptionId}
        onKeyDown={handleKeyDown}
      >
        <form className="contents" onSubmit={handleSubmit} noValidate>
          <header className={styles.header}>
            <div className="flex items-start justify-between gap-4">
              <div>
                <p className="mb-1 text-[10px] font-bold uppercase tracking-[0.2em] text-primary">{modeLabel}</p>
                <h2 id={titleId} className="text-xl font-bold text-foreground">Set up session</h2>
                <p id={descriptionId} className="mt-1 max-w-2xl text-sm leading-relaxed text-muted">{description}</p>
              </div>
              <button type="button" aria-label="Close setup" onClick={onCancel} className="grid min-h-11 min-w-11 place-items-center rounded-lg text-muted hover:bg-white/5 hover:text-foreground">✕</button>
            </div>
          </header>
          <div className={styles.body}>{children}</div>
          <footer className={styles.footer}>
            <div className="flex flex-col-reverse gap-2 sm:flex-row sm:justify-end">
              <button type="button" onClick={onCancel} className="min-h-11 rounded-lg border border-white/10 px-5 text-sm font-semibold text-muted hover:bg-white/5 hover:text-foreground">Cancel</button>
              <button type="submit" disabled={!canSubmit} className="min-h-11 rounded-lg bg-primary px-6 text-sm font-bold text-primary-foreground shadow-lg shadow-primary/20 disabled:cursor-not-allowed disabled:opacity-50">{submitLabel}</button>
            </div>
          </footer>
        </form>
      </div>
    </div>
  );
}
