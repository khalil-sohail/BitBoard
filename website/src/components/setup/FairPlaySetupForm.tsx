"use client";

import { useState } from 'react';
import type { PlayerColor } from '@/types/engine';
import type { SessionController } from '@/session/session-controller.types';
import { AdvancedSettings } from './AdvancedSettings';
import { DifficultyControl, PlayerColorControl, TimeControlControl, ToggleControl } from './SetupControls';
import { SetupSection } from './SetupSection';
import { SessionSetupDialog } from './SessionSetupDialog';

export function FairPlaySetupForm({ session }: { session: SessionController }) {
  const { setup, board } = session;
  const [playerColor, setPlayerColor] = useState<PlayerColor | 'random'>(board.orientation);
  const [timeControl, setTimeControl] = useState(setup.timeControl);
  const [difficulty, setDifficulty] = useState(setup.difficulty);
  const [ownBook, setOwnBook] = useState(setup.ownBook);

  const submit = () => {
    setup.changeOwnBook(ownBook);
    setup.start({ playerColor, timeControl, difficulty });
  };

  return (
    <SessionSetupDialog
      modeLabel="Fair Play"
      description="Choose your side and clock. Live evaluation and suggested moves stay hidden throughout the match."
      submitLabel="Start Fair Play"
      onCancel={setup.close}
      onSubmit={submit}
    >
      <div className="space-y-4">
        <SetupSection title="Match" description="These choices are locked when the game starts.">
          <div className="space-y-5">
            <PlayerColorControl value={playerColor} onChange={setPlayerColor} />
            <TimeControlControl value={timeControl} onChange={setTimeControl} />
            {timeControl.initialMs === 0 && <DifficultyControl value={difficulty} onChange={setDifficulty} />}
            {timeControl.initialMs > 0 && <p className="text-xs text-muted">With a clock, engine strength follows its available time budget.</p>}
          </div>
        </SetupSection>
        <AdvancedSettings defaultExpanded={!ownBook}>
          <ToggleControl label="Opening book" description="Allow the engine to use its configured opening book before normal search." checked={ownBook} onChange={setOwnBook} />
        </AdvancedSettings>
      </div>
    </SessionSetupDialog>
  );
}
