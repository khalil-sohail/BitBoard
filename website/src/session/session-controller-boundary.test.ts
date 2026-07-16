import { strict as assert } from 'node:assert';
import { readFileSync } from 'node:fs';
import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { SessionControllerContext } from './SessionControllerContext';
import { useSessionController } from './useSessionController';
import type { SessionController } from './session-controller.types';

const read = (path: string) => readFileSync(path, 'utf8');
const page = read('src/app/page.tsx');
const provider = read('src/session/SessionControllerProvider.tsx');
const controller = read('src/session/useSessionControllerValue.tsx');
const view = read('src/session/SessionControllerView.tsx');
const shell = read('src/components/layout/ProductAppShell.tsx');

function ContextConsumer() {
  return createElement('span', null, useSessionController().mode.value);
}

function testContextContract(): void {
  assert.throws(
    () => renderToStaticMarkup(createElement(ContextConsumer)),
    /useSessionController must be used within a SessionControllerProvider/,
  );

  const fake = { mode: { value: 'training' } } as unknown as SessionController;
  const markup = renderToStaticMarkup(createElement(
    SessionControllerContext.Provider,
    { value: fake },
    createElement(ContextConsumer),
  ));
  assert.equal(markup, '<span>training</span>');
}

function testPersistentBoundary(): void {
  assert.match(page, /<SessionControllerProvider>[\s\S]*<SessionControllerView/);
  assert.doesNotMatch(page, /useEngine\(|useChessGame\(|useChessClock\(|useState\(|useEffect\(/);
  assert.doesNotMatch(provider, /key=|fullscreen|viewport|mode=/);
  assert.match(provider, /useSessionControllerValue\(\)/);
  assert.match(view, /useSessionController\(\)/);
  assert.match(view, /<ProductAppShell/);
  assert.match(shell, /fullscreenchange/);
  assert.doesNotMatch(shell, /SessionControllerProvider/);
}

function testControllerOwnershipAndDomains(): void {
  assert.equal((controller.match(/useEngine\(\)/g) ?? []).length, 1);
  assert.equal((controller.match(/useChessGame\(\)/g) ?? []).length, 1);
  assert.equal((controller.match(/useChessClock\(/g) ?? []).length, 1);
  assert.equal((controller.match(/useMoveReview\(\)/g) ?? []).length, 1);
  assert.equal((controller.match(/useTrainingHint\(/g) ?? []).length, 1);
  for (const domain of ['mode', 'lifecycle', 'board', 'engine', 'clocks', 'setup', 'fairPlay', 'training', 'analysis', 'history', 'actions']) {
    assert.match(controller, new RegExp(`\\n    ${domain}: \\{`));
  }
  const fairPlayDomain = controller.match(/fairPlay: \{([\s\S]*?)\n    \},\n    training:/)?.[1] ?? '';
  assert.doesNotMatch(fairPlayDomain, /displayInfo|evaluation|principal|pvs|arrows/);
}

function testProtocolCriticalLifetimeIsPreserved(): void {
  for (const ref of [
    'ignoreStaleBestMoveRef',
    'appliedOwnBookRef',
    'lastClockSearchPositionRef',
    'appliedBestMoveRequestRef',
    'analysisFenRef',
    'nextReviewIdRef',
    'pendingMoveReviewRef',
    'latestEngineInfoRef',
  ]) assert.match(controller, new RegExp(`const ${ref} = useRef`));

  for (const invariant of [
    'searchStartedRequestId',
    'searchStartedPositionKey',
    'shouldAcceptEngineBestMove',
    'createOpponentMoveApplicationReceipt',
    'fen-mismatch',
    'side-to-move-mismatch',
    'duplicate-request',
    'acknowledgeAppliedEngineMove',
    'clockTransitionAfterLegalMove',
    'shouldStartPlayerClock',
    "dispatchTraining({ type: 'REVIEW_COMPLETED'",
    "dispatchTraining({ type: 'RESET_REQUESTED', reason: 'mode-switch'",
  ]) assert.ok(controller.includes(invariant), `missing invariant: ${invariant}`);
}

function testPresentationEquivalence(): void {
  for (const component of [
    'ChessBoardComponent', 'EvalBar', 'FairPlaySidebar',
    'TrainingSidebar', 'AnalysisSidebar', 'SessionSetupHost',
  ]) assert.match(view, new RegExp(`<${component}`));
  assert.match(view, /arrows=\{engine\.showPanel \? board\.arrows : \[\]\}/);
  assert.doesNotMatch(view, /<EnginePanel|<MoveHistory|<GameControls/);
  assert.doesNotMatch(view, /PositionSetup|EngineToggle|NewGameModal/);
}

testContextContract();
testPersistentBoundary();
testControllerOwnershipAndDomains();
testProtocolCriticalLifetimeIsPreserved();
testPresentationEquivalence();
console.log('session controller boundary tests passed');
