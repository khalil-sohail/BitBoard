import { strict as assert } from 'assert';
import {
  assertTrainingInvariant,
  canChangeTrainingSettings,
  canPlayerMove,
  initialTrainingState,
  isBoardLocked,
  isEngineWorkPending,
  resultFromTerminal,
  trainingReducer,
  type TrainingState,
} from './training-machine';

const whitePromotion = { from: 'e7', to: 'e8', color: 'w' as const };

function reduce(state: TrainingState, events: Parameters<typeof trainingReducer>[1][]): TrainingState {
  return events.reduce(trainingReducer, state);
}

function testEntryAndExit(): void {
  const white = reduce(initialTrainingState, [
    { type: 'ENTER', playerColor: 'w' },
    { type: 'READY', turn: 'w' },
  ]);
  assert.deepEqual(white, { status: 'waiting-player', playerColor: 'w' });

  const black = reduce(initialTrainingState, [
    { type: 'ENTER', playerColor: 'b' },
    { type: 'READY', turn: 'w' },
  ]);
  assert.deepEqual(black, { status: 'waiting-engine-move', playerColor: 'b' });

  for (const state of states()) {
    assert.deepEqual(trainingReducer(state, { type: 'EXIT' }), { status: 'inactive' });
  }
}

function testPlayerAndPromotionFlow(): void {
  const waiting: TrainingState = { status: 'waiting-player', playerColor: 'w' };
  assert.deepEqual(trainingReducer(waiting, { type: 'REVIEW_STARTED', reviewId: 1 }), {
    status: 'reviewing-player-move',
    playerColor: 'w',
    reviewId: 1,
  });

  const promotion = trainingReducer(waiting, { type: 'PROMOTION_REQUIRED', promotion: whitePromotion });
  assert.deepEqual(promotion, { status: 'promotion-pending', playerColor: 'w', promotion: whitePromotion });
  assert.deepEqual(trainingReducer(promotion, { type: 'PROMOTION_CANCELLED' }), waiting);
  assert.deepEqual(trainingReducer(promotion, { type: 'PROMOTION_SELECTED', piece: 'n', reviewId: 2 }), {
    status: 'reviewing-player-move',
    playerColor: 'w',
    reviewId: 2,
  });
}

function testReviewFlow(): void {
  const reviewing: TrainingState = { status: 'reviewing-player-move', playerColor: 'w', reviewId: 7 };
  assert.equal(trainingReducer(reviewing, { type: 'REVIEW_COMPLETED', reviewId: 8, available: true }), reviewing);
  const feedback = trainingReducer(reviewing, { type: 'REVIEW_COMPLETED', reviewId: 7, available: true });
  assert.deepEqual(feedback, { status: 'showing-feedback', playerColor: 'w', reviewId: 7, available: true });
  assert.deepEqual(trainingReducer(feedback, { type: 'FEEDBACK_SHOWN' }), {
    status: 'waiting-engine-move',
    playerColor: 'w',
  });

  const unavailable = trainingReducer(reviewing, { type: 'REVIEW_COMPLETED', reviewId: 7, available: false });
  assert.deepEqual(unavailable, { status: 'showing-feedback', playerColor: 'w', reviewId: 7, available: false });
}

function testEngineAndTerminalFlow(): void {
  const waitingEngine: TrainingState = { status: 'waiting-engine-move', playerColor: 'b' };
  const withRequest = trainingReducer(waitingEngine, { type: 'ENGINE_SEARCH_STARTED', requestId: 11 });
  assert.deepEqual(withRequest, { status: 'waiting-engine-move', playerColor: 'b', requestId: 11 });
  assert.equal(trainingReducer(withRequest, { type: 'ENGINE_MOVE_RECEIVED', requestId: 12 }), withRequest);
  assert.deepEqual(trainingReducer(withRequest, { type: 'ENGINE_MOVE_RECEIVED', requestId: 11 }), {
    status: 'waiting-player',
    playerColor: 'b',
  });

  assert.deepEqual(trainingReducer(withRequest, {
    type: 'TERMINAL',
    result: { reason: 'checkmate', winner: 'white' },
  }), {
    status: 'game-over',
    result: { reason: 'checkmate', winner: 'white' },
  });

  assert.deepEqual(resultFromTerminal({ reason: 'stalemate' }), { reason: 'stalemate' });
}

function testResetFlow(): void {
  for (const state of states().filter(s => s.status !== 'inactive')) {
    const resetting = trainingReducer(state, { type: 'RESET_REQUESTED', reason: 'manual', playerColor: 'w' });
    assert.equal(resetting.status, 'resetting');
    assert.deepEqual(trainingReducer(resetting, { type: 'RESET_COMPLETED', playerColor: 'w', turn: 'b' }), {
      status: 'waiting-engine-move',
      playerColor: 'w',
    });
  }
}

function testConnectionAndErrorFlow(): void {
  const duringReview: TrainingState = { status: 'reviewing-player-move', playerColor: 'w', reviewId: 1 };
  const lost = trainingReducer(duringReview, { type: 'DISCONNECTED' });
  assert.deepEqual(lost, { status: 'connection-lost', recoverTo: { status: 'waiting-player', playerColor: 'w' } });
  assert.deepEqual(trainingReducer(lost, { type: 'RECONNECTED', turn: 'w' }), { status: 'waiting-player', playerColor: 'w' });

  const duringEngine: TrainingState = { status: 'waiting-engine-move', playerColor: 'b', requestId: 3 };
  const lostEngine = trainingReducer(duringEngine, { type: 'DISCONNECTED' });
  assert.deepEqual(trainingReducer(lostEngine, { type: 'RECONNECTED', turn: 'w' }), {
    status: 'waiting-engine-move',
    playerColor: 'b',
  });

  assert.deepEqual(trainingReducer(duringEngine, { type: 'ENGINE_FAILED', message: 'boom' }), {
    status: 'engine-error',
    message: 'boom',
  });
}

function testSelectorsAndInvariants(): void {
  assert.equal(canPlayerMove({ status: 'waiting-player', playerColor: 'w' }), true);
  assert.equal(isBoardLocked({ status: 'waiting-player', playerColor: 'w' }), false);
  assert.equal(isBoardLocked({ status: 'reviewing-player-move', playerColor: 'w', reviewId: 1 }), true);
  assert.equal(isEngineWorkPending({ status: 'waiting-engine-move', playerColor: 'w' }), true);
  assert.equal(canChangeTrainingSettings({ status: 'reviewing-player-move', playerColor: 'w', reviewId: 1 }), false);
  assert.equal(canChangeTrainingSettings({ status: 'waiting-player', playerColor: 'w' }), true);

  for (const state of states()) {
    assert.doesNotThrow(() => assertTrainingInvariant(state));
  }
}

function testIllegalTransitionsAreSafe(): void {
  const waiting: TrainingState = { status: 'waiting-player', playerColor: 'w' };
  assert.equal(trainingReducer(waiting, { type: 'ENGINE_MOVE_RECEIVED', requestId: 1 }), waiting);
  assert.equal(trainingReducer(initialTrainingState, { type: 'DISCONNECTED' }), initialTrainingState);
  assert.equal(trainingReducer(initialTrainingState, {
    type: 'TERMINAL',
    result: { reason: 'draw' },
  }), initialTrainingState);
}

function states(): TrainingState[] {
  return [
    { status: 'inactive' },
    { status: 'initializing', playerColor: 'w' },
    { status: 'waiting-player', playerColor: 'w' },
    { status: 'promotion-pending', playerColor: 'w', promotion: whitePromotion },
    { status: 'reviewing-player-move', playerColor: 'w', reviewId: 1 },
    { status: 'showing-feedback', playerColor: 'w', reviewId: 1, available: true },
    { status: 'waiting-engine-move', playerColor: 'w', requestId: 2 },
    { status: 'resetting', reason: 'manual', playerColor: 'w' },
    { status: 'connection-lost', recoverTo: { status: 'waiting-player', playerColor: 'w' } },
    { status: 'engine-error', message: 'boom' },
    { status: 'game-over', result: { reason: 'draw' } },
  ];
}

function run(): void {
  testEntryAndExit();
  testPlayerAndPromotionFlow();
  testReviewFlow();
  testEngineAndTerminalFlow();
  testResetFlow();
  testConnectionAndErrorFlow();
  testSelectorsAndInvariants();
  testIllegalTransitionsAreSafe();
  console.log('training machine tests passed');
}

run();
