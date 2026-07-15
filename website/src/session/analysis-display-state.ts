import type { AnalysisDisplayState, AnalysisSnapshot } from '@/lib/board-arrows';

export type AnalysisDisplayAction =
  | { type: 'INFO'; snapshot: AnalysisSnapshot }
  | { type: 'COMPLETE'; requestId: number }
  | { type: 'FEN_CHANGED'; fen: string }
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
    case 'CLEAR':
      if (!state.live && !state.finalized) return state;
      return { live: null, finalized: null };
  }
}
