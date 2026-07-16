import { strict as assert } from 'node:assert';
import { readFileSync } from 'node:fs';
import type { EngineBestMoveResult } from '../types/engine';
import {
  createOpponentMoveApplicationReceipt,
  matchesActiveOpponentResult,
  requiresMoveApplicationAcknowledgment,
  statusAfterBestMove,
} from './engine-result-ownership';

const opponentResult: EngineBestMoveResult = {
  requestId: 41,
  rootFen: 'root-fen',
  positionFen: 'position-fen',
  positionKey: 'position-key',
  sessionId: 'session-a',
  sessionGeneration: 7,
  positionSequence: 3,
  expectedSide: 'b',
  purpose: 'opponent',
  move: 'e7e5',
};

const informationalPurposes = [
  'training-root-review',
  'training-result-review',
  'training-hint',
  'analysis',
] as const;

for (const purpose of informationalPurposes) {
  const result = { ...opponentResult, purpose };
  assert.equal(requiresMoveApplicationAcknowledgment(result), false, `${purpose} must be informational`);
  assert.equal(createOpponentMoveApplicationReceipt(result, 'training'), null, `${purpose} must not create an application receipt`);
  assert.equal(statusAfterBestMove(result), 'idle', `${purpose} completion must release the search slot`);
}

assert.equal(requiresMoveApplicationAcknowledgment(opponentResult), true);
assert.equal(statusAfterBestMove(opponentResult), 'result_ready');
const receipt = createOpponentMoveApplicationReceipt(opponentResult, 'training');
assert.deepEqual(receipt, {
  kind: 'opponent-move',
  ownerMode: 'training',
  requestId: 41,
  positionKey: 'position-key',
  sessionId: 'session-a',
  sessionGeneration: 7,
  positionFen: 'position-fen',
});
assert.ok(receipt);

const active = {
  ownerMode: 'training' as const,
  purpose: 'opponent',
  requestId: 41,
  positionKey: 'position-key',
  sessionId: 'session-a',
  sessionGeneration: 7,
  positionFen: 'position-fen',
};
assert.equal(matchesActiveOpponentResult(active, receipt), true);
assert.equal(matchesActiveOpponentResult({ ...active, requestId: 42 }, receipt), false);
assert.equal(matchesActiveOpponentResult({ ...active, positionKey: 'stale-position' }, receipt), false);
assert.equal(matchesActiveOpponentResult({ ...active, sessionGeneration: 8 }, receipt), false);
assert.equal(matchesActiveOpponentResult({ ...active, sessionId: 'session-b' }, receipt), false);
assert.equal(matchesActiveOpponentResult({ ...active, positionFen: 'other-fen' }, receipt), false);
assert.equal(matchesActiveOpponentResult({ ...active, purpose: 'analysis' }, receipt), false);
assert.equal(matchesActiveOpponentResult({ ...active, ownerMode: 'fair' }, receipt), false);

// Promotion remains an opponent move with the same immutable correlation receipt.
const promotionReceipt = createOpponentMoveApplicationReceipt({ ...opponentResult, move: 'e7e8q' }, 'training');
assert.deepEqual(promotionReceipt, receipt);
assert.equal(createOpponentMoveApplicationReceipt(opponentResult, 'analysis'), null);

// Terminal informational results and null opponent results never claim application.
assert.equal(requiresMoveApplicationAcknowledgment({ purpose: 'analysis', move: null }), false);
assert.equal(requiresMoveApplicationAcknowledgment({ purpose: 'opponent', move: null }), false);

const controller = readFileSync('src/session/useSessionControllerValue.tsx', 'utf8');
const engine = readFileSync('src/hooks/useEngine.ts', 'utf8');
const server = readFileSync('src/lib/engine-session.ts', 'utf8');

assert.match(controller, /if \(moveResult\?\.purpose !== 'opponent'\) return;/);
assert.match(controller, /createOpponentMoveApplicationReceipt\(moveResult, gameMode\)/);
assert.match(controller, /acknowledgeAppliedEngineMove\(\{[\s\S]*receipt: applicationReceipt, applied: true/);
assert.doesNotMatch(controller, /acknowledgeBestMove/);
assert.match(engine, /statusAfterBestMove\(result\)/);
assert.match(engine, /nextStatus === 'idle'[\s\S]*clearActiveRequestIdentity\(\)/);
assert.match(engine, /matchesActiveOpponentResult/);
assert.match(engine, /case 'move-applied':[\s\S]*invalidateActiveRequest\(\)/);
assert.equal((engine.match(/type: 'resultAck'/g) ?? []).length, 1, 'useEngine must have one resultAck sending boundary');
assert.match(server, /if \(activeSearch\.purpose === 'opponent' && move !== null\)[\s\S]*awaitingMoveApplication =/);
assert.match(server, /STALE_APPLICATION_ACK/);
assert.match(controller, /resultRequestId = startResolvedAnalysis\(fen, 'training-result-review'\)/);
assert.match(controller, /bestMoveResult\.requestId !== pendingReview\.resultRequestId/);
assert.match(controller, /startAnalysis\(rootFen, \[\], undefined/);

console.log('engine result acknowledgment ownership tests passed');
