import { strict as assert } from 'assert';
import {
  ENGINE_DIFFICULTIES,
  getDifficultyProfile,
  isEngineDifficulty,
} from './engine-difficulty';

function testSupportedValues(): void {
  assert.deepEqual([...ENGINE_DIFFICULTIES], ['blitz', 'standard', 'deep']);
  assert.equal(isEngineDifficulty('blitz'), true);
  assert.equal(isEngineDifficulty('standard'), true);
  assert.equal(isEngineDifficulty('deep'), true);
  assert.equal(isEngineDifficulty('easy'), false);
  assert.equal(isEngineDifficulty(undefined), false);
}

function testOpponentProfilesDiffer(): void {
  assert.deepEqual(getDifficultyProfile('blitz', 'opponent'), {
    movetimeMs: 1000,
    multiPv: 1,
    openingSelection: { mode: 'weighted' },
  });
  assert.deepEqual(getDifficultyProfile('standard', 'opponent'), {
    movetimeMs: 3000,
    multiPv: 1,
    openingSelection: { mode: 'top-n-weighted', maxCandidates: 4 },
  });
  assert.deepEqual(getDifficultyProfile('deep', 'opponent'), {
    depth: 8,
    multiPv: 1,
    openingSelection: { mode: 'best' },
  });
}

function testReviewProfilesIgnoreDifficulty(): void {
  const root = getDifficultyProfile('blitz', 'training-root-review', { depth: 10, multiPv: 3 });
  const result = getDifficultyProfile('deep', 'training-result-review', { depth: 10, multiPv: 3 });
  const hint = getDifficultyProfile('blitz', 'training-hint', { depth: 10, multiPv: 1 });
  assert.deepEqual(root, { depth: 10, multiPv: 3 });
  assert.deepEqual(result, { depth: 10, multiPv: 3 });
  assert.deepEqual(hint, { depth: 10, multiPv: 1 });
}

function testAnalysisProfilePreservesExplicitInputs(): void {
  assert.deepEqual(getDifficultyProfile('standard', 'analysis', { depth: 7, multiPv: 2 }), {
    depth: 7,
    multiPv: 2,
  });
  assert.deepEqual(getDifficultyProfile('standard', 'analysis'), {
    depth: 30,
    multiPv: 3,
  });
}

function testProfilesDoNotMutate(): void {
  const blitz = getDifficultyProfile('blitz', 'opponent');
  const again = getDifficultyProfile('blitz', 'opponent');
  assert.deepEqual(blitz, again);
  assert.throws(() => {
    (blitz as { multiPv: number }).multiPv = 2;
  }, TypeError);
}

function run(): void {
  testSupportedValues();
  testOpponentProfilesDiffer();
  testReviewProfilesIgnoreDifficulty();
  testAnalysisProfilePreservesExplicitInputs();
  testProfilesDoNotMutate();

  console.log('engine difficulty tests passed');
}

run();
