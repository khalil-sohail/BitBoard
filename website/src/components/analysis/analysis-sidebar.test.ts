import { strict as assert } from 'node:assert';
import { readFileSync } from 'node:fs';
import type { AnalysisSnapshot } from '../../lib/board-arrows';
import { analysisDisplayReducer, selectCurrentAnalysisSnapshot } from '../../session/analysis-display-state';
import { deriveAnalysisPresentation, evaluationText, sourceLabel } from './analysis-presentation';

const FEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
const NEXT_FEN = 'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1';

function snapshot(overrides: Partial<AnalysisSnapshot> = {}): AnalysisSnapshot {
  return {
    requestId: 7, mode: 'analysis', purpose: 'analysis', fen: FEN, positionFen: FEN,
    positionKey: 'position-key', sessionId: 'session-id', sessionGeneration: 3,
    requestedLimit: { mode: 'depth', depth: 10 }, reportedDepth: 10, selectiveDepth: 14,
    multiPv: 2, status: 'live', createdAt: 1,
    lines: [
      { multipv: 2, evaluation: { kind: 'centipawn', value: 18 }, pv: ['d2d4', 'd7d5'] },
      { multipv: 1, evaluation: { kind: 'centipawn', value: 42 }, pv: ['e2e4', 'e7e5'] },
    ],
    ...overrides,
  };
}

function testLifecyclePresentation(): void {
  assert.equal(deriveAnalysisPresentation({ lifecycle: 'idle', connectionStatus: 'idle', paused: false, snapshot: null }).state, 'idle');
  assert.equal(deriveAnalysisPresentation({ lifecycle: 'active', connectionStatus: 'analyzing', paused: false, snapshot: null }).state, 'starting-analysis');
  assert.equal(deriveAnalysisPresentation({ lifecycle: 'active', connectionStatus: 'analyzing', paused: false, snapshot: snapshot() }).state, 'analyzing');
  assert.equal(deriveAnalysisPresentation({ lifecycle: 'active', connectionStatus: 'idle', paused: true, snapshot: snapshot() }).state, 'stopped');
  assert.equal(deriveAnalysisPresentation({ lifecycle: 'active', connectionStatus: 'idle', paused: false, snapshot: null }).state, 'position-changed');
  assert.equal(deriveAnalysisPresentation({ lifecycle: 'active', connectionStatus: 'disconnected', paused: false, snapshot: null }).state, 'reconnecting');
  assert.equal(deriveAnalysisPresentation({ lifecycle: 'active', connectionStatus: 'error', paused: false, snapshot: null }).state, 'error');
}

function testEvaluationAndPv(): void {
  assert.deepEqual(evaluationText({ kind: 'centipawn', value: -180 }), { score: '-1.80', meaning: 'Black is clearly better', accessible: 'White-perspective evaluation: -1.80. Black is clearly better' });
  assert.match(evaluationText({ kind: 'mate', plies: 6, winner: 'white' }).meaning, /Mate in 3 for White/);
  const evaluation = readFileSync('src/components/analysis/AnalysisEvaluationPanel.tsx', 'utf8');
  const pv = readFileSync('src/components/analysis/PrincipalVariationPanel.tsx', 'utf8');
  assert.match(evaluation, /White perspective/);
  assert.match(evaluation, /<EvaluationDisplay/);
  assert.match(pv, /<PrincipalVariationList/);
  assert.match(pv, /analysisPvLines/);
}

function testSnapshotOwnership(): void {
  const current = snapshot();
  assert.equal(selectCurrentAnalysisSnapshot({ live: current, finalized: null }, FEN), current);
  assert.equal(selectCurrentAnalysisSnapshot({ live: current, finalized: null }, NEXT_FEN), null);
  assert.equal(selectCurrentAnalysisSnapshot({ live: snapshot({ mode: 'training' }), finalized: null }, FEN), null);
  assert.equal(selectCurrentAnalysisSnapshot({ live: snapshot({ purpose: 'training-result-review' }), finalized: null }, FEN), null);
  assert.equal(selectCurrentAnalysisSnapshot({ live: snapshot({ sessionGeneration: undefined }), finalized: null }, FEN), null);

  const stopped = analysisDisplayReducer({ live: current, finalized: null }, { type: 'STOPPED' });
  assert.equal(stopped.live, null);
  assert.equal(stopped.finalized?.status, 'finalized');
  assert.equal(selectCurrentAnalysisSnapshot(stopped, FEN)?.requestId, 7);
  const changed = analysisDisplayReducer(stopped, { type: 'FEN_CHANGED', fen: NEXT_FEN });
  assert.equal(selectCurrentAnalysisSnapshot(changed, NEXT_FEN), null);
}

function testNavigationAndSources(): void {
  const navigation = readFileSync('src/components/analysis/AnalysisNavigationPanel.tsx', 'utf8');
  assert.match(navigation, /Go to starting position/);
  assert.match(navigation, /Go to previous move/);
  assert.match(navigation, /Go to next move/);
  assert.match(navigation, /Go to latest position/);
  assert.match(navigation, /Viewing history/);
  assert.equal(sourceLabel('default'), 'Starting position');
  assert.equal(sourceLabel('fen'), 'FEN position');
  assert.equal(sourceLabel('pgn'), 'PGN game');
  assert.equal(sourceLabel('board'), 'Board position');
}

function testArchitectureAndReliability(): void {
  const view = readFileSync('src/session/SessionControllerView.tsx', 'utf8');
  const controller = readFileSync('src/session/useSessionControllerValue.tsx', 'utf8');
  const sidebar = readFileSync('src/components/analysis/AnalysisSidebar.tsx', 'utf8');
  const ownership = readFileSync('src/lib/engine-result-ownership.ts', 'utf8');
  assert.match(view, /<AnalysisSidebar/);
  assert.doesNotMatch(view, /<EnginePanel|<MoveHistory|<GameControls/);
  assert.doesNotMatch(sidebar, /useEngine\(|useChessGame\(|resultAck|acknowledgeApplied/);
  assert.match(controller, /startResolvedAnalysis\(analysisPositionFen, 'analysis'\)/);
  assert.match(controller, /startAnalysis\(rootFen, \[\]/);
  assert.match(controller, /selectCurrentAnalysisSnapshot/);
  assert.match(controller, /dispatchAnalysisDisplay\(\{ type: 'CLEAR' \}\)/);
  assert.match(ownership, /if \(mode === 'analysis'\) return null/);
  assert.doesNotMatch(sidebar, /Opening book|FEN input|PGN input/);
}

testLifecyclePresentation();
testEvaluationAndPv();
testSnapshotOwnership();
testNavigationAndSources();
testArchitectureAndReliability();
console.log('Analysis sidebar tests passed');
