import { Chess } from 'chess.js';
import type { EngineInfo, PlayerColor } from '../types/engine';
import type { HintLevel, TrainingState } from './training-machine';

export interface HintMoveDisplay {
  fen: string;
  uci: string;
  from: string;
  to: string;
  promotion?: string;
  san: string;
  piece: string;
  color: PlayerColor;
  isCapture: boolean;
  isCheck: boolean;
  isCheckmate: boolean;
  isCastling: boolean;
  isEnPassant: boolean;
  isForced: boolean;
}

export interface ProgressiveHintView {
  level: HintLevel;
  text: string;
  from?: string;
  to?: string;
  uci?: string;
  san?: string;
  isForced: boolean;
}

export function advanceHintLevel(level: HintLevel): HintLevel {
  return Math.min(3, level + 1) as HintLevel;
}

export function deriveHintMove(fen: string, uci: string): HintMoveDisplay | null {
  if (!/^[a-h][1-8][a-h][1-8][qrbn]?$/.test(uci)) return null;

  try {
    const chess = new Chess(fen);
    if (chess.isGameOver()) return null;

    const from = uci.slice(0, 2);
    const to = uci.slice(2, 4);
    const promotion = uci.length === 5 ? uci[4] : undefined;
    const legalMoves = chess.moves({ verbose: true });
    const legal = legalMoves.find(move =>
      move.from === from &&
      move.to === to &&
      ((move.promotion ?? undefined) === promotion)
    );
    if (!legal) return null;

    const applied = chess.move({ from, to, promotion });
    if (!applied) return null;

    const flags = applied.flags ?? '';
    return {
      fen,
      uci,
      from,
      to,
      promotion,
      san: applied.san,
      piece: applied.piece,
      color: applied.color as PlayerColor,
      isCapture: flags.includes('c') || flags.includes('e'),
      isCheck: applied.san.includes('+') || applied.san.includes('#'),
      isCheckmate: applied.san.includes('#'),
      isCastling: flags.includes('k') || flags.includes('q'),
      isEnPassant: flags.includes('e'),
      isForced: legalMoves.length === 1,
    };
  } catch {
    return null;
  }
}

export function buildProgressiveHintView(
  move: HintMoveDisplay,
  level: HintLevel,
): ProgressiveHintView | null {
  if (level <= 0) return null;

  if (move.isForced) {
    return {
      level,
      text: level >= 3
        ? `Forced move: ${move.san}`
        : `Only one legal move is available.`,
      from: move.from,
      to: level >= 2 ? move.to : undefined,
      uci: level >= 3 ? move.uci : undefined,
      san: level >= 3 ? move.san : undefined,
      isForced: true,
    };
  }

  if (level === 1) {
    return {
      level,
      text: `Consider the ${pieceName(move.piece)} on ${move.from}.`,
      from: move.from,
      isForced: false,
    };
  }

  if (level === 2) {
    return {
      level,
      text: `Consider moving from ${move.from} toward ${move.to}.`,
      from: move.from,
      to: move.to,
      isForced: false,
    };
  }

  return {
    level,
    text: `Try ${move.san}.`,
    from: move.from,
    to: move.to,
    uci: move.uci,
    san: move.san,
    isForced: false,
  };
}

export function reusableRootHintMove(info: EngineInfo | null, fen: string): string | null {
  if (!info) return null;
  if (info.rootFen !== fen) return null;
  if (info.purpose !== 'training-root-review' && info.purpose !== 'training-hint') return null;

  const move = info.pvs?.[0]?.pv?.[0];
  return typeof move === 'string' && move.length >= 4 ? move : null;
}

export function hintLevelUsedForMove(state: TrainingState, fen: string): HintLevel {
  if (state.status !== 'waiting-player') return 0;
  if (state.hint?.fen !== fen) return 0;
  if (state.hint.status !== 'available' && state.hint.status !== 'searching') return 0;
  return state.hint.level;
}

export function shouldClearHintForFen(state: TrainingState, fen: string): boolean {
  return state.status === 'waiting-player' && state.hint !== undefined && state.hint.fen !== fen;
}

function pieceName(piece: string): string {
  switch (piece) {
    case 'p': return 'pawn';
    case 'n': return 'knight';
    case 'b': return 'bishop';
    case 'r': return 'rook';
    case 'q': return 'queen';
    case 'k': return 'king';
    default: return 'piece';
  }
}
