import { strict as assert } from 'assert';
import {
  advanceHintLevel,
  buildProgressiveHintView,
  deriveHintMove,
  hintLevelUsedForMove,
  reusableRootHintMove,
  shouldClearHintForFen,
} from './training-hint';
import type { TrainingState } from './training-machine';

const START = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';

function requireMove(fen: string, uci: string) {
  const move = deriveHintMove(fen, uci);
  assert.ok(move, `${uci} should be legal`);
  return move;
}

function testProgressiveViews(): void {
  assert.equal(buildProgressiveHintView(requireMove(START, 'e2e4'), 0), null);
  const move = requireMove(START, 'g1f3');
  assert.equal(buildProgressiveHintView(move, 1)?.from, 'g1');
  assert.equal(buildProgressiveHintView(move, 1)?.to, undefined);
  assert.equal(buildProgressiveHintView(move, 2)?.to, 'f3');
  assert.equal(buildProgressiveHintView(move, 3)?.san, 'Nf3');
  assert.equal(advanceHintLevel(0), 1);
  assert.equal(advanceHintLevel(1), 2);
  assert.equal(advanceHintLevel(2), 3);
  assert.equal(advanceHintLevel(3), 3);
}

function testSpecialMoves(): void {
  const promotion = requireMove('4k3/6P1/8/8/8/8/8/4K3 w - - 0 1', 'g7g8n');
  assert.equal(promotion.promotion, 'n');
  assert.equal(promotion.san, 'g8=N');

  const castle = requireMove('r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1', 'e1g1');
  assert.equal(castle.san, 'O-O');
  assert.equal(castle.isCastling, true);

  const ep = requireMove('8/8/8/3pP3/8/8/8/4K2k w - d6 0 1', 'e5d6');
  assert.equal(ep.isEnPassant, true);
  assert.equal(ep.isCapture, true);
}

function testInvalidTerminalAndForced(): void {
  assert.equal(deriveHintMove(START, 'e2e5'), null);
  assert.equal(deriveHintMove('7k/5Q2/7K/8/8/8/8/8 b - - 0 1', 'h8g8'), null);

  const forced = requireMove('7k/8/6K1/8/8/8/8/5R2 b - - 0 1', 'h8g8');
  assert.equal(forced.isForced, true);
  assert.equal(buildProgressiveHintView(forced, 1)?.text, 'Only one legal move is available.');
}

function testFenAndMetadata(): void {
  const state: TrainingState = {
    status: 'waiting-player',
    playerColor: 'w',
    hint: { fen: START, level: 3, status: 'available', move: 'e2e4' },
  };
  assert.equal(hintLevelUsedForMove({ status: 'waiting-player', playerColor: 'w' }, START), 0);
  assert.equal(hintLevelUsedForMove(state, START), 3);
  assert.equal(hintLevelUsedForMove(state, 'other'), 0);
  assert.equal(shouldClearHintForFen(state, 'other'), true);
}

function testReusableRootAnalysis(): void {
  assert.equal(reusableRootHintMove(null, START), null);
  assert.equal(reusableRootHintMove({
    requestId: 1,
    purpose: 'analysis',
    rootFen: START,
    depth: 1,
    pvs: [{ multipv: 1, pv: ['e2e4'] }],
  }, START), null);
  assert.equal(reusableRootHintMove({
    requestId: 2,
    purpose: 'training-root-review',
    rootFen: START,
    depth: 1,
    pvs: [{ multipv: 1, pv: ['e2e4'] }],
  }, START), 'e2e4');
  assert.equal(reusableRootHintMove({
    requestId: 3,
    purpose: 'training-hint',
    rootFen: START,
    depth: 1,
    pvs: [{ multipv: 1, pv: ['d2d4'] }],
  }, 'other'), null);
}

function run(): void {
  testProgressiveViews();
  testSpecialMoves();
  testInvalidTerminalAndForced();
  testFenAndMetadata();
  testReusableRootAnalysis();
  console.log('training hint tests passed');
}

run();
