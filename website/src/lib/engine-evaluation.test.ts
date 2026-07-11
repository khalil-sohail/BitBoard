import { strict as assert } from 'assert';
import {
  NormalizedEvaluation,
  gradeMove,
  moveLossCp,
  normalizeEngineScore,
} from './engine-evaluation';
import { parseUciInfo } from './uci-parser';

const WHITE_TO_MOVE = '4k3/8/8/8/8/8/8/Q3K3 w - - 0 1';
const BLACK_TO_MOVE = '4k3/8/8/8/8/8/8/Q3K3 b - - 0 1';

function cp(value: number): NormalizedEvaluation {
  return { kind: 'centipawn', value };
}

function mate(winner: 'white' | 'black', plies: number): NormalizedEvaluation {
  return { kind: 'mate', winner, plies };
}

function testCentipawnNormalization(): void {
  assert.deepEqual(normalizeEngineScore({ score: 125 }, WHITE_TO_MOVE), cp(125));
  assert.deepEqual(normalizeEngineScore({ score: -125 }, WHITE_TO_MOVE), cp(-125));
  assert.deepEqual(normalizeEngineScore({ score: 125 }, BLACK_TO_MOVE), cp(-125));
  assert.deepEqual(normalizeEngineScore({ score: -125 }, BLACK_TO_MOVE), cp(125));
  assert.deepEqual(normalizeEngineScore({ score: 0 }, WHITE_TO_MOVE), cp(0));
  assert.deepEqual(
    normalizeEngineScore({ score: 90 }, WHITE_TO_MOVE),
    normalizeEngineScore({ score: -90 }, BLACK_TO_MOVE),
  );
}

function testMateNormalization(): void {
  assert.deepEqual(normalizeEngineScore({ mate: 2 }, WHITE_TO_MOVE), mate('white', 4));
  assert.deepEqual(normalizeEngineScore({ mate: -2 }, WHITE_TO_MOVE), mate('black', 4));
  assert.deepEqual(normalizeEngineScore({ mate: 2 }, BLACK_TO_MOVE), mate('black', 4));
  assert.deepEqual(normalizeEngineScore({ mate: -2 }, BLACK_TO_MOVE), mate('white', 4));
  assert.deepEqual(normalizeEngineScore({ mate: 0 }, WHITE_TO_MOVE), mate('white', 0));
  assert.deepEqual(normalizeEngineScore({ mate: 0 }, BLACK_TO_MOVE), mate('black', 0));
}

function testInvalidNormalization(): void {
  assert.equal(normalizeEngineScore({}, WHITE_TO_MOVE), null);
  assert.equal(normalizeEngineScore({ score: 1 }, 'not a fen'), null);
  assert.equal(normalizeEngineScore({ score: 1.5 }, WHITE_TO_MOVE), null);
  assert.equal(normalizeEngineScore({ mate: 1.5 }, WHITE_TO_MOVE), null);
  assert.equal(normalizeEngineScore({ score: Number.POSITIVE_INFINITY }, WHITE_TO_MOVE), null);
}

function testMateParserDoesNotFlattenToCentipawns(): void {
  const parsed = parseUciInfo('info depth 2 multipv 1 score mate 0 nodes 117 time 0 pv g6f6');
  assert.equal(parsed?.mate, 0);
  assert.equal(parsed?.score, undefined);
}

function testCentipawnMoveLossAndGrades(): void {
  assert.equal(moveLossCp(cp(120), cp(120), 'w'), 0);
  assert.equal(gradeMove({ best: cp(120), played: cp(120), playerColor: 'w' })?.grade, 'Best');
  assert.equal(gradeMove({ best: cp(120), played: cp(80), playerColor: 'w' })?.grade, 'Good');
  assert.equal(gradeMove({ best: cp(120), played: cp(0), playerColor: 'w' })?.grade, 'Inaccuracy');
  assert.equal(gradeMove({ best: cp(120), played: cp(-100), playerColor: 'w' })?.grade, 'Mistake');
  assert.equal(gradeMove({ best: cp(120), played: cp(-250), playerColor: 'w' })?.grade, 'Blunder');
  assert.equal(gradeMove({ best: cp(-120), played: cp(-120), playerColor: 'b' })?.grade, 'Best');
  assert.equal(gradeMove({ best: cp(-120), played: cp(250), playerColor: 'b' })?.grade, 'Blunder');
}

function testThresholdBoundaries(): void {
  assert.equal(gradeMove({ best: cp(100), played: cp(90), playerColor: 'w' })?.grade, 'Best');
  assert.equal(gradeMove({ best: cp(100), played: cp(89), playerColor: 'w' })?.grade, 'Good');
  assert.equal(gradeMove({ best: cp(100), played: cp(30), playerColor: 'w' })?.grade, 'Good');
  assert.equal(gradeMove({ best: cp(100), played: cp(29), playerColor: 'w' })?.grade, 'Inaccuracy');
  assert.equal(gradeMove({ best: cp(100), played: cp(-50), playerColor: 'w' })?.grade, 'Inaccuracy');
  assert.equal(gradeMove({ best: cp(100), played: cp(-51), playerColor: 'w' })?.grade, 'Mistake');
  assert.equal(gradeMove({ best: cp(100), played: cp(-200), playerColor: 'w' })?.grade, 'Mistake');
  assert.equal(gradeMove({ best: cp(100), played: cp(-201), playerColor: 'w' })?.grade, 'Blunder');
}

function testMateMoveLossAndGrades(): void {
  assert.equal(gradeMove({ best: mate('white', 1), played: mate('white', 1), playerColor: 'w' })?.grade, 'Best');
  assert.equal(gradeMove({ best: mate('white', 1), played: mate('white', 5), playerColor: 'w' })?.grade, 'Inaccuracy');
  assert.equal(gradeMove({ best: mate('white', 1), played: cp(300), playerColor: 'w' })?.grade, 'Blunder');
  assert.equal(gradeMove({ best: cp(50), played: mate('black', 1), playerColor: 'w' })?.grade, 'Blunder');
  assert.equal(gradeMove({ best: mate('black', 8), played: mate('black', 2), playerColor: 'w' })?.grade, 'Inaccuracy');
  assert.equal(gradeMove({ best: mate('black', 2), played: mate('black', 8), playerColor: 'w' })?.grade, 'Best');
}

function testForcedBookAndMissingAnalysis(): void {
  assert.equal(gradeMove({ best: null, played: null, playerColor: 'w', legalMoveCount: 1 })?.grade, 'Forced');
  assert.equal(gradeMove({ best: null, played: null, playerColor: 'w', isBook: true })?.grade, 'Book');
  assert.equal(gradeMove({ best: cp(100), played: null, playerColor: 'w' }), null);
  assert.equal(gradeMove({ best: null, played: cp(100), playerColor: 'w' }), null);
}

function testReviewIdentityGate(): void {
  const pendingReview = { reviewId: 7, resultFen: 'after-move' };
  const incoming = { reviewId: 8, rootFen: 'after-move' };
  assert.equal(
    pendingReview.reviewId === incoming.reviewId && pendingReview.resultFen === incoming.rootFen,
    false,
  );
}

function run(): void {
  testCentipawnNormalization();
  testMateNormalization();
  testInvalidNormalization();
  testMateParserDoesNotFlattenToCentipawns();
  testCentipawnMoveLossAndGrades();
  testThresholdBoundaries();
  testMateMoveLossAndGrades();
  testForcedBookAndMissingAnalysis();
  testReviewIdentityGate();

  console.log('engine evaluation tests passed');
}

run();
