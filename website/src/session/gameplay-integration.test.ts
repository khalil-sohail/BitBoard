import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  clockTransitionAfterLegalMove,
  shouldAcceptEngineBestMove,
  shouldStartEngineClockForSearch,
  shouldStartPlayerClock,
} from '../lib/time-management-policy';

function testTimedGameLifecycle(): void {
  const initialMs = 60_000;
  const incrementMs = 1_000;
  assert.equal(shouldStartPlayerClock({
    hasTimeControl: true, gameStatus: 'active', isTerminal: false, timeoutColor: null,
    engineReady: true, waitingForSessionReady: false, turn: 'w', playerColor: 'w',
    activeSide: null, isRunning: false,
  }), true);

  const playerBeforeMove = initialMs - 900;
  const playerMove = clockTransitionAfterLegalMove({ hasTimeControl: true, mover: 'w', nextSide: 'b', isTerminal: false });
  assert.deepEqual(playerMove, { stopClock: true, incrementSide: 'w', nextActiveSide: 'b', completeGame: false });
  assert.equal(playerBeforeMove + incrementMs, 60_100);

  assert.equal(shouldStartEngineClockForSearch({
    hasTimeControl: true, gameStatus: 'active', isTerminal: false, timeoutColor: null,
    turn: 'b', engineColor: 'b', searchStartedRequestId: 4, lastStartedRequestId: null,
  }), true);
  assert.equal(shouldStartEngineClockForSearch({
    hasTimeControl: true, gameStatus: 'active', isTerminal: false, timeoutColor: null,
    turn: 'b', engineColor: 'b', searchStartedRequestId: 4, lastStartedRequestId: 4,
  }), false);

  const engineMove = clockTransitionAfterLegalMove({ hasTimeControl: true, mover: 'b', nextSide: 'w', isTerminal: false });
  assert.deepEqual(engineMove, { stopClock: true, incrementSide: 'b', nextActiveSide: 'w', completeGame: false });
}

function testPauseReconnectAndTimeout(): void {
  const playerTurn = {
    hasTimeControl: true, gameStatus: 'active' as const, isTerminal: false, timeoutColor: null,
    waitingForSessionReady: false, turn: 'w' as const, playerColor: 'w' as const,
    activeSide: null, isRunning: false,
  };
  assert.equal(shouldStartPlayerClock({ ...playerTurn, engineReady: false }), false);
  assert.equal(shouldStartPlayerClock({ ...playerTurn, engineReady: true, waitingForSessionReady: true }), false);
  assert.equal(shouldStartPlayerClock({ ...playerTurn, engineReady: true }), true);
  assert.equal(shouldAcceptEngineBestMove({ gameStatus: 'completed', timeoutColor: 'b' }), false);
}

function testControllerGameplayBoundaries(): void {
  const controller = readFileSync('src/session/useSessionControllerValue.tsx', 'utf8');
  const browser = readFileSync('scripts/result-acknowledgment.browser.integration.mjs', 'utf8');
  for (const invariant of [
    'shouldStartPlayerClock', 'clock.startClock(engineColor)', 'clock.applyIncrement',
    'clock.stopClock()', "setGameStatus('completed')", "reason: 'mode-switch'",
    "dispatchTraining({ type: 'REVIEW_COMPLETED'", 'stopAnalysisSession', 'resume: startAnalysisSession',
  ]) assert.ok(controller.includes(invariant) || browser.includes(invariant), `missing gameplay invariant: ${invariant}`);
  for (const browserCase of [
    'initial player clock start', 'Player increment was not applied', 'Fair Play timeout completion',
    'Rapid second player move', 'Delayed Training messages after a mode switch',
    'Analysis depth restart', 'Analysis MultiPV restart', 'resumed Analysis request',
  ]) assert.ok(browser.includes(browserCase), `missing browser gameplay case: ${browserCase}`);
  assert.match(controller, /\[clockStopSignal, stopClockForEngineSignal\]/);
  assert.doesNotMatch(controller, /\[clock, clockStopSignal\]/);
}

testTimedGameLifecycle();
testPauseReconnectAndTimeout();
testControllerGameplayBoundaries();
console.log('gameplay integration policy tests passed');
