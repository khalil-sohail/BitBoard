import { strict as assert } from 'node:assert';
import { readFileSync } from 'node:fs';
import { deriveCompletedHeadline, deriveFairPlayStatus, formatClockMs } from './fair-play-presentation';

const read = (path: string) => readFileSync(path, 'utf8');
const sidebar = read('src/components/fair-play/FairPlaySidebar.tsx');
const clocks = read('src/components/fair-play/FairPlayClockPanel.tsx');
const history = read('src/components/fair-play/FairPlayHistoryPanel.tsx');
const status = read('src/components/fair-play/FairPlayOperationalStatus.tsx');
const actions = read('src/components/fair-play/FairPlayActions.tsx');
const styles = read('src/components/fair-play/FairPlaySidebar.module.css');
const view = read('src/session/SessionControllerView.tsx');
const controller = read('src/session/useSessionControllerValue.tsx');
const engine = read('src/hooks/useEngine.ts');

const base = {
  lifecycle: 'active' as const,
  connectionStatus: 'idle' as const,
  queuePosition: null,
  searchRetryCount: null,
  waitingForSessionReady: false,
  currentTurn: 'w' as const,
  playerColor: 'w' as const,
};

assert.equal(deriveFairPlayStatus({ ...base, lifecycle: 'idle' }).state, 'idle');
assert.equal(deriveFairPlayStatus({ ...base, waitingForSessionReady: true }).state, 'starting');
assert.deepEqual(deriveFairPlayStatus({ ...base, connectionStatus: 'queued', queuePosition: 2 }), {
  state: 'queued',
  headline: 'Waiting in queue — position 2',
  detail: 'Clocks remain paused while an engine session is assigned.',
  tone: 'waiting',
});
assert.equal(deriveFairPlayStatus(base).headline, 'Your turn');
assert.equal(deriveFairPlayStatus({ ...base, currentTurn: 'b', connectionStatus: 'thinking' }).headline, 'Engine thinking');
assert.equal(deriveFairPlayStatus({ ...base, currentTurn: 'b', connectionStatus: 'thinking', searchRetryCount: 1 }).headline, 'Retrying engine move…');
assert.equal(deriveFairPlayStatus({ ...base, connectionStatus: 'result_ready' }).headline, 'Move received, applying…');
assert.equal(deriveFairPlayStatus({ ...base, connectionStatus: 'disconnected' }).state, 'error');
assert.equal(deriveFairPlayStatus({ ...base, lifecycle: 'completed' }).state, 'completed');
assert.equal(deriveCompletedHeadline('White Wins by Checkmate', 'w'), 'You won');
assert.equal(deriveCompletedHeadline('Black wins on Time', 'w'), 'Engine won');
assert.equal(deriveCompletedHeadline('Draw by Stalemate', 'b'), 'Draw');
assert.equal(deriveCompletedHeadline('Game ended because the engine session became unavailable', 'w'), 'Session ended');
assert.equal(formatClockMs(180_000), '3:00');
assert.equal(formatClockMs(3_601_000), '1:00:01');

assert.match(view, /mode\.value === 'fair' \? \(/);
assert.match(view, /<FairPlaySidebar/);
assert.doesNotMatch(view, /lifecycle\.isComplete && mode\.isTraining/);
assert.doesNotMatch(sidebar, /EnginePanel|EvalBar|EvalGraph|MoveBadge|displayInfo|engineInfo|principal variation|\bdepth\b|\bnodes\b|\bNPS\b/);
assert.doesNotMatch(sidebar, /useEngine\(|useSessionController\(/);
assert.match(sidebar, /data-fair-play-state/);
assert.match(sidebar, /<FairPlayClockPanel/);
assert.match(sidebar, /<FairPlayHistoryPanel/);
assert.match(sidebar, /<FairPlayOperationalStatus/);
assert.match(sidebar, /<FairPlayCompletedSummary/);

assert.match(clocks, /active clock/);
assert.match(clocks, /Clocks paused/);
assert.match(clocks, /Untimed game/);
assert.match(history, /aria-current=\{whiteLatest/);
assert.match(history, /followLatestRef/);
assert.doesNotMatch(history, /grades|MoveBadge|evaluation/);
assert.match(status, /aria-live=\{isError \? 'assertive' : 'polite'\}/);
assert.match(actions, /onResign/);
assert.match(actions, /Set up game/);
assert.match(actions, /New game/);
assert.doesNotMatch(actions, />Undo</);
assert.match(styles, /@media \(max-width: 74\.999rem\)/);
assert.match(styles, /overflow: visible/);

assert.match(controller, /fairPlay: \{[\s\S]*searchRetryCount/);
assert.doesNotMatch(controller.match(/fairPlay: \{([\s\S]*?)\n    \},/)?.[1] ?? '', /displayEngineInfo|evaluation|pvs|depth|nodes/);
assert.match(engine, /case 'search-retrying':[\s\S]*setSearchRetryCount/);
assert.match(engine, /isWaitingForNewGameReady = true;[\s\S]*setWaitingForSessionReady\(true\)/);
assert.match(controller, /acknowledgeAppliedEngineMove/);
assert.match(controller, /searchStartedPositionKey/);
assert.match(controller, /releaseSession/);
assert.match(controller, /setFairPlaySessionFailure\('Game ended because the engine session became unavailable'\)/);

console.log('Fair Play sidebar tests passed');
