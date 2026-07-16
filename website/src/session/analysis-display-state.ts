import type { AnalysisDisplayState, AnalysisSnapshot } from '../lib/board-arrows';

export type AnalysisDisplayAction =
  | { type: 'INFO'; snapshot: AnalysisSnapshot }
  | { type: 'COMPLETE'; requestId: number }
  | { type: 'FEN_CHANGED'; fen: string }
  | { type: 'STOPPED' }
  | { type: 'CLEAR' };

export function analysisDisplayReducer(
  state: AnalysisDisplayState,
  action: AnalysisDisplayAction,
): AnalysisDisplayState {
  switch (action.type) {
    case 'INFO': {
      const newerRequest = state.finalized !== null && action.snapshot.requestId > state.finalized.requestId;
      return {
        live: action.snapshot,
        finalized: newerRequest ? null : state.finalized,
      };
    }
    case 'COMPLETE':
      if (!state.live || state.live.requestId !== action.requestId) return state;
      return {
        live: null,
        finalized: {
          ...state.live,
          lines: state.live.lines.map(line => ({ ...line, pv: [...line.pv] })),
          status: 'finalized',
          createdAt: Date.now(),
        },
      };
    case 'FEN_CHANGED':
      if (state.live?.fen === action.fen || state.finalized?.fen === action.fen) {
        return {
          live: state.live?.fen === action.fen ? state.live : null,
          finalized: state.finalized?.fen === action.fen ? state.finalized : null,
        };
      }
      if (!state.live && !state.finalized) return state;
      return { live: null, finalized: null };
    case 'STOPPED':
      if (!state.live) return state;
      return {
        live: null,
        finalized: {
          ...state.live,
          lines: state.live.lines.map(line => ({ ...line, pv: [...line.pv] })),
          status: 'finalized',
          createdAt: Date.now(),
        },
      };
    case 'CLEAR':
      if (!state.live && !state.finalized) return state;
      return { live: null, finalized: null };
  }
}

export function selectCurrentAnalysisSnapshot(state: AnalysisDisplayState, positionFen: string): AnalysisSnapshot | null {
  const snapshot = state.live ?? state.finalized;
  if (!snapshot || snapshot.mode !== 'analysis' || snapshot.purpose !== 'analysis') return null;
  if (snapshot.fen !== positionFen || snapshot.positionFen !== positionFen) return null;
  if (!snapshot.positionKey || !snapshot.sessionId || snapshot.sessionGeneration === undefined) return null;
  return snapshot;
}
