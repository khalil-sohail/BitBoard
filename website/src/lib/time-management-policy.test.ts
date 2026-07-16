import { strict as assert } from 'assert';
import {
  clockTransitionAfterLegalMove,
  shouldAcceptEngineBestMove,
  shouldStartEngineClockForSearch,
  shouldStartPlayerClock,
} from './time-management-policy';

function testTerminalMoveClockTransition(): void {
  assert.deepEqual(clockTransitionAfterLegalMove({
    hasTimeControl: true,
    mover: 'w',
    nextSide: 'b',
    isTerminal: true,
  }), {
    stopClock: true,
    incrementSide: 'w',
    nextActiveSide: undefined,
    completeGame: true,
  });
}

function testNonTerminalMoveClockTransition(): void {
  assert.deepEqual(clockTransitionAfterLegalMove({
    hasTimeControl: true,
    mover: 'b',
    nextSide: 'w',
    isTerminal: false,
  }), {
    stopClock: true,
    incrementSide: 'b',
    nextActiveSide: 'w',
    completeGame: false,
  });
}

function testUntimedTerminalMoveCompletesWithoutClockMutation(): void {
  assert.deepEqual(clockTransitionAfterLegalMove({
    hasTimeControl: false,
    mover: 'w',
    nextSide: 'b',
    isTerminal: true,
  }), {
    stopClock: false,
    completeGame: true,
  });
}

function testFlagFallRejectsLateBestmove(): void {
  assert.equal(shouldAcceptEngineBestMove({ gameStatus: 'active', timeoutColor: null }), true);
  assert.equal(shouldAcceptEngineBestMove({ gameStatus: 'active', timeoutColor: 'w' }), false);
  assert.equal(shouldAcceptEngineBestMove({ gameStatus: 'completed', timeoutColor: null }), false);
}

function testQueuedSearchClockStartPolicy(): void {
  assert.equal(shouldStartEngineClockForSearch({
    hasTimeControl: true,
    gameStatus: 'active',
    isTerminal: false,
    timeoutColor: null,
    turn: 'b',
    engineColor: 'b',
    searchStartedRequestId: null,
    lastStartedRequestId: null,
  }), false);

  assert.equal(shouldStartEngineClockForSearch({
    hasTimeControl: true,
    gameStatus: 'active',
    isTerminal: false,
    timeoutColor: null,
    turn: 'b',
    engineColor: 'b',
    searchStartedRequestId: 10,
    lastStartedRequestId: null,
  }), true);

  assert.equal(shouldStartEngineClockForSearch({
    hasTimeControl: true,
    gameStatus: 'active',
    isTerminal: false,
    timeoutColor: null,
    turn: 'b',
    engineColor: 'b',
    searchStartedRequestId: 10,
    lastStartedRequestId: 10,
  }), false);
}

function testPlayerClockStartAndReconnectPolicy(): void {
  const readyTurn = {
    hasTimeControl: true,
    gameStatus: 'active' as const,
    isTerminal: false,
    timeoutColor: null,
    engineReady: true,
    waitingForSessionReady: false,
    turn: 'w' as const,
    playerColor: 'w' as const,
    activeSide: null,
    isRunning: false,
  };
  assert.equal(shouldStartPlayerClock(readyTurn), true);
  assert.equal(shouldStartPlayerClock({ ...readyTurn, waitingForSessionReady: true }), false);
  assert.equal(shouldStartPlayerClock({ ...readyTurn, engineReady: false }), false);
  assert.equal(shouldStartPlayerClock({ ...readyTurn, turn: 'b' }), false);
  assert.equal(shouldStartPlayerClock({ ...readyTurn, hasTimeControl: false }), false);
  assert.equal(shouldStartPlayerClock({ ...readyTurn, isRunning: true, activeSide: 'w' }), false);
  assert.equal(shouldStartPlayerClock({ ...readyTurn, isRunning: true, activeSide: 'b' }), true);
  assert.equal(shouldStartPlayerClock({ ...readyTurn, timeoutColor: 'w' }), false);
  assert.equal(shouldStartPlayerClock({ ...readyTurn, gameStatus: 'completed' }), false);
}

testTerminalMoveClockTransition();
testNonTerminalMoveClockTransition();
testUntimedTerminalMoveCompletesWithoutClockMutation();
testFlagFallRejectsLateBestmove();
testQueuedSearchClockStartPolicy();
testPlayerClockStartAndReconnectPolicy();

console.log('time-management policy tests passed');
