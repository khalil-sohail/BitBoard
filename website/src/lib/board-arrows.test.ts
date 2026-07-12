import { strict as assert } from 'assert';
import type { EngineInfo } from '../types/engine';
import type { TrainingState } from './training-machine';
import { composeBoardArrows, toChessboardArrows } from './board-arrows';

const START_FEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';

function info(overrides: Partial<EngineInfo> = {}): EngineInfo {
  return {
    requestId: 7,
    rootFen: START_FEN,
    purpose: 'analysis',
    depth: 8,
    pvs: [
      { multipv: 1, pv: ['e2e4'], score: 20 },
      { multipv: 2, pv: ['d2d4'], score: 10 },
      { multipv: 3, pv: ['g1f3'], score: 5 },
    ],
    ...overrides,
  };
}

const waitingPlayer: TrainingState = { status: 'waiting-player', playerColor: 'w' };
const waitingEngine: TrainingState = { status: 'waiting-engine-move', playerColor: 'w', requestId: 8 };

function testDuplicateAnalysisArrowDeduped(): void {
  const arrows = composeBoardArrows({
    mode: 'analysis',
    currentFen: START_FEN,
    engineInfo: info({
      pvs: [
        { multipv: 1, pv: ['e2e4'] },
        { multipv: 1, pv: ['e2e4'] },
      ],
    }),
  });

  assert.equal(arrows.length, 1);
  assert.equal(arrows[0].key, 'analysis-7-1-e2-e4');
}

function testDuplicatePvFirstMovesCollapseForBoard(): void {
  const arrows = composeBoardArrows({
    mode: 'analysis',
    currentFen: START_FEN,
    engineInfo: info({
      pvs: [
        { multipv: 1, pv: ['g5h4'] },
        { multipv: 2, pv: ['g5h4'] },
      ],
    }),
  });

  assert.equal(arrows.length, 1);
  assert.equal(toChessboardArrows(arrows).length, 1);
}

function testHintAndAnalysisUseStableSemanticIdentity(): void {
  const arrows = composeBoardArrows({
    mode: 'analysis',
    currentFen: START_FEN,
    engineInfo: info({ pvs: [{ multipv: 1, pv: ['e2e4'] }] }),
    hintView: { level: 2, text: '', from: 'e2', to: 'e4', isForced: false },
  });

  assert.equal(arrows.length, 1);
  assert.equal(arrows[0].kind, 'hint');
  assert.equal(arrows[0].key, 'hint-static-0-e2-e4');
}

function testOldRequestAndFenRemoved(): void {
  assert.equal(composeBoardArrows({
    mode: 'analysis',
    currentFen: START_FEN,
    engineInfo: info({ rootFen: '8/8/8/8/8/8/8/8 w - - 0 1' }),
  }).length, 0);
}

function testTrainingOpponentLiveArrowsHidden(): void {
  const arrows = composeBoardArrows({
    mode: 'training',
    trainingState: waitingEngine,
    currentFen: START_FEN,
    engineInfo: info({ purpose: 'opponent' }),
  });

  assert.equal(arrows.length, 0);
}

function testTrainingRootReviewAllowedOnlyForWaitingPlayer(): void {
  const allowed = composeBoardArrows({
    mode: 'training',
    trainingState: waitingPlayer,
    currentFen: START_FEN,
    engineInfo: info({ purpose: 'training-root-review' }),
  });
  assert.equal(allowed.length, 3);

  const hidden = composeBoardArrows({
    mode: 'training',
    trainingState: { status: 'reviewing-player-move', playerColor: 'w', reviewId: 1 },
    currentFen: START_FEN,
    engineInfo: info({ purpose: 'training-result-review' }),
  });
  assert.equal(hidden.length, 0);
}

function testAnalysisModeDisplaysMultiPvAndStableKeys(): void {
  const first = composeBoardArrows({ mode: 'analysis', currentFen: START_FEN, engineInfo: info() });
  const second = composeBoardArrows({ mode: 'analysis', currentFen: START_FEN, engineInfo: info() });
  assert.deepEqual(first.map(a => a.key), [
    'analysis-7-1-e2-e4',
    'analysis-7-2-d2-d4',
    'analysis-7-3-g1-f3',
  ]);
  assert.deepEqual(second.map(a => a.key), first.map(a => a.key));
}

function testTerminalAndResetStatesShowNoLiveArrows(): void {
  for (const trainingState of [
    { status: 'game-over', result: { reason: 'draw' } },
    { status: 'resetting', reason: 'manual', playerColor: 'w' },
  ] as TrainingState[]) {
    const arrows = composeBoardArrows({
      mode: 'training',
      trainingState,
      currentFen: START_FEN,
      engineInfo: info({ purpose: 'training-root-review' }),
    });
    assert.equal(arrows.length, 0);
  }
}

function run(): void {
  testDuplicateAnalysisArrowDeduped();
  testDuplicatePvFirstMovesCollapseForBoard();
  testHintAndAnalysisUseStableSemanticIdentity();
  testOldRequestAndFenRemoved();
  testTrainingOpponentLiveArrowsHidden();
  testTrainingRootReviewAllowedOnlyForWaitingPlayer();
  testAnalysisModeDisplaysMultiPvAndStableKeys();
  testTerminalAndResetStatesShowNoLiveArrows();

  console.log('board arrow tests passed');
}

run();
