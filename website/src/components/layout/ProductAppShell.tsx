"use client";

import { useEffect, useState, type ReactNode } from 'react';
import type { GameMode } from '@/types/engine';
import type { SessionLifecycleStatus } from '@/session/session-lifecycle';
import type { CompactSessionStatus } from '@/components/responsive/responsive-session.types';
import { ProductFooter } from './ProductFooter';
import { ProductHeader } from './ProductHeader';
import { SessionWorkspace } from './SessionWorkspace';
import styles from './ProductLayout.module.css';

interface ProductAppShellProps {
  mode: GameMode;
  onModeChange: (mode: GameMode) => void;
  sessionActive: boolean;
  board: ReactNode;
  evaluationBar?: ReactNode;
  sidebar: ReactNode;
  sessionStatus: SessionLifecycleStatus;
  compactStatus: CompactSessionStatus;
}

export function ProductAppShell(props: ProductAppShellProps) {
  const [isFullscreen, setIsFullscreen] = useState(false);

  useEffect(() => {
    const syncFullscreen = () => setIsFullscreen(Boolean(document.fullscreenElement));
    syncFullscreen();
    document.addEventListener('fullscreenchange', syncFullscreen);
    return () => document.removeEventListener('fullscreenchange', syncFullscreen);
  }, []);

  return (
    <div
      className={`${styles.shell} bg-zinc-950 text-zinc-100`}
      data-product-app-shell
      data-fullscreen={isFullscreen}
      data-session-active={props.sessionActive}
    >
      <ProductHeader />
      <SessionWorkspace
        mode={props.mode}
        onModeChange={props.onModeChange}
        board={props.board}
        evaluationBar={props.evaluationBar}
        sidebar={props.sidebar}
        sessionStatus={props.sessionStatus}
        compactStatus={props.compactStatus}
      />
      <ProductFooter />
    </div>
  );
}
