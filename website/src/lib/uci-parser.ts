/**
 * uci-parser.ts
 *
 * Standalone parser for UCI `info` lines emitted by the C++ engine.
 * Also exposes parseBestMove() for parsing `bestmove <m> [ponder <p>]` lines.
 *
 * Recognised `info` tokens (in any order on the line):
 *   depth <n>
 *   score cp <n>
 *   score mate <n>   (n is negative when the engine is getting mated)
 *   nodes <n>
 *   time <n>
 *   pv <move> [<move> ...]   (must be last token group — consumes to EOL)
 */

export interface ParsedInfo {
  depth?: number;
  multipv?: number;
  score?: number;
  mate?: number;
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
  const parts = line.split(' ');

  for (let i = 0; i < parts.length; i++) {
    const token = parts[i];

    if (token === 'depth' && i + 1 < parts.length) {
      result.depth = parseInt(parts[i + 1], 10);

    } else if (token === 'multipv' && i + 1 < parts.length) {
      result.multipv = parseInt(parts[i + 1], 10);

    } else if (token === 'score' && i + 2 < parts.length) {
      const scoreType = parts[i + 1];

      if (scoreType === 'cp') {
        result.score = parseInt(parts[i + 2], 10);

      } else if (scoreType === 'mate') {
        result.mate = parseInt(parts[i + 2], 10);
      }

    } else if (token === 'nodes' && i + 1 < parts.length) {
      result.nodes = parseInt(parts[i + 1], 10);

    } else if (token === 'time' && i + 1 < parts.length) {
      result.time = parseInt(parts[i + 1], 10);

    } else if (token === 'pv' && i + 1 < parts.length) {
      // PV is always the last token group — consume everything to EOL.
      result.pv = parts.slice(i + 1);
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
