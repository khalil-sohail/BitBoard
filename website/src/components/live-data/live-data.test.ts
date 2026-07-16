import { strict as assert } from 'node:assert';
import { readFileSync } from 'node:fs';
import { Chess, type Move } from 'chess.js';
import { buildFairPlayHistoryEntries } from '../fair-play/fair-play-live-data';
import { buildTrainingHistoryEntries } from '../training/training-live-data';
import { buildAnalysisHistoryEntries } from '../analysis/analysis-live-data';
import { formatEvaluation } from './evaluation-format';
import { groupMoveHistoryEntries } from './history-model';
import { formatPrincipalVariation, storedMoveNotation } from './move-notation';
import { sortPrincipalVariations } from './pv-model';
import { formatCompactCount } from './metrics-format';

const read = (path: string) => readFileSync(path, 'utf8');

function playedMoves(): Move[] {
  const game = new Chess();
  return ['e4', 'd5', 'exd5'].map(san => game.move(san));
}

function testNotation(): void {
  assert.deepEqual(formatPrincipalVariation(new Chess().fen(), ['e2e4', 'e7e5']), { notation: 'SAN', moves: ['e4', 'e5'] });
  assert.deepEqual(formatPrincipalVariation(new Chess().fen(), ['e2e4', 'd7d5', 'e4d5']), { notation: 'SAN', moves: ['e4', 'd5', 'exd5'] });
  assert.deepEqual(formatPrincipalVariation('r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1', ['e1g1']), { notation: 'SAN', moves: ['O-O'] });
  assert.deepEqual(formatPrincipalVariation('7k/P7/8/8/8/8/8/7K w - - 0 1', ['a7a8q']), { notation: 'SAN', moves: ['a8=Q+'] });
  assert.deepEqual(formatPrincipalVariation(new Chess().fen(), ['e2e4', 'e7e5', 'd1h5', 'b8c6', 'f1c4', 'g8f6', 'h5f7']).moves.at(-1), 'Qxf7#');
  assert.deepEqual(formatPrincipalVariation(new Chess().fen(), ['e2e4', 'invalid']), { notation: 'UCI', moves: ['e2e4', 'invalid'] });
  assert.equal(storedMoveNotation(playedMoves()[0]), 'e4');
  assert.equal(storedMoveNotation({ from: 'a7', to: 'a8', promotion: 'q' }), 'a7a8q');
}

function testHistoryAndAdapters(): void {
  const moves = playedMoves();
  const fair = buildFairPlayHistoryEntries(moves);
  assert.equal(fair[0].notation, 'e4');
  assert.equal(fair.some(entry => entry.markers !== undefined), false);
  assert.equal(fair.some(entry => entry.selectable), false);

  const training = buildTrainingHistoryEntries(moves, [{ moveIndex: 0, grade: 'Good', delta: -25, hintLevelUsed: 2 }]);
  assert.deepEqual(training[0].markers?.map(marker => marker.text), ['Good', 'Hint 2']);
  assert.equal(training[1].markers, undefined, 'unreviewed engine moves remain ungraded');

  const analysis = buildAnalysisHistoryEntries(moves, 2);
  assert.equal(analysis.every(entry => entry.selectable), true);
  assert.equal(analysis[1].selected, true);
  assert.equal(analysis[0].selected, false);

  const rows = groupMoveHistoryEntries(fair);
  assert.equal(rows.length, 2);
  assert.equal(rows[0].white?.notation, 'e4');
  assert.equal(rows[0].black?.notation, 'd5');
  assert.equal(rows[1].white?.notation, 'exd5');
}

function testEvaluationPvAndMetrics(): void {
  assert.equal(formatEvaluation({ kind: 'centipawn', value: 42 }).score, '+0.42');
  assert.equal(formatEvaluation({ kind: 'centipawn', value: -180 }).meaning, 'Black is clearly better');
  assert.equal(formatEvaluation({ kind: 'mate', plies: 6, winner: 'white' }).meaning, 'Mate in 3 for White');
  assert.equal(formatEvaluation(null).score, '—');
  assert.deepEqual(sortPrincipalVariations([
    { rank: 3, evaluation: null, moves: [] }, { rank: 1, evaluation: null, moves: [] }, { rank: 2, evaluation: null, moves: [] },
  ]).map(line => line.rank), [1, 2, 3]);
  assert.equal(formatCompactCount(1_250), '1.3k');
  assert.equal(formatCompactCount(2_000_000), '2.00M');
}

function testSharedArchitectureAndSemantics(): void {
  const section = read('src/components/live-data/LiveDataSection.tsx');
  const history = read('src/components/live-data/MoveHistoryTable.tsx');
  const pv = read('src/components/live-data/PrincipalVariationList.tsx');
  const status = read('src/components/live-data/OperationalStatus.tsx');
  const actions = read('src/components/live-data/ContextualActions.tsx');
  const styles = read('src/components/live-data/LiveData.module.css');
  const shared = [section, history, pv, status, actions, read('src/components/live-data/EvaluationDisplay.tsx'), read('src/components/live-data/EngineMetrics.tsx')].join('\n');
  assert.match(section, /<section[\s\S]*aria-labelledby/);
  assert.match(section, /<details[\s\S]*<summary/);
  assert.match(history, /followLatestRef/);
  assert.match(history, /aria-current=\{current\}/);
  assert.match(history, /scrollIntoView/);
  assert.match(pv, /<ol/);
  assert.match(status, /role=\{error \? 'alert' : 'status'\}/);
  assert.match(actions, /data-variant=\{action\.variant/);
  assert.match(styles, /overflow-wrap: anywhere/);
  assert.doesNotMatch(shared, /useSessionController\(|useEngine\(|useChessGame\(|resultAck|OpponentMoveApplicationReceipt/);
}

function testModeHierarchy(): void {
  const fair = read('src/components/fair-play/FairPlaySidebar.tsx');
  const training = read('src/components/training/TrainingSidebar.tsx');
  const analysis = read('src/components/analysis/AnalysisSidebar.tsx');
  assert.ok(fair.indexOf('<FairPlayClockPanel') < fair.indexOf('<FairPlayHistoryPanel'));
  assert.ok(fair.indexOf('<FairPlayHistoryPanel') < fair.indexOf('<FairPlayOperationalStatus'));
  assert.doesNotMatch(fair, /EvaluationDisplay|PrincipalVariationList|EngineMetrics/);
  assert.ok(training.indexOf('<TrainingTaskPanel') < training.indexOf('<TrainingFeedbackPanel'));
  assert.ok(training.indexOf('<TrainingFeedbackPanel') < training.indexOf('<TrainingAnalysisPanel'));
  assert.ok(training.indexOf('<TrainingAnalysisPanel') < training.indexOf('<TrainingHistoryPanel'));
  assert.ok(analysis.indexOf('<AnalysisEvaluationPanel') < analysis.indexOf('<PrincipalVariationPanel'));
  assert.ok(analysis.indexOf('<PrincipalVariationPanel') < analysis.indexOf('<AnalysisNavigationPanel'));
  assert.ok(analysis.indexOf('<AnalysisNavigationPanel') < analysis.indexOf('<AnalysisHistoryPanel'));
}

testNotation();
testHistoryAndAdapters();
testEvaluationPvAndMetrics();
testSharedArchitectureAndSemantics();
testModeHierarchy();
console.log('shared live-data tests passed');
