import { RawData } from 'ws';
import { validateFen } from 'chess.js';
import { isEngineDifficulty } from './engine-difficulty';
import type { EngineDifficulty, SearchPurpose } from './engine-difficulty';

export const DEFAULT_START_FEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
export const ANALYSIS_MULTIPV = 3;

const MAX_RAW_MESSAGE_BYTES = 16 * 1024;
const MAX_FEN_LENGTH = 256;
const MAX_MOVES = 512;
const MAX_DEPTH = 64;
const MAX_TIME_MS = 24 * 60 * 60 * 1000;
const MAX_INCREMENT_MS = 60 * 60 * 1000;
const MAX_MULTIPV = 8;
const MAX_HASH_MB = 32768;
const MAX_BOOK_DEPTH = 100;
const MAX_REQUEST_ID = Number.MAX_SAFE_INTEGER;

export type ProtocolErrorCode =
  | 'INVALID_JSON'
  | 'MESSAGE_TOO_LARGE'
  | 'INVALID_MESSAGE'
  | 'UNKNOWN_MESSAGE_TYPE'
  | 'INVALID_FEN'
  | 'INVALID_MOVE'
  | 'INVALID_NUMBER'
  | 'INVALID_OPTION'
  | 'INVALID_UCI_COMMAND';

export interface ProtocolError {
  code: ProtocolErrorCode;
  message: string;
}

export type ParseResult<T> =
  | { ok: true; value: T }
  | { ok: false; error: ProtocolError };

export interface MoveMessage {
  type: 'move';
  requestId: number;
  purpose: 'opponent';
  ponder: boolean;
  fen: string;
  moves: string[];
  wtime: number;
  btime: number;
  winc: number;
  binc: number;
  depth?: number;
  multiPv: number;
  difficulty: EngineDifficulty;
  searchLimit?: SearchLimit;
}

export interface PositionMessage {
  type: 'position';
  fen: string;
  moves: string[];
}

export interface AnalyzeMessage {
  type: 'analyze';
  requestId: number;
  purpose: 'training-root-review' | 'training-result-review' | 'training-hint' | 'analysis';
  fen: string;
  moves: string[];
  depth?: number;
  multiPv?: number;
}

export interface StopMessage {
  type: 'stop';
  requestId?: number;
}

export interface NewGameMessage {
  type: 'newgame';
}

export interface ReleaseSessionMessage {
  type: 'releaseSession';
}

export type EngineOptionName = 'Hash' | 'OwnBook' | 'BookDepth' | 'MultiPV' | 'BookSelection' | 'BookSelectionTopN' | 'BookSeed';

export interface SetOptionMessage {
  type: 'setoption';
  name: EngineOptionName;
  value: number | boolean | string;
}

export type ClientMessage =
  | MoveMessage
  | PositionMessage
  | AnalyzeMessage
  | StopMessage
  | NewGameMessage
  | ReleaseSessionMessage
  | SetOptionMessage;

export type SearchLimit =
  | {
      mode: 'clock';
      wtime: number;
      btime: number;
      winc: number;
      binc: number;
      movestogo?: number;
    }
  | {
      mode: 'movetime';
      movetimeMs: number;
    }
  | {
      mode: 'depth';
      depth: number;
    };

function protocolError(code: ProtocolErrorCode, message: string): ProtocolError {
  return { code, message };
}

function ok<T>(value: T): ParseResult<T> {
  return { ok: true, value };
}

function err<T>(code: ProtocolErrorCode, message: string): ParseResult<T> {
  return { ok: false, error: protocolError(code, message) };
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function containsLineBreak(value: string): boolean {
  return value.includes('\n') || value.includes('\r');
}

function rawDataToBuffer(raw: RawData): Buffer {
  if (Buffer.isBuffer(raw)) {
    return raw;
  }

  if (Array.isArray(raw)) {
    return Buffer.concat(raw);
  }

  return Buffer.from(raw);
}

function validateString(value: unknown, field: string): ParseResult<string> {
  if (typeof value !== 'string') {
    return err('INVALID_MESSAGE', `${field} must be a string.`);
  }

  if (containsLineBreak(value)) {
    return err('INVALID_MESSAGE', `${field} must not contain line breaks.`);
  }

  return ok(value);
}

function validateFenString(value: unknown): ParseResult<string> {
  const stringResult = validateString(value, 'fen');
  if (stringResult.ok === false) {
    return err('INVALID_FEN', stringResult.error.message);
  }

  const fen = stringResult.value;
  if (fen.length > MAX_FEN_LENGTH) {
    return err('INVALID_FEN', 'FEN is too long.');
  }

  const fields = fen.trim().split(/\s+/);
  if (fields.length !== 6) {
    return err('INVALID_FEN', 'FEN must contain six fields.');
  }

  const [board, activeColor, castling, enPassant, halfmove, fullmove] = fields;
  if (board.split('/').length !== 8) {
    return err('INVALID_FEN', 'FEN board must contain eight ranks.');
  }

  if (activeColor !== 'w' && activeColor !== 'b') {
    return err('INVALID_FEN', 'FEN active color must be w or b.');
  }

  if (!/^(K?Q?k?q?|-)$/.test(castling)) {
    return err('INVALID_FEN', 'FEN castling field is invalid.');
  }

  if (!/^(-|[a-h][36])$/.test(enPassant)) {
    return err('INVALID_FEN', 'FEN en-passant field is invalid.');
  }

  if (!/^\d+$/.test(halfmove) || Number(halfmove) > 1000) {
    return err('INVALID_FEN', 'FEN halfmove counter is invalid.');
  }

  if (!/^[1-9]\d*$/.test(fullmove) || Number(fullmove) > 10000) {
    return err('INVALID_FEN', 'FEN fullmove counter is invalid.');
  }

  const chessValidation = validateFen(fen);
  if (chessValidation.ok === false) {
    return err('INVALID_FEN', 'FEN is not valid.');
  }

  return ok(fen);
}

function validateMoves(value: unknown): ParseResult<string[]> {
  if (value === undefined) {
    return ok([]);
  }

  if (!Array.isArray(value)) {
    return err('INVALID_MOVE', 'moves must be an array.');
  }

  if (value.length > MAX_MOVES) {
    return err('INVALID_MOVE', 'Move list is too long.');
  }

  const moves: string[] = [];
  for (const item of value) {
    if (typeof item !== 'string') {
      return err('INVALID_MOVE', 'Each move must be a string.');
    }

    if (containsLineBreak(item)) {
      return err('INVALID_MOVE', 'Moves must not contain line breaks.');
    }

    if (!/^[a-h][1-8][a-h][1-8][qrbn]?$/.test(item)) {
      return err('INVALID_MOVE', 'Move must be valid UCI coordinate notation.');
    }

    moves.push(item);
  }

  return ok(moves);
}

function validateInteger(
  value: unknown,
  field: string,
  min: number,
  max: number,
  code: ProtocolErrorCode = 'INVALID_NUMBER',
): ParseResult<number> {
  if (typeof value !== 'number' || !Number.isFinite(value) || !Number.isInteger(value)) {
    return err(code, `${field} must be an integer.`);
  }

  if (value < min || value > max) {
    return err(code, `${field} is out of range.`);
  }

  return ok(value);
}

function validateRequestId(value: unknown): ParseResult<number> {
  return validateInteger(value, 'requestId', 1, MAX_REQUEST_ID, 'INVALID_MESSAGE');
}

function validateOptionalRequestId(value: unknown): ParseResult<number | undefined> {
  if (value === undefined) {
    return ok(undefined);
  }

  return validateRequestId(value);
}

function optionalInteger(value: unknown, field: string, min: number, max: number): ParseResult<number | undefined> {
  if (value === undefined) {
    return ok(undefined);
  }

  return validateInteger(value, field, min, max);
}

function validateDifficulty(value: unknown): ParseResult<EngineDifficulty> {
  if (!isEngineDifficulty(value)) {
    return err('INVALID_MESSAGE', 'difficulty is invalid.');
  }

  return ok(value);
}

function validateSearchPurpose(value: unknown, allowed: readonly SearchPurpose[]): ParseResult<SearchPurpose> {
  if (typeof value !== 'string') {
    return err('INVALID_MESSAGE', 'purpose must be a string.');
  }

  if (!allowed.includes(value as SearchPurpose)) {
    return err('INVALID_MESSAGE', 'purpose is invalid.');
  }

  return ok(value as SearchPurpose);
}

function validateCommonPositionFields(record: Record<string, unknown>): ParseResult<{ fen: string; moves: string[] }> {
  const fenResult = validateFenString(record.fen);
  if (fenResult.ok === false) {
    return fenResult;
  }

  const movesResult = validateMoves(record.moves);
  if (movesResult.ok === false) {
    return movesResult;
  }

  return ok({ fen: fenResult.value, moves: movesResult.value });
}

function validateMoveMessage(record: Record<string, unknown>): ParseResult<MoveMessage> {
  const requestId = validateRequestId(record.requestId);
  if (requestId.ok === false) return requestId;

  const purpose = validateSearchPurpose(record.purpose, ['opponent']);
  if (purpose.ok === false) return purpose;

  if (record.ponder !== undefined && typeof record.ponder !== 'boolean') {
    return err('INVALID_MESSAGE', 'ponder must be a boolean.');
  }

  const positionResult = validateCommonPositionFields(record);
  if (positionResult.ok === false) {
    return positionResult;
  }

  const hasClockField = record.wtime !== undefined ||
    record.btime !== undefined ||
    record.winc !== undefined ||
    record.binc !== undefined ||
    record.movestogo !== undefined;
  const hasClockTimes = record.wtime !== undefined || record.btime !== undefined;
  if (hasClockField && (!hasClockTimes || record.wtime === undefined || record.btime === undefined)) {
    return err('INVALID_MESSAGE', 'Clock mode requires both wtime and btime.');
  }

  const wtime = validateInteger(record.wtime ?? 0, 'wtime', 0, MAX_TIME_MS);
  if (wtime.ok === false) return wtime;

  const btime = validateInteger(record.btime ?? 0, 'btime', 0, MAX_TIME_MS);
  if (btime.ok === false) return btime;

  const winc = validateInteger(record.winc ?? 0, 'winc', 0, MAX_INCREMENT_MS);
  if (winc.ok === false) return winc;

  const binc = validateInteger(record.binc ?? 0, 'binc', 0, MAX_INCREMENT_MS);
  if (binc.ok === false) return binc;

  const depth = optionalInteger(record.depth, 'depth', 1, MAX_DEPTH);
  if (depth.ok === false) return depth;

  const movetime = optionalInteger(record.movetime ?? record.movetimeMs, 'movetime', 1, MAX_TIME_MS);
  if (movetime.ok === false) return movetime;

  const movestogo = optionalInteger(record.movestogo, 'movestogo', 1, MAX_MOVES);
  if (movestogo.ok === false) return movestogo;

  const hasPositiveClock = wtime.value > 0 || btime.value > 0;
  const explicitLimitCount =
    (hasPositiveClock ? 1 : 0) +
    (depth.value !== undefined ? 1 : 0) +
    (movetime.value !== undefined ? 1 : 0);

  if (explicitLimitCount > 1) {
    return err('INVALID_MESSAGE', 'Search request must specify exactly one timing mode.');
  }

  if (movestogo.value !== undefined && !hasPositiveClock) {
    return err('INVALID_MESSAGE', 'movestogo is only valid with clock mode.');
  }

  const multiPv = validateInteger(record.multiPv ?? 1, 'multiPv', 1, MAX_MULTIPV);
  if (multiPv.ok === false) return multiPv;

  const difficulty = validateDifficulty(record.difficulty);
  if (difficulty.ok === false) return difficulty;

  const searchLimit: SearchLimit | undefined = hasPositiveClock
    ? { mode: 'clock', wtime: wtime.value, btime: btime.value, winc: winc.value, binc: binc.value, ...(movestogo.value !== undefined ? { movestogo: movestogo.value } : {}) }
    : depth.value !== undefined
      ? { mode: 'depth', depth: depth.value }
      : movetime.value !== undefined
        ? { mode: 'movetime', movetimeMs: movetime.value }
        : undefined;

  return ok({
    type: 'move',
    requestId: requestId.value,
    purpose: 'opponent',
    ponder: record.ponder === true,
    fen: positionResult.value.fen,
    moves: positionResult.value.moves,
    wtime: wtime.value,
    btime: btime.value,
    winc: winc.value,
    binc: binc.value,
    depth: depth.value,
    multiPv: multiPv.value,
    difficulty: difficulty.value,
    searchLimit,
  });
}

function validatePositionMessage(record: Record<string, unknown>): ParseResult<PositionMessage> {
  const positionResult = validateCommonPositionFields(record);
  if (positionResult.ok === false) {
    return positionResult;
  }

  return ok({ type: 'position', ...positionResult.value });
}

function validateAnalyzeMessage(record: Record<string, unknown>): ParseResult<AnalyzeMessage> {
  const requestId = validateRequestId(record.requestId);
  if (requestId.ok === false) return requestId;

  if (record.difficulty !== undefined) {
    return err('INVALID_MESSAGE', 'difficulty is only valid for opponent searches.');
  }

  const purpose = validateSearchPurpose(record.purpose, ['training-root-review', 'training-result-review', 'training-hint', 'analysis']);
  if (purpose.ok === false) return purpose;

  const positionResult = validateCommonPositionFields(record);
  if (positionResult.ok === false) {
    return positionResult;
  }

  const depth = optionalInteger(record.depth, 'depth', 1, MAX_DEPTH);
  if (depth.ok === false) return depth;

  const multiPv = optionalInteger(record.multiPv, 'multiPv', 1, MAX_MULTIPV);
  if (multiPv.ok === false) return multiPv;

  return ok({
    type: 'analyze',
    requestId: requestId.value,
    purpose: purpose.value as AnalyzeMessage['purpose'],
    fen: positionResult.value.fen,
    moves: positionResult.value.moves,
    depth: depth.value,
    multiPv: multiPv.value,
  });
}

function validateStopMessage(record: Record<string, unknown>): ParseResult<StopMessage> {
  const requestId = validateOptionalRequestId(record.requestId);
  if (requestId.ok === false) return requestId;

  return ok({ type: 'stop', requestId: requestId.value });
}

function validateEmptyMessage(record: Record<string, unknown>, type: NewGameMessage['type']): ParseResult<NewGameMessage>;
function validateEmptyMessage(record: Record<string, unknown>, type: ReleaseSessionMessage['type']): ParseResult<ReleaseSessionMessage>;
function validateEmptyMessage(
  _record: Record<string, unknown>,
  type: NewGameMessage['type'] | ReleaseSessionMessage['type'],
): ParseResult<NewGameMessage | ReleaseSessionMessage> {
  return ok({ type });
}

function validateSetOptionMessage(record: Record<string, unknown>): ParseResult<SetOptionMessage> {
  const nameResult = validateString(record.name, 'name');
  if (nameResult.ok === false) {
    return err('INVALID_OPTION', nameResult.error.message);
  }

  const name = nameResult.value;
  if (name === 'Hash') {
    const value = validateInteger(record.value, 'Hash', 1, MAX_HASH_MB, 'INVALID_OPTION');
    return value.ok === true ? ok({ type: 'setoption', name, value: value.value }) : value;
  }

  if (name === 'BookDepth') {
    const value = validateInteger(record.value, 'BookDepth', 0, MAX_BOOK_DEPTH, 'INVALID_OPTION');
    return value.ok === true ? ok({ type: 'setoption', name, value: value.value }) : value;
  }

  if (name === 'MultiPV') {
    const value = validateInteger(record.value, 'MultiPV', 1, MAX_MULTIPV, 'INVALID_OPTION');
    return value.ok === true ? ok({ type: 'setoption', name, value: value.value }) : value;
  }

  if (name === 'BookSelection') {
    const value = validateString(record.value, 'BookSelection');
    if (value.ok === false) return err('INVALID_OPTION', value.error.message);
    if (!['best', 'weighted', 'top-n-weighted'].includes(value.value)) {
      return err('INVALID_OPTION', 'BookSelection is invalid.');
    }
    return ok({ type: 'setoption', name, value: value.value });
  }

  if (name === 'BookSelectionTopN') {
    const value = validateInteger(record.value, 'BookSelectionTopN', 1, 32, 'INVALID_OPTION');
    return value.ok === true ? ok({ type: 'setoption', name, value: value.value }) : value;
  }

  if (name === 'BookSeed') {
    const value = validateInteger(record.value, 'BookSeed', 0, 2147483647, 'INVALID_OPTION');
    return value.ok === true ? ok({ type: 'setoption', name, value: value.value }) : value;
  }

  if (name === 'OwnBook') {
    if (typeof record.value !== 'boolean') {
      return err('INVALID_OPTION', 'OwnBook must be a boolean.');
    }

    return ok({ type: 'setoption', name, value: record.value });
  }

  return err('INVALID_OPTION', 'Unsupported engine option.');
}

export function validateClientMessage(value: unknown): ParseResult<ClientMessage> {
  if (!isRecord(value)) {
    return err('INVALID_MESSAGE', 'Message must be a JSON object.');
  }

  if (typeof value.type !== 'string') {
    return err('INVALID_MESSAGE', 'Message type must be a string.');
  }

  if (containsLineBreak(value.type)) {
    return err('INVALID_MESSAGE', 'Message type must not contain line breaks.');
  }

  switch (value.type) {
    case 'move':
      return validateMoveMessage(value);
    case 'position':
      return validatePositionMessage(value);
    case 'analyze':
      return validateAnalyzeMessage(value);
    case 'stop':
      return validateStopMessage(value);
    case 'newgame':
      return validateEmptyMessage(value, 'newgame');
    case 'releaseSession':
      return validateEmptyMessage(value, 'releaseSession');
    case 'setoption':
      return validateSetOptionMessage(value);
    default:
      return err('UNKNOWN_MESSAGE_TYPE', 'Unsupported message type.');
  }
}

export function parseClientMessage(raw: RawData): ParseResult<ClientMessage> {
  const rawBuffer = rawDataToBuffer(raw);
  if (rawBuffer.byteLength > MAX_RAW_MESSAGE_BYTES) {
    return err('MESSAGE_TOO_LARGE', 'Message is too large.');
  }

  let parsed: unknown;
  try {
    parsed = JSON.parse(rawBuffer.toString('utf8'));
  } catch {
    return err('INVALID_JSON', 'Message must be valid JSON.');
  }

  return validateClientMessage(parsed);
}

function assertSafeUciValue(value: string): void {
  if (containsLineBreak(value)) {
    throw protocolError('INVALID_UCI_COMMAND', 'UCI command must not contain line breaks.');
  }
}

export function buildPositionCommand(fen: string, moves: readonly string[]): string {
  assertSafeUciValue(fen);
  moves.forEach(assertSafeUciValue);

  const movesStr = moves.length > 0 ? ` moves ${moves.join(' ')}` : '';
  return fen === DEFAULT_START_FEN
    ? `position startpos${movesStr}`
    : `position fen ${fen}${movesStr}`;
}

export function buildGoCommand(limit: SearchLimit, options: { ponder?: boolean } = {}): string {
  const prefix = options.ponder === true ? 'go ponder' : 'go';
  if (limit.mode === 'depth') {
    return `${prefix} depth ${limit.depth}`;
  }

  if (limit.mode === 'movetime') {
    return `${prefix} movetime ${limit.movetimeMs}`;
  }

  const movestogo = limit.movestogo !== undefined ? ` movestogo ${limit.movestogo}` : '';
  return `${prefix} wtime ${limit.wtime} btime ${limit.btime} winc ${limit.winc} binc ${limit.binc}${movestogo}`;
}

export function buildSetOptionCommand(name: EngineOptionName, value: number | boolean | string): string {
  const serializedValue = typeof value === 'boolean' ? String(value) : `${value}`;
  assertSafeUciValue(name);
  assertSafeUciValue(serializedValue);
  return `setoption name ${name} value ${serializedValue}`;
}

export function writeUciCommand(stdin: NodeJS.WritableStream | null | undefined, command: string): boolean {
  assertSafeUciValue(command);
  if (!stdin) {
    return false;
  }

  stdin.write(`${command}\n`);
  return true;
}
