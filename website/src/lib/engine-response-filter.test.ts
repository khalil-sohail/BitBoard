import { strict as assert } from 'assert';
import { shouldAcceptSearchResponse, RequestState } from './engine-response-filter';

function testMatchingRequestAccepted(): void {
  const state: RequestState = { activeRequestId: 1 };
  assert.equal(shouldAcceptSearchResponse(state, 1), true);
}

function testStaleRequestIgnored(): void {
  const state: RequestState = { activeRequestId: 2 };
  assert.equal(shouldAcceptSearchResponse(state, 1), false);
}

function testMissingOrInvalidRequestIgnored(): void {
  const state: RequestState = { activeRequestId: 2 };
  assert.equal(shouldAcceptSearchResponse(state, undefined), false);
  assert.equal(shouldAcceptSearchResponse(state, null), false);
  assert.equal(shouldAcceptSearchResponse(state, '2'), false);
  assert.equal(shouldAcceptSearchResponse(state, 2.5), false);
}

function testResetInvalidatesRequest(): void {
  const state: RequestState = { activeRequestId: 3 };
  state.activeRequestId = null;
  assert.equal(shouldAcceptSearchResponse(state, 3), false);
}

function testModeSwitchInvalidatesRequest(): void {
  const state: RequestState = { activeRequestId: 4 };
  state.activeRequestId = null;
  assert.equal(shouldAcceptSearchResponse(state, 4), false);
}

function testNewerSearchInvalidatesOlderRequest(): void {
  const state: RequestState = { activeRequestId: 5 };
  state.activeRequestId = 6;
  assert.equal(shouldAcceptSearchResponse(state, 5), false);
  assert.equal(shouldAcceptSearchResponse(state, 6), true);
}

function testDisconnectInvalidatesRequest(): void {
  const state: RequestState = { activeRequestId: 7 };
  state.activeRequestId = null;
  assert.equal(shouldAcceptSearchResponse(state, 7), false);
}

function testValidResponseAfterStaleResponseStillWorks(): void {
  const state: RequestState = { activeRequestId: 9 };
  assert.equal(shouldAcceptSearchResponse(state, 8), false);
  assert.equal(shouldAcceptSearchResponse(state, 9), true);
}

function run(): void {
  testMatchingRequestAccepted();
  testStaleRequestIgnored();
  testMissingOrInvalidRequestIgnored();
  testResetInvalidatesRequest();
  testModeSwitchInvalidatesRequest();
  testNewerSearchInvalidatesOlderRequest();
  testDisconnectInvalidatesRequest();
  testValidResponseAfterStaleResponseStillWorks();

  console.log('engine response filter tests passed');
}

run();
