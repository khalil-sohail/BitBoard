import { strict as assert } from 'assert';
import { Chess } from 'chess.js';
import {
  SamePositionRetryGuard,
  createPositionIdentity,
  resolveExactPosition,
  validateEngineMove,
} from './fair-play-engine';

const REPORTED_FEN = 'r2r2k1/p1R2pp1/bp2p3/6N1/3P4/P3P3/1n3PPP/5RK1 w - - 2 20';

function testReportedFailure(): void {
  const board = new Chess(REPORTED_FEN);
  assert.equal(board.turn(), 'w');
  assert.deepEqual(board.get('b6'), { type: 'p', color: 'b' });
  assert.deepEqual(validateEngineMove(REPORTED_FEN, 'b6c5'), { ok: false, reason: 'wrong_side_piece' });

  const legal = validateEngineMove(REPORTED_FEN, 'c7f7');
  assert.equal(legal.ok, true);
  if (legal.ok) {
    assert.equal(new Chess(legal.newFen).turn(), 'b');
  }
}

function testMoveValidation(): void {
  assert.deepEqual(validateEngineMove(REPORTED_FEN, 'not-a-move'), { ok: false, reason: 'invalid_uci_move' });
  assert.deepEqual(validateEngineMove(REPORTED_FEN, 'c7d8'), { ok: false, reason: 'illegal_move_for_position' });

  const promotion = validateEngineMove('7k/P7/8/8/8/8/8/7K w - - 0 1', 'a7a8q');
  assert.equal(promotion.ok, true);
  const castling = validateEngineMove('r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1', 'e1g1');
  assert.equal(castling.ok, true);
  const enPassant = validateEngineMove('7k/8/8/3pP3/8/8/8/7K w - d6 0 2', 'e5d6');
  assert.equal(enPassant.ok, true);

  const mate = validateEngineMove('7k/6Q1/6K1/8/8/8/8/8 b - - 0 1', '0000');
  assert.equal(mate.ok, true);
  assert.deepEqual(validateEngineMove(REPORTED_FEN, '0000'), { ok: false, reason: 'terminal_position' });
}

function testExactPositionAndIdentity(): void {
  const afterE4 = resolveExactPosition('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1', ['e2e4']);
  assert.equal(afterE4?.fen(), 'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1');
  // A current FEN and a full-game move list are two alternative position
  // descriptions, not inputs that may be composed.
  assert.equal(resolveExactPosition(afterE4!.fen(), ['e2e4']), null);

  const base = createPositionIdentity({ requestId: 54, sessionId: 's', sessionGeneration: 1, positionSequence: 7, fen: REPORTED_FEN });
  assert.equal(base.expectedSide, 'w');
  assert.deepEqual(base, createPositionIdentity({ requestId: 54, sessionId: 's', sessionGeneration: 1, positionSequence: 7, fen: REPORTED_FEN }));
  assert.notEqual(base.key, createPositionIdentity({ requestId: 55, sessionId: 's', sessionGeneration: 1, positionSequence: 8, fen: REPORTED_FEN }).key);
  assert.notEqual(base.key, createPositionIdentity({ requestId: 54, sessionId: 's', sessionGeneration: 2, positionSequence: 7, fen: REPORTED_FEN }).key);
}

function testRetryGuard(): void {
  const guard = new SamePositionRetryGuard();
  assert.equal(guard.canStart('position-a'), true);
  assert.deepEqual(guard.recordFailure('position-a'), { retryAllowed: true, retryCount: 1 });
  assert.equal(guard.canStart('position-a'), true);
  assert.deepEqual(guard.recordFailure('position-a'), { retryAllowed: false, retryCount: 2 });
  assert.equal(guard.canStart('position-a'), false);

  const completed = new SamePositionRetryGuard();
  completed.recordValidated('position-b');
  assert.equal(completed.canStart('position-b'), false);
}

testReportedFailure();
testMoveValidation();
testExactPositionAndIdentity();
testRetryGuard();
console.log('fair-play engine validation tests passed');
