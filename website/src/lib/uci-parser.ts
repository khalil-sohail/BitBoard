/**
 * uci-parser.ts
 *
 * Standalone parser for UCI `info` lines emitted by the C++ engine.
 * Also exposes parseBestMove() for parsing `bestmove <m> [ponder <p>]` lines.
 *
 * Recognised `info` tokens (in any order on the line):
 *   depth <n>
 *   seldepth <n>
 *   score cp <n>
 *   score mate <n>   (n is negative when the engine is getting mated)
 *   nodes <n>
 *   time <n>
 *   pv <move> [<move> ...]   (must be last token group — consumes to EOL)
 */

export interface ParsedInfo {
  depth?: number;
  selectiveDepth?: number;
  multipv?: number;
  score?: number;
  mate?: number;
  scoreBound?: 'lowerbound' | 'upperbound';
  nodes?: number;
  time?: number;
  pv?: string[];
}

export interface ParsedBestMove {
  bestMove: string;
  ponderMove: string | null;
}

/**
 * Parse a single UCI `info ...` line into a structured object.
 *
 * @param line - The raw `info ...` string from the engine stdout.
 * @returns A ParsedInfo object, or null if the line lacks required fields.
 */
export function parseUciInfo(line: string): ParsedInfo | null {
  const result: ParsedInfo = {};
  const parts = line.trim().split(/\s+/);
  const integer = /^[+-]?\d+$/;
  const uciMove = /^[a-h][1-8][a-h][1-8][qrbn]?$/;

  if (parts[0] !== 'info' || parts.filter((token) => token === 'score').length > 1) return null;

  const readInteger = (index: number): number | null => {
    if (index >= parts.length || !integer.test(parts[index])) return null;
    const value = Number(parts[index]);
    return Number.isSafeInteger(value) ? value : null;
  };

  for (let i = 0; i < parts.length; i++) {
    const token = parts[i];

    if (token === 'depth' && i + 1 < parts.length) {
      const value = readInteger(i + 1);
      if (value === null) return null;
      result.depth = value;

    } else if (token === 'seldepth' && i + 1 < parts.length) {
      const value = readInteger(i + 1);
      if (value === null) return null;
      result.selectiveDepth = value;

    } else if (token === 'multipv' && i + 1 < parts.length) {
      const value = readInteger(i + 1);
      if (value === null) return null;
      result.multipv = value;

    } else if (token === 'score' && i + 2 < parts.length) {
      const scoreType = parts[i + 1];
      const value = readInteger(i + 2);
      if (value === null) return null;

      if (scoreType === 'cp') {
        result.score = value;

      } else if (scoreType === 'mate') {
        if (value === 0) return null;
        result.mate = value;
      } else {
        return null;
      }
      if (parts.filter((token) => token === 'cp').length !== (scoreType === 'cp' ? 1 : 0) ||
          parts.filter((token) => token === 'mate').length !== (scoreType === 'mate' ? 1 : 0)) return null;
      const bound = parts[i + 3];
      if (bound === 'lowerbound' || bound === 'upperbound') result.scoreBound = bound;
      else if (parts.includes('lowerbound') || parts.includes('upperbound')) return null;

    } else if (token === 'nodes' && i + 1 < parts.length) {
      const value = readInteger(i + 1);
      if (value === null) return null;
      result.nodes = value;

    } else if (token === 'time' && i + 1 < parts.length) {
      const value = readInteger(i + 1);
      if (value === null) return null;
      result.time = value;

    } else if (token === 'pv' && i + 1 < parts.length) {
      // PV is always the last token group — consume everything to EOL.
      const pv = parts.slice(i + 1);
      if (pv.length === 0 || pv.some((move) => !uciMove.test(move))) return null;
      result.pv = pv;
      break;
    }
  }

  // Require at minimum a depth and a score/mate to be considered valid.
  if (result.depth === undefined || (result.score === undefined && result.mate === undefined)) {
    return null;
  }

  // Default to 1 if the engine doesn't explicitly send multipv
  if (result.multipv === undefined) {
    result.multipv = 1;
  }

  return result;
}

/**
 * Parse a `bestmove <m> [ponder <p>]` line from engine stdout.
 *
 * Returns { bestMove, ponderMove } where ponderMove is null if absent.
 * Returns null if the line doesn't start with "bestmove".
 */
export function parseBestMove(line: string): ParsedBestMove | null {
  const parts = line.trim().split(/\s+/);
  if (parts[0] !== 'bestmove' || !parts[1]) return null;

  const bestMove   = parts[1];
  const ponderMove = (parts[2] === 'ponder' && parts[3]) ? parts[3] : null;

  return { bestMove, ponderMove };
}
