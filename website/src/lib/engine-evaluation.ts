import type { MoveGrade } from '../types/grades';
import type { EngineInfo, PVLine } from '../types/engine';

export type Color = 'w' | 'b';
export type Winner = 'white' | 'black';

export interface RawEngineScore {
  score?: number;
  mate?: number;
}

export type NormalizedEvaluation =
  | { kind: 'centipawn'; value: number }
  | { kind: 'mate'; plies: number; winner: Winner }
  | { kind: 'terminal'; result: 'checkmate' | 'stalemate' | 'draw' | 'unknown' };

export interface MoveGradeInput {
  best: NormalizedEvaluation | null;
  played: NormalizedEvaluation | null;
  playerColor: Color;
  legalMoveCount?: number;
  isBook?: boolean;
}

export interface MoveGradeResult {
  grade: MoveGrade;
  loss: number;
}

export function fenTurn(fen: string): Color | null {
  const fields = fen.trim().split(/\s+/);
  if (fields.length < 2) return null;
  return fields[1] === 'w' || fields[1] === 'b' ? fields[1] : null;
}

function winnerFromRoot(rootTurn: Color, rawMate: number): Winner {
  const sideToMoveWins = rawMate >= 0;
  if ((rootTurn === 'w' && sideToMoveWins) || (rootTurn === 'b' && !sideToMoveWins)) {
    return 'white';
  }
  return 'black';
}

/**
 * Canonical project convention:
 * - Centipawns are always White-perspective. Positive means White is better.
 * - Mate preserves the winning side and distance. It is not flattened to cp.
 *
 * The C++ engine emits UCI scores from the root side-to-move perspective.
 */
export function normalizeEngineScore(raw: RawEngineScore, rootFen: string): NormalizedEvaluation | null {
  const rootTurn = fenTurn(rootFen);
  if (!rootTurn) return null;

  if (raw.mate !== undefined) {
    if (!Number.isFinite(raw.mate) || !Number.isInteger(raw.mate) || raw.mate === 0) return null;
    return {
      kind: 'mate',
      plies: Math.abs(raw.mate) * 2,
      winner: winnerFromRoot(rootTurn, raw.mate),
    };
  }

  if (raw.score !== undefined) {
    if (!Number.isFinite(raw.score) || !Number.isInteger(raw.score)) return null;
    return {
      kind: 'centipawn',
      value: rootTurn === 'w' ? raw.score : -raw.score,
    };
  }

  return null;
}

export function evaluationForColor(evaluation: NormalizedEvaluation, color: Color): number | null {
  if (evaluation.kind !== 'centipawn') return null;
  return color === 'w' ? evaluation.value : -evaluation.value;
}

export function displayScore(evaluation: NormalizedEvaluation | null): string {
  if (!evaluation) return '0.00';
  if (evaluation.kind === 'centipawn') {
    const pawns = (evaluation.value / 100).toFixed(2);
    return evaluation.value > 0 ? `+${pawns}` : pawns;
  }
  if (evaluation.kind === 'mate') {
    const sign = evaluation.winner === 'white' ? '+' : '-';
    return `${sign}M${Math.ceil(evaluation.plies / 2)}`;
  }
  return '0.00';
}

export function evalBarCentipawns(evaluation: NormalizedEvaluation | null): number {
  if (!evaluation) return 0;
  if (evaluation.kind === 'centipawn') return evaluation.value;
  if (evaluation.kind === 'mate') return evaluation.winner === 'white' ? 1000 : -1000;
  return 0;
}

function mateLoss(best: NormalizedEvaluation, played: NormalizedEvaluation, playerColor: Color): number | null {
  if (best.kind !== 'mate' && played.kind !== 'mate') return null;
  const playerWinner: Winner = playerColor === 'w' ? 'white' : 'black';
  const opponentWinner: Winner = playerColor === 'w' ? 'black' : 'white';

  if (best.kind === 'mate' && played.kind === 'mate') {
    if (best.winner === playerWinner && played.winner === playerWinner) {
      return Math.max(0, played.plies - best.plies) * 20;
    }
    if (best.winner === opponentWinner && played.winner === opponentWinner) {
      return Math.max(0, best.plies - played.plies) * 20;
    }
    if (best.winner === playerWinner && played.winner === opponentWinner) return 1000;
    if (best.winner === opponentWinner && played.winner === playerWinner) return 0;
  }

  if (best.kind === 'mate') {
    return best.winner === playerWinner ? 1000 : 0;
  }

  if (played.kind === 'mate') {
    return played.winner === opponentWinner ? 1000 : 0;
  }

  return null;
}

export function moveLossCp(
  best: NormalizedEvaluation,
  played: NormalizedEvaluation,
  playerColor: Color,
): number | null {
  const mate = mateLoss(best, played, playerColor);
  if (mate !== null) return mate;

  const bestCp = evaluationForColor(best, playerColor);
  const playedCp = evaluationForColor(played, playerColor);
  if (bestCp === null || playedCp === null) return null;
  return Math.max(0, bestCp - playedCp);
}

export function gradeFromLoss(loss: number): MoveGrade {
  if (loss <= 10) return 'Best';
  if (loss <= 70) return 'Good';
  if (loss <= 150) return 'Inaccuracy';
  if (loss <= 300) return 'Mistake';
  return 'Blunder';
}

export function gradeMove(input: MoveGradeInput): MoveGradeResult | null {
  if (input.isBook) return { grade: 'Book', loss: 0 };
  if (input.legalMoveCount === 1) return { grade: 'Forced', loss: 0 };
  if (!input.best || !input.played) return null;

  const loss = moveLossCp(input.best, input.played, input.playerColor);
  if (loss === null) return null;
  return { grade: gradeFromLoss(loss), loss };
}

export function normalizePvLine(pv: PVLine, rootFen: string): PVLine {
  return {
    ...pv,
    evaluation: normalizeEngineScore({ score: pv.score, mate: pv.mate }, rootFen) ?? undefined,
  };
}

export function normalizeEngineInfo(info: EngineInfo | null): EngineInfo | null {
  if (!info?.rootFen) return info;
  return {
    ...info,
    pvs: info.pvs.map(pv => normalizePvLine(pv, info.rootFen!)),
  };
}
