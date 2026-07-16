import type { NormalizedEvaluation } from '../../lib/engine-evaluation';

export interface EvaluationPresentation {
  score: string;
  meaning: string;
  accessible: string;
}

export function formatEvaluation(evaluation: NormalizedEvaluation | null): EvaluationPresentation {
  if (!evaluation) return { score: '—', meaning: 'Waiting for evaluation', accessible: 'No current evaluation' };
  if (evaluation.kind === 'mate') {
    const moves = Math.ceil(evaluation.plies / 2);
    const side = evaluation.winner === 'white' ? 'White' : 'Black';
    return { score: `${evaluation.winner === 'white' ? '+' : '-'}M${moves}`, meaning: `Mate in ${moves} for ${side}`, accessible: `White-perspective evaluation: mate in ${moves} for ${side}` };
  }
  if (evaluation.kind === 'terminal') return { score: '—', meaning: `Terminal: ${evaluation.result}`, accessible: `Terminal position: ${evaluation.result}` };
  const pawns = evaluation.value / 100;
  const score = `${pawns > 0 ? '+' : ''}${pawns.toFixed(2)}`;
  const magnitude = Math.abs(evaluation.value);
  const meaning = magnitude < 20 ? 'Approximately equal' : `${evaluation.value > 0 ? 'White' : 'Black'} is ${magnitude < 80 ? 'slightly better' : magnitude < 180 ? 'better' : 'clearly better'}`;
  return { score, meaning, accessible: `White-perspective evaluation: ${score}. ${meaning}` };
}
