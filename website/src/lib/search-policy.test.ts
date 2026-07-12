import { strict as assert } from 'assert';
import { resolveSearchPolicy, TRAINING_HINT_DEPTH_CAP, TRAINING_REVIEW_DEPTH_CAP } from './search-policy';

function testTrainingOpponentUsesProfileAndOptionalPonder(): void {
  const standard = resolveSearchPolicy({
    mode: 'training',
    purpose: 'opponent',
    difficulty: 'standard',
    userMaxDepth: 2,
    multiPv: 3,
    trainingPonderEnabled: false,
  });
  assert.deepEqual(standard.limit, { mode: 'movetime', movetimeMs: 3000 });
  assert.equal(standard.multiPv, 1);
  assert.equal(standard.ponder, false);
  assert.equal(standard.source, 'training-opponent-profile');

  const deep = resolveSearchPolicy({
    mode: 'training',
    purpose: 'opponent',
    difficulty: 'deep',
    userMaxDepth: 6,
    multiPv: 3,
    trainingPonderEnabled: true,
  });
  assert.deepEqual(deep.limit, { mode: 'depth', depth: 6 });
  assert.equal(deep.ponder, true);
}

function testTrainingReviewAndHintsAreCappedAndNeverPonder(): void {
  const root = resolveSearchPolicy({
    mode: 'training',
    purpose: 'training-root-review',
    difficulty: 'blitz',
    userMaxDepth: 30,
    multiPv: 3,
    trainingPonderEnabled: true,
  });
  assert.deepEqual(root.limit, { mode: 'depth', depth: TRAINING_REVIEW_DEPTH_CAP });
  assert.equal(root.multiPv, 3);
  assert.equal(root.ponder, false);

  const result = resolveSearchPolicy({
    mode: 'training',
    purpose: 'training-result-review',
    difficulty: 'deep',
    userMaxDepth: 4,
    multiPv: 2,
    trainingPonderEnabled: true,
  });
  assert.deepEqual(result.limit, { mode: 'depth', depth: 4 });
  assert.equal(result.ponder, false);

  const hint = resolveSearchPolicy({
    mode: 'training',
    purpose: 'training-hint',
    difficulty: 'standard',
    userMaxDepth: 30,
    multiPv: 8,
    trainingPonderEnabled: true,
  });
  assert.deepEqual(hint.limit, { mode: 'depth', depth: TRAINING_HINT_DEPTH_CAP });
  assert.equal(hint.multiPv, 1);
  assert.equal(hint.ponder, false);
}

function testAnalysisUsesUserDepth(): void {
  const policy = resolveSearchPolicy({
    mode: 'analysis',
    purpose: 'analysis',
    difficulty: 'standard',
    userMaxDepth: 7,
    multiPv: 3,
    trainingPonderEnabled: true,
  });
  assert.deepEqual(policy.limit, { mode: 'depth', depth: 7 });
  assert.equal(policy.ponder, false);
  assert.equal(policy.source, 'user-max-depth');
}

function testFairClockAndFixedDepthSemantics(): void {
  const clocked = resolveSearchPolicy({
    mode: 'fair',
    purpose: 'opponent',
    difficulty: 'blitz',
    userMaxDepth: 2,
    multiPv: 3,
    trainingPonderEnabled: true,
    fairPonderEnabled: true,
    clock: { wtime: 60000, btime: 59000, winc: 1000, binc: 1000 },
  });
  assert.deepEqual(clocked.limit, {
    mode: 'clock',
    wtime: 60000,
    btime: 59000,
    winc: 1000,
    binc: 1000,
  });
  assert.equal(clocked.ponder, true);
  assert.equal(clocked.source, 'clock');

  const fixed = resolveSearchPolicy({
    mode: 'fair',
    purpose: 'opponent',
    difficulty: 'deep',
    userMaxDepth: 2,
    multiPv: 3,
    trainingPonderEnabled: false,
    fairPonderEnabled: true,
  });
  assert.deepEqual(fixed.limit, { mode: 'depth', depth: 8 });
  assert.equal(fixed.ponder, true);
}

function run(): void {
  testTrainingOpponentUsesProfileAndOptionalPonder();
  testTrainingReviewAndHintsAreCappedAndNeverPonder();
  testAnalysisUsesUserDepth();
  testFairClockAndFixedDepthSemantics();

  console.log('search policy tests passed');
}

run();

