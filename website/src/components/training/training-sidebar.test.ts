import { strict as assert } from 'node:assert';
import { readFileSync } from 'node:fs';
import type { TrainingState } from '../../lib/training-machine';
import { deriveTrainingPresentation } from './training-presentation';

const read = (path: string) => readFileSync(path, 'utf8');
const sidebar = read('src/components/training/TrainingSidebar.tsx');
const task = read('src/components/training/TrainingTaskPanel.tsx');
const feedback = read('src/components/training/TrainingFeedbackPanel.tsx');
const hint = read('src/components/training/TrainingHintPanel.tsx');
const analysis = read('src/components/training/TrainingAnalysisPanel.tsx');
const history = read('src/components/training/TrainingHistoryPanel.tsx');
const actions = read('src/components/training/TrainingActions.tsx');
const styles = read('src/components/training/TrainingSidebar.module.css');
const view = read('src/session/SessionControllerView.tsx');
const controller = read('src/session/useSessionControllerValue.tsx');

const presentation = (state: TrainingState, connectionStatus: 'idle' | 'thinking' | 'result_ready' = 'idle') => deriveTrainingPresentation({
  lifecycle: state.status === 'inactive' ? 'idle' : state.status === 'game-over' ? 'completed' : 'active',
  trainingState: state,
  connectionStatus,
  currentTurn: 'w',
  playerColor: 'w',
  hasLatestFeedback: true,
});

assert.equal(presentation({ status: 'inactive' }).state, 'idle');
assert.equal(presentation({ status: 'initializing', playerColor: 'w' }).state, 'starting');
assert.equal(presentation({ status: 'waiting-player', playerColor: 'w' }).state, 'player-turn');
assert.equal(presentation({ status: 'reviewing-player-move', playerColor: 'w', reviewId: 4 }).state, 'reviewing');
assert.equal(presentation({ status: 'showing-feedback', playerColor: 'w', reviewId: 4, available: true }).state, 'feedback-ready');
assert.equal(presentation({ status: 'waiting-engine-move', playerColor: 'w' }, 'thinking').state, 'opponent-thinking');
assert.equal(presentation({ status: 'waiting-engine-move', playerColor: 'w' }, 'result_ready').state, 'applying-opponent-move');
assert.equal(presentation({ status: 'game-over', result: { reason: 'draw' } }).state, 'completed');
assert.equal(presentation({ status: 'engine-error', message: 'unavailable' }).state, 'error');

assert.match(view, /mode\.isTraining \? \([\s\S]*<TrainingSidebar/);
assert.doesNotMatch(view, /lifecycle\.isComplete && mode\.isTraining/);
assert.doesNotMatch(sidebar, /useEngine\(|useSessionController\(|resultAck|OpponentMoveApplicationReceipt/);
assert.doesNotMatch(sidebar, /difficulty.*onChange|time control|opening book|ponder/i);
assert.match(sidebar, /<TrainingTaskPanel/);
assert.match(sidebar, /<TrainingFeedbackPanel/);
assert.match(sidebar, /<TrainingHintPanel/);
assert.match(sidebar, /<TrainingAnalysisPanel/);
assert.match(sidebar, /<TrainingHistoryPanel/);
assert.match(sidebar, /<TrainingCompletedSummary/);

assert.match(task, /aria-live="polite"/);
assert.match(feedback, /latest = grades\.reduce/);
assert.match(feedback, /<MoveBadge grade=\{latest\.grade\}/);
assert.match(feedback, /estimated loss/);
assert.match(hint, /disabled=\{!canRequest \|\| searching\}/);
assert.match(hint, /onClick=\{onRequest\}/);
assert.match(hint, /role="status"/);
assert.match(analysis, /White-perspective evaluation/);
assert.match(analysis, /info\?\.purpose === 'training-result-review'/);
assert.match(analysis, /<details className=\{styles\.metrics\}/);
assert.match(history, /<caption className="sr-only">/);
assert.match(history, /grade\.grade/);
assert.match(history, /followLatestRef/);
assert.match(actions, /Undo is available only on your turn/);
assert.match(styles, /@media \(max-width: 1219px\)/);
assert.match(styles, /overflow-wrap: anywhere/);

// Reliability remains controller-owned and exact-result correlated.
assert.match(controller, /resultRequestId = startResolvedAnalysis\(fen, 'training-result-review'\)/);
assert.match(controller, /bestMoveResult\.requestId !== pendingReview\.resultRequestId/);
assert.match(controller, /moveResult\?\.purpose !== 'opponent'/);
assert.match(controller, /createOpponentMoveApplicationReceipt\(moveResult, gameMode\)/);
assert.match(controller, /startAnalysis\(rootFen, \[\], undefined/);
assert.match(controller, /setLatestTrainingReviewInfo\(null\)[\s\S]*setGameMode\(newMode\)/);

console.log('Training sidebar tests passed');
