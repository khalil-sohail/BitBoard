"use client";

import { useState } from 'react';
import type { PlayerColor } from '@/types/engine';
import type { SessionController } from '@/session/session-controller.types';
import { AdvancedSettings } from './AdvancedSettings';
import { DifficultyControl, PlayerColorControl, RangeControl, ToggleControl } from './SetupControls';
import { SetupSection } from './SetupSection';
import { SessionSetupDialog } from './SessionSetupDialog';

export function TrainingSetupForm({ session }: { session: SessionController }) {
  const { setup, board } = session;
  const [playerColor, setPlayerColor] = useState<PlayerColor | 'random'>(board.orientation);
  const [difficulty, setDifficulty] = useState(setup.difficulty);
  const [reviewDepth, setReviewDepth] = useState(setup.maxDepth);
  const [ownBook, setOwnBook] = useState(setup.ownBook);
  const [pondering, setPondering] = useState(setup.trainingPonderEnabled);

  const submit = () => {
    setup.changeOwnBook(ownBook);
    setup.changeTrainingPonder(pondering);
    setup.start({ playerColor, difficulty, timeControl: setup.timeControl, maxDepth: reviewDepth });
  };

  return (
    <SessionSetupDialog
      modeLabel="Training"
      description="Set the opponent and review depth. Hints, grading, and correlated move review begin after the game starts."
      submitLabel="Start Training"
      onCancel={setup.close}
      onSubmit={submit}
    >
      <div className="space-y-4">
        <SetupSection title="Training game" description="Training is untimed; review searches use the selected depth.">
          <div className="space-y-5">
            <PlayerColorControl value={playerColor} onChange={setPlayerColor} />
            <DifficultyControl value={difficulty} onChange={setDifficulty} />
            <RangeControl id="training-review-depth" label="Review depth" value={reviewDepth} min={1} max={30} onChange={setReviewDepth} />
          </div>
        </SetupSection>
        <AdvancedSettings defaultExpanded={!ownBook || pondering}>
          <ToggleControl label="Opening book" description="Use book moves when available before switching to normal search." checked={ownBook} onChange={setOwnBook} />
          <ToggleControl label="Pondering" description="Allow the Training opponent to think during your turn." checked={pondering} onChange={setPondering} />
        </AdvancedSettings>
      </div>
    </SessionSetupDialog>
  );
}
