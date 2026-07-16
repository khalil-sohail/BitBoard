"use client";

import { useCallback, useEffect, useId, useRef, useState, type ReactNode } from 'react';
import type { GameMode } from '@/types/engine';
import { ModeNavigation } from '@/components/layout/ModeNavigation';
import { SidebarRegion } from '@/components/layout/SidebarRegion';
import { CompactStatusStrip } from './CompactStatusStrip';
import { SessionBottomPanel } from './SessionBottomPanel';
import type { CompactSessionStatus } from './responsive-session.types';
import styles from './ResponsiveSession.module.css';

const COMPACT_QUERY = '(max-width: 74.999rem)';
const MOBILE_QUERY = '(max-width: 47.999rem)';
const FOCUSABLE = 'button:not([disabled]), [href], input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';

export function ResponsiveSessionPanel({ mode, onModeChange, status, sessionEngaged, children }: {
  mode: GameMode; onModeChange: (mode: GameMode) => void; status: CompactSessionStatus;
  sessionEngaged: boolean; children: ReactNode;
}) {
  const panelId = `session-panel-${useId().replace(/:/g, '')}`;
  const [expanded, setExpanded] = useState(false);
  const [compact, setCompact] = useState(false);
  const [mobile, setMobile] = useState(false);
  const toggleRef = useRef<HTMLDivElement>(null);
  const panelRef = useRef<HTMLDivElement>(null);
  const closeRef = useRef<HTMLButtonElement>(null);
  const restoreFocusRef = useRef(false);

  useEffect(() => {
    const compactQuery = window.matchMedia(COMPACT_QUERY);
    const mobileQuery = window.matchMedia(MOBILE_QUERY);
    const sync = () => { setCompact(compactQuery.matches); setMobile(mobileQuery.matches); };
    sync();
    compactQuery.addEventListener('change', sync);
    mobileQuery.addEventListener('change', sync);
    return () => { compactQuery.removeEventListener('change', sync); mobileQuery.removeEventListener('change', sync); };
  }, []);

  const collapse = useCallback((restoreFocus = true) => {
    restoreFocusRef.current = restoreFocus;
    setExpanded(false);
  }, []);

  useEffect(() => {
    if (expanded || !restoreFocusRef.current) return;
    restoreFocusRef.current = false;
    const frame = requestAnimationFrame(() => toggleRef.current?.querySelector<HTMLButtonElement>('button')?.focus());
    return () => cancelAnimationFrame(frame);
  }, [expanded]);

  useEffect(() => {
    if (!expanded || !mobile) return;
    const previousOverflow = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    const frame = requestAnimationFrame(() => closeRef.current?.focus());
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') { event.preventDefault(); collapse(); return; }
      if (event.key !== 'Tab' || !panelRef.current) return;
      const focusable = [...panelRef.current.querySelectorAll<HTMLElement>(FOCUSABLE)].filter(element => !element.hasAttribute('disabled'));
      if (!focusable.length) return;
      const first = focusable[0];
      const last = focusable[focusable.length - 1];
      if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus(); }
      else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus(); }
    };
    document.addEventListener('keydown', onKeyDown);
    return () => {
      cancelAnimationFrame(frame);
      document.body.style.overflow = previousOverflow;
      document.removeEventListener('keydown', onKeyDown);
    };
  }, [collapse, expanded, mobile]);

  const toggle = () => expanded ? collapse(false) : setExpanded(true);
  return <div className={styles.region} data-responsive-session-panel data-expanded={expanded} data-compact={compact} data-mobile={mobile} data-session-engaged={sessionEngaged}>
    <ModeNavigation mode={mode} onModeChange={nextMode => { setExpanded(false); onModeChange(nextMode); }} />
    <div ref={toggleRef} className={styles.compactBoundary}>
      <CompactStatusStrip status={status} expanded={expanded} controls={panelId} onToggle={toggle} />
    </div>
    {mobile && expanded ? <div className={styles.backdrop} aria-hidden="true" onClick={() => collapse()} /> : null}
    <SessionBottomPanel id={panelId} title={status.modeLabel} expanded={expanded} compact={compact} mobile={mobile && expanded} panelRef={panelRef} closeRef={closeRef} onClose={() => collapse()}>
      <SidebarRegion>{children}</SidebarRegion>
    </SessionBottomPanel>
  </div>;
}
