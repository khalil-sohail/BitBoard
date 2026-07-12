import { strict as assert } from 'assert';
import { Chess } from 'chess.js';
import {
  buildPromotionMove,
  isPromotionCandidate,
  isPromotionPiece,
} from './promotion';

const WHITE_PROMOTION_FEN = 'k7/4P3/8/8/8/8/8/7K w - - 0 1';
const BLACK_PROMOTION_FEN = '7k/8/8/8/8/8/4p3/K7 b - - 0 1';

function testWhitePromotionCandidates(): void {
  assert.deepEqual(isPromotionCandidate(WHITE_PROMOTION_FEN, 'e7', 'e8'), {
    from: 'e7',
    to: 'e8',
    color: 'w',
  });
  assert.equal(isPromotionCandidate(WHITE_PROMOTION_FEN, 'e7', 'e6'), null);
}

function testBlackPromotionCandidates(): void {
  assert.deepEqual(isPromotionCandidate(BLACK_PROMOTION_FEN, 'e2', 'e1'), {
    from: 'e2',
    to: 'e1',
    color: 'b',
  });
  assert.equal(isPromotionCandidate(BLACK_PROMOTION_FEN, 'e2', 'e3'), null);
}

function testPromotionPieces(): void {
  assert.equal(isPromotionPiece('q'), true);
  assert.equal(isPromotionPiece('r'), true);
  assert.equal(isPromotionPiece('b'), true);
  assert.equal(isPromotionPiece('n'), true);
  assert.equal(isPromotionPiece('p'), false);
  assert.equal(isPromotionPiece('Q'), false);
}

function testGeneratedUciSuffixesAreAccepted(): void {
  const whitePending = isPromotionCandidate(WHITE_PROMOTION_FEN, 'e7', 'e8');
  assert.ok(whitePending);
  for (const piece of ['q', 'r', 'b', 'n'] as const) {
    const move = buildPromotionMove(whitePending, piece);
    const chess = new Chess(WHITE_PROMOTION_FEN);
    const result = chess.move(move);
    assert.equal(`${result.from}${result.to}${result.promotion}`, `e7e8${piece}`);
  }

  const blackPending = isPromotionCandidate(BLACK_PROMOTION_FEN, 'e2', 'e1');
  assert.ok(blackPending);
  for (const piece of ['q', 'r', 'b', 'n'] as const) {
    const move = buildPromotionMove(blackPending, piece);
    const chess = new Chess(BLACK_PROMOTION_FEN);
    const result = chess.move(move);
    assert.equal(`${result.from}${result.to}${result.promotion}`, `e2e1${piece}`);
  }
}

function run(): void {
  testWhitePromotionCandidates();
  testBlackPromotionCandidates();
  testPromotionPieces();
  testGeneratedUciSuffixesAreAccepted();

  console.log('promotion tests passed');
}

run();
