export type EngineRequestId = number;

export interface RequestState {
  activeRequestId: EngineRequestId | null;
}

export function shouldAcceptSearchResponse(
  state: RequestState,
  requestId: unknown,
): requestId is EngineRequestId {
  return typeof requestId === 'number' &&
    Number.isSafeInteger(requestId) &&
    requestId > 0 &&
    state.activeRequestId === requestId;
}
