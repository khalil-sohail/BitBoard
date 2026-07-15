import type { ReactNode } from 'react';
import type { GameMode } from '@/types/engine';
import { BoardRegion } from './BoardRegion';
import { ModeNavigation } from './ModeNavigation';
import { SidebarRegion } from './SidebarRegion';
import styles from './ProductLayout.module.css';

interface SessionWorkspaceProps {
  mode: GameMode;
  onModeChange: (mode: GameMode) => void;
  board: ReactNode;
  evaluationBar?: ReactNode;
  sidebar: ReactNode;
}

export function SessionWorkspace(props: SessionWorkspaceProps) {
  return (
    <main className={styles.main}>
      <div className={styles.workspace}>
        <BoardRegion board={props.board} evaluationBar={props.evaluationBar} />
        <div className={styles.sideRail}>
          <ModeNavigation mode={props.mode} onModeChange={props.onModeChange} />
          <SidebarRegion>{props.sidebar}</SidebarRegion>
        </div>
      </div>
    </main>
  );
}
