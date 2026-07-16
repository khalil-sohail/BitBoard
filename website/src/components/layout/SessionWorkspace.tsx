import type { ReactNode } from 'react';
import type { GameMode } from '@/types/engine';
import type { SessionLifecycleStatus } from '@/session/session-lifecycle';
import { ResponsiveSessionPanel } from '@/components/responsive/ResponsiveSessionPanel';
import type { CompactSessionStatus } from '@/components/responsive/responsive-session.types';
import { BoardRegion } from './BoardRegion';
import styles from './ProductLayout.module.css';

interface SessionWorkspaceProps {
  mode: GameMode;
  onModeChange: (mode: GameMode) => void;
  board: ReactNode;
  evaluationBar?: ReactNode;
  sidebar: ReactNode;
  sessionStatus: SessionLifecycleStatus;
  compactStatus: CompactSessionStatus;
}

export function SessionWorkspace(props: SessionWorkspaceProps) {
  return (
    <main className={styles.main}>
      <div className={styles.workspace}>
        <BoardRegion board={props.board} evaluationBar={props.evaluationBar} />
        <ResponsiveSessionPanel mode={props.mode} onModeChange={props.onModeChange} status={props.compactStatus} sessionEngaged={props.sessionStatus !== 'idle'}>
          {props.sidebar}
        </ResponsiveSessionPanel>
      </div>
    </main>
  );
}
