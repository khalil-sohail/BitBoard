import { EventEmitter } from 'events';
import { ChildProcess, SpawnOptions } from 'child_process';
import path from 'path';
import { PassThrough } from 'stream';
import { strict as assert } from 'assert';
import { WebSocket } from 'ws';
import { EnginePoolManager } from './engine-session';
import {
  DEFAULT_START_FEN,
  buildGoCommand,
  buildPositionCommand,
  buildSetOptionCommand,
  parseClientMessage,
  validateClientMessage,
} from './engine-protocol';

interface ServerMessage {
  type?: string;
  code?: string;
  message?: string;
  requestId?: number;
  move?: string | null;
  terminal?: {
    reason: string;
    winner?: string;
  };
  depth?: number;
  pvs?: unknown[];
}

class MockSocket extends EventEmitter {
  public readyState: number = WebSocket.OPEN;
  public readonly sent: ServerMessage[] = [];

  public send(payload: string): void {
    if (this.readyState !== WebSocket.OPEN) {
      throw new Error('Cannot send to closed socket');
    }

    this.sent.push(JSON.parse(payload) as ServerMessage);
    this.emit('sent', this.sent[this.sent.length - 1]);
  }

  public close(): void {
    if (this.readyState === WebSocket.CLOSED) {
      return;
    }

    this.readyState = WebSocket.CLOSED;
    this.emit('close');
  }

  public asWebSocket(): WebSocket {
    return this as unknown as WebSocket;
  }

  public emitRawMessage(payload: string): void {
    this.emit('message', Buffer.from(payload));
  }
}

class RecordingEngineProcess extends EventEmitter {
  public readonly stdin = new PassThrough();
  public readonly stdout = new PassThrough();
  public readonly stderr = new PassThrough();
  public readonly writes: string[] = [];
  public exitCode: number | null = null;
  public signalCode: NodeJS.Signals | null = null;
  private lineBuffer = '';

  public constructor() {
    super();
    this.stdin.setEncoding('utf8');
    this.stdin.on('data', chunk => {
      this.lineBuffer += chunk.toString();
      const lines = this.lineBuffer.split('\n');
      this.lineBuffer = lines.pop() || '';
      lines.forEach(line => this.handleLine(line.trim()));
    });
  }

  public kill(signal?: NodeJS.Signals | number): boolean {
    if (this.exitCode !== null || this.signalCode !== null) {
      return false;
    }

    this.signalCode = typeof signal === 'string' ? signal : 'SIGTERM';
    setImmediate(() => this.emit('exit', null, this.signalCode));
    return true;
  }

  public asChildProcess(): ChildProcess {
    return this as unknown as ChildProcess;
  }

  private handleLine(line: string): void {
    if (!line) {
      return;
    }

    this.writes.push(line);

    if (line === 'uci') {
      this.stdout.write('uciok\n');
      return;
    }

    if (line === 'isready') {
      this.stdout.write('readyok\n');
      return;
    }

    if (line.startsWith('go')) {
      this.stdout.write('bestmove e2e4\n');
      return;
    }

    if (line === 'quit') {
      this.exitCode = 0;
      setImmediate(() => this.emit('exit', 0, null));
    }
  }
}

class ManualEngineProcess extends EventEmitter {
  public readonly stdin = new PassThrough();
  public readonly stdout = new PassThrough();
  public readonly stderr = new PassThrough();
  public readonly writes: string[] = [];
  public exitCode: number | null = null;
  public signalCode: NodeJS.Signals | null = null;
  private lineBuffer = '';

  public constructor() {
    super();
    this.stdin.setEncoding('utf8');
    this.stdin.on('data', chunk => {
      this.lineBuffer += chunk.toString();
      const lines = this.lineBuffer.split('\n');
      this.lineBuffer = lines.pop() || '';
      lines.forEach(line => this.handleLine(line.trim()));
    });
  }

  public emitInfo(line = 'info depth 1 score cp 10 nodes 1 time 1 pv e2e4'): void {
    this.stdout.write(`${line}\n`);
  }

  public emitBestMove(move = 'bestmove e2e4'): void {
    this.stdout.write(`${move}\n`);
  }

  public kill(signal?: NodeJS.Signals | number): boolean {
    if (this.exitCode !== null || this.signalCode !== null) {
      return false;
    }

    this.signalCode = typeof signal === 'string' ? signal : 'SIGTERM';
    setImmediate(() => this.emit('exit', null, this.signalCode));
    return true;
  }

  public asChildProcess(): ChildProcess {
    return this as unknown as ChildProcess;
  }

  private handleLine(line: string): void {
    if (!line) {
      return;
    }

    this.writes.push(line);

    if (line === 'uci') {
      this.stdout.write('uciok\n');
      return;
    }

    if (line === 'isready') {
      this.stdout.write('readyok\n');
      return;
    }

    if (line === 'quit') {
      this.exitCode = 0;
      setImmediate(() => this.emit('exit', 0, null));
    }
  }
}

function waitForMessage(socket: MockSocket, type: string): Promise<ServerMessage> {
  const existing = socket.sent.find(message => message.type === type);
  if (existing) {
    return Promise.resolve(existing);
  }

  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      socket.off('sent', handleSent);
      reject(new Error(`Timed out waiting for ${type}`));
    }, 2_000);

    const handleSent = (message: ServerMessage) => {
      if (message.type !== type) {
        return;
      }

      clearTimeout(timeout);
      socket.off('sent', handleSent);
      resolve(message);
    };

    socket.on('sent', handleSent);
  });
}

function messagesOfType(socket: MockSocket, type: string): ServerMessage[] {
  return socket.sent.filter(message => message.type === type);
}

async function flushEvents(): Promise<void> {
  await new Promise<void>(resolve => setImmediate(resolve));
}

async function createManualSession(): Promise<{ pool: EnginePoolManager; engine: ManualEngineProcess; socket: MockSocket }> {
  const engine = new ManualEngineProcess();
  const pool = new EnginePoolManager({
    maxConcurrent: 1,
    enginePath: process.execPath,
    idleTimeoutMs: 30_000,
    sessionMaxMs: 30_000,
    spawnEngineProcess: () => engine.asChildProcess(),
  });
  const socket = new MockSocket();

  pool.handleConnection(socket.asWebSocket(), 'manual-session');
  await waitForMessage(socket, 'ready');
  engine.writes.length = 0;
  socket.sent.length = 0;

  return { pool, engine, socket };
}

function parsePayload(payload: unknown) {
  return parseClientMessage(Buffer.from(JSON.stringify(payload)));
}

function opponentSearch(overrides: Record<string, unknown> = {}): Record<string, unknown> {
  return {
    type: 'move',
    requestId: 1,
    purpose: 'opponent',
    difficulty: 'standard',
    fen: DEFAULT_START_FEN,
    moves: [],
    ...overrides,
  };
}

function analysisSearch(overrides: Record<string, unknown> = {}): Record<string, unknown> {
  return {
    type: 'analyze',
    requestId: 1,
    purpose: 'analysis',
    fen: DEFAULT_START_FEN,
    moves: [],
    ...overrides,
  };
}

function assertInvalid(payload: unknown, code: string): void {
  const result = parsePayload(payload);
  assert.equal(result.ok, false);
  if (!result.ok) {
    assert.equal(result.error.code, code);
  }
}

function assertValid(payload: unknown): void {
  const result = parsePayload(payload);
  assert.equal(result.ok, true);
}

function testJsonBoundary(): void {
  assertValid({ type: 'newgame' });

  const invalidJson = parseClientMessage(Buffer.from('{bad'));
  assert.equal(invalidJson.ok, false);
  if (!invalidJson.ok) assert.equal(invalidJson.error.code, 'INVALID_JSON');

  assertInvalid(null, 'INVALID_MESSAGE');
  assertInvalid([], 'INVALID_MESSAGE');
  assertInvalid('stop', 'INVALID_MESSAGE');
  assertInvalid({ type: 'unknown' }, 'UNKNOWN_MESSAGE_TYPE');

  const oversized = parseClientMessage(Buffer.from(JSON.stringify({ type: 'newgame', pad: 'x'.repeat(20_000) })));
  assert.equal(oversized.ok, false);
  if (!oversized.ok) assert.equal(oversized.error.code, 'MESSAGE_TOO_LARGE');
}

function testFenValidation(): void {
  assertValid({ type: 'position', fen: DEFAULT_START_FEN, moves: [] });
  assertInvalid({ type: 'position', fen: '8/8/8/8/8/8/8/8 w - - 0', moves: [] }, 'INVALID_FEN');
  assertInvalid({ type: 'position', fen: '8/8/8/8/8/8/8/8 x - - 0 1', moves: [] }, 'INVALID_FEN');
  assertInvalid({ type: 'position', fen: `${DEFAULT_START_FEN}\n`, moves: [] }, 'INVALID_FEN');
  assertInvalid({ type: 'position', fen: `${DEFAULT_START_FEN} ${'x'.repeat(300)}`, moves: [] }, 'INVALID_FEN');
}

function testMoveValidation(): void {
  assertValid(opponentSearch({ moves: ['e2e4', 'e7e8q'] }));
  assertInvalid(opponentSearch({ moves: ['i2e4'] }), 'INVALID_MOVE');
  assertInvalid(opponentSearch({ moves: ['e7e8x'] }), 'INVALID_MOVE');
  assertInvalid(opponentSearch({ moves: [42] }), 'INVALID_MOVE');
  assertInvalid(opponentSearch({ moves: Array.from({ length: 513 }, () => 'e2e4') }), 'INVALID_MOVE');
  assertInvalid(opponentSearch({ moves: ['e2e4\nstop'] }), 'INVALID_MOVE');
}

function testRequestIdValidation(): void {
  assertInvalid(opponentSearch({ requestId: undefined }), 'INVALID_MESSAGE');
  assertInvalid(analysisSearch({ requestId: undefined }), 'INVALID_MESSAGE');
  assertValid(opponentSearch({ requestId: 7 }));
  assertValid(analysisSearch({ requestId: 8, depth: 1, multiPv: 1 }));
  assertValid({ type: 'stop', requestId: 9 });
  assertInvalid(opponentSearch({ requestId: 0 }), 'INVALID_MESSAGE');
  assertInvalid(opponentSearch({ requestId: -1 }), 'INVALID_MESSAGE');
  assertInvalid(opponentSearch({ requestId: 1.5 }), 'INVALID_MESSAGE');
  assertInvalid(opponentSearch({ requestId: '1' }), 'INVALID_MESSAGE');
  assertInvalid(opponentSearch({ requestId: Number.MAX_SAFE_INTEGER + 1 }), 'INVALID_MESSAGE');
}

function testNumericValidation(): void {
  assertValid({
    ...opponentSearch({ moves: ['e2e4'] }),
    wtime: 180000,
    btime: 180000,
    winc: 2000,
    binc: 2000,
    depth: 10,
    multiPv: 3,
  });

  assertInvalid(opponentSearch({ depth: -1 }), 'INVALID_NUMBER');
  assertInvalid(opponentSearch({ depth: 0 }), 'INVALID_NUMBER');
  assertInvalid(opponentSearch({ depth: 1.5 }), 'INVALID_NUMBER');
  assertInvalid(opponentSearch({ depth: 65 }), 'INVALID_NUMBER');

  const nanResult = validateClientMessage(opponentSearch({ depth: NaN }));
  assert.equal(nanResult.ok, false);
  if (!nanResult.ok) assert.equal(nanResult.error.code, 'INVALID_NUMBER');

  const infinityResult = validateClientMessage(opponentSearch({ wtime: Infinity }));
  assert.equal(infinityResult.ok, false);
  if (!infinityResult.ok) assert.equal(infinityResult.error.code, 'INVALID_NUMBER');
}

function testDifficultyAndPurposeValidation(): void {
  assertValid(opponentSearch({ difficulty: 'blitz' }));
  assertValid(opponentSearch({ difficulty: 'standard' }));
  assertValid(opponentSearch({ difficulty: 'deep' }));
  assertInvalid(opponentSearch({ difficulty: undefined }), 'INVALID_MESSAGE');
  assertInvalid(opponentSearch({ difficulty: 'easy' }), 'INVALID_MESSAGE');
  assertInvalid(opponentSearch({ purpose: 'analysis' }), 'INVALID_MESSAGE');
  assertValid(analysisSearch({ purpose: 'analysis' }));
  assertValid(analysisSearch({ purpose: 'training-root-review' }));
  assertValid(analysisSearch({ purpose: 'training-result-review' }));
  assertValid(analysisSearch({ purpose: 'training-hint' }));
  assertInvalid(analysisSearch({ purpose: 'opponent' }), 'INVALID_MESSAGE');
  assertInvalid(analysisSearch({ purpose: 'review' }), 'INVALID_MESSAGE');
  assertInvalid(analysisSearch({ difficulty: 'blitz' }), 'INVALID_MESSAGE');
}

function testOptionValidation(): void {
  assertValid({ type: 'setoption', name: 'OwnBook', value: true });
  assertValid({ type: 'setoption', name: 'Hash', value: 32 });
  assertInvalid({ type: 'setoption', name: 'Threads', value: 4 }, 'INVALID_OPTION');
  assertInvalid({ type: 'setoption', name: 'Hash', value: 0 }, 'INVALID_OPTION');
  assertInvalid({ type: 'setoption', name: 'OwnBook', value: 'true\n' }, 'INVALID_OPTION');
  assertInvalid({ type: 'setoption', name: 'OwnBook\n', value: true }, 'INVALID_OPTION');
}

function testUciBuilders(): void {
  assert.equal(
    buildPositionCommand(DEFAULT_START_FEN, ['e2e4']),
    'position startpos moves e2e4',
  );
  assert.equal(
    buildPositionCommand('8/8/8/8/8/8/8/K6k w - - 0 1', []),
    'position fen 8/8/8/8/8/8/8/K6k w - - 0 1',
  );
  assert.equal(
    buildGoCommand({ wtime: 180000, btime: 180000, winc: 2000, binc: 2000 }),
    'go wtime 180000 btime 180000 winc 2000 binc 2000',
  );
  assert.equal(
    buildGoCommand({ depth: 10, wtime: 0, btime: 0, winc: 0, binc: 0 }),
    'go depth 10',
  );
  assert.equal(
    buildGoCommand({ movetimeMs: 1000, wtime: 0, btime: 0, winc: 0, binc: 0 }),
    'go movetime 1000',
  );
  assert.equal(buildSetOptionCommand('OwnBook', false), 'setoption name OwnBook value false');
}

async function testInvalidInputDoesNotWriteToEngine(): Promise<void> {
  const engine = new RecordingEngineProcess();
  const spawnOptions: SpawnOptions[] = [];
  const pool = new EnginePoolManager({
    maxConcurrent: 1,
    enginePath: process.execPath,
    idleTimeoutMs: 30_000,
    sessionMaxMs: 30_000,
    spawnEngineProcess: (_enginePath, _args, options) => {
      spawnOptions.push(options ?? {});
      return engine.asChildProcess();
    },
  });
  const socket = new MockSocket();

  pool.handleConnection(socket.asWebSocket(), 'session');
  await waitForMessage(socket, 'ready');
  assert.equal(spawnOptions[0].cwd, path.dirname(process.execPath));
  engine.writes.length = 0;

  socket.emitRawMessage('{bad');
  await waitForMessage(socket, 'error');
  assert.equal(socket.sent[socket.sent.length - 1].code, 'INVALID_JSON');
  assert.deepEqual(engine.writes, []);

  socket.emitRawMessage(JSON.stringify({ type: 'position', fen: `${DEFAULT_START_FEN}\n`, moves: [] }));
  await new Promise(resolve => setTimeout(resolve, 20));
  assert.equal(socket.sent[socket.sent.length - 1].code, 'INVALID_FEN');
  assert.deepEqual(engine.writes, []);

  socket.emitRawMessage(JSON.stringify({ type: 'position', fen: DEFAULT_START_FEN, moves: ['e2e4'] }));
  await new Promise(resolve => setTimeout(resolve, 20));
  assert.deepEqual(engine.writes, ['position startpos moves e2e4']);

  engine.writes.length = 0;
  socket.emitRawMessage(JSON.stringify(opponentSearch({ requestId: 1, moves: ['bad'] })));
  socket.emitRawMessage(JSON.stringify(opponentSearch({ requestId: 2, moves: ['e2e4'], difficulty: 'deep', multiPv: 1 })));
  await new Promise(resolve => setTimeout(resolve, 20));
  assert.deepEqual(engine.writes, [
    'setoption name MultiPV value 1',
    'position startpos moves e2e4',
    'go depth 8',
  ]);

  socket.close();
}

async function testInfoCarriesRequestId(): Promise<void> {
  const { engine, socket } = await createManualSession();

  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 10, depth: 2, multiPv: 1 })));
  engine.emitInfo();
  engine.emitInfo('info depth 2 score cp 20 nodes 2 time 2 pv d2d4');

  const infos = messagesOfType(socket, 'info');
  assert.equal(infos.length, 2);
  assert.equal(infos[0].requestId, 10);
  assert.equal(infos[1].requestId, 10);

  socket.close();
}

async function testStopSwallowsTrailingBestMove(): Promise<void> {
  const { engine, socket } = await createManualSession();

  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 1, depth: 2, multiPv: 1 })));
  engine.emitInfo();
  socket.emitRawMessage(JSON.stringify({ type: 'stop', requestId: 1 }));
  engine.emitBestMove('bestmove e2e4');
  await flushEvents();

  assert.equal(messagesOfType(socket, 'info').length, 1);
  assert.equal(messagesOfType(socket, 'bestmove').length, 0);

  socket.close();
}

async function testNewerSearchSupersedesOlderSearch(): Promise<void> {
  const { engine, socket } = await createManualSession();

  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 1, depth: 2, multiPv: 1 })));
  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 2, moves: ['e2e4'], depth: 2, multiPv: 1 })));

  engine.emitInfo('info depth 1 score cp 11 nodes 1 time 1 pv e2e4');
  engine.emitBestMove('bestmove e2e4');
  await flushEvents();
  engine.emitInfo('info depth 1 score cp 22 nodes 1 time 1 pv e7e5');
  engine.emitBestMove('bestmove e7e5');
  await flushEvents();

  const infos = messagesOfType(socket, 'info');
  const bestMoves = messagesOfType(socket, 'bestmove');
  assert.equal(infos.length, 1);
  assert.equal(infos[0].requestId, 2);
  assert.equal(bestMoves.length, 1);
  assert.equal(bestMoves[0].requestId, 2);
  assert.equal(bestMoves[0].move, 'e7e5');

  socket.close();
}

async function testTerminalBestMoveCarriesClassification(): Promise<void> {
  const checkmate = await createManualSession();
  checkmate.socket.emitRawMessage(JSON.stringify(analysisSearch({
    requestId: 90,
    fen: '7k/6Q1/7K/8/8/8/8/8 b - - 0 1',
    depth: 2,
    multiPv: 1,
  })));
  checkmate.engine.emitBestMove('bestmove 0000');
  await flushEvents();
  const checkmateBest = messagesOfType(checkmate.socket, 'bestmove')[0];
  assert.equal(checkmateBest.requestId, 90);
  assert.equal(checkmateBest.move, null);
  assert.deepEqual(checkmateBest.terminal, { reason: 'checkmate', winner: 'white' });
  checkmate.socket.close();

  const stalemate = await createManualSession();
  stalemate.socket.emitRawMessage(JSON.stringify(analysisSearch({
    requestId: 91,
    fen: '8/8/8/8/8/5kq1/8/7K w - - 0 1',
    depth: 2,
    multiPv: 1,
  })));
  stalemate.engine.emitBestMove('bestmove 0000');
  await flushEvents();
  const stalemateBest = messagesOfType(stalemate.socket, 'bestmove')[0];
  assert.equal(stalemateBest.move, null);
  assert.deepEqual(stalemateBest.terminal, { reason: 'stalemate' });
  stalemate.socket.close();

  const draw = await createManualSession();
  draw.socket.emitRawMessage(JSON.stringify(analysisSearch({
    requestId: 92,
    fen: '8/8/8/8/8/8/8/K6k w - - 0 1',
    depth: 2,
    multiPv: 1,
  })));
  draw.engine.emitBestMove('bestmove 0000');
  await flushEvents();
  const drawBest = messagesOfType(draw.socket, 'bestmove')[0];
  assert.equal(drawBest.move, null);
  assert.deepEqual(drawBest.terminal, { reason: 'draw' });
  draw.socket.close();
}

async function testTerminalBestMoveClearsSearchAndLaterRequestWorks(): Promise<void> {
  const { engine, socket } = await createManualSession();

  socket.emitRawMessage(JSON.stringify(analysisSearch({
    requestId: 93,
    fen: '8/8/8/8/8/5kq1/8/7K w - - 0 1',
    depth: 2,
    multiPv: 1,
  })));
  engine.emitBestMove('bestmove 0000');
  await flushEvents();

  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 94, depth: 2, multiPv: 1 })));
  engine.emitBestMove('bestmove e2e4');
  await flushEvents();

  const bestMoves = messagesOfType(socket, 'bestmove');
  assert.equal(bestMoves.length, 2);
  assert.equal(bestMoves[0].requestId, 93);
  assert.equal(bestMoves[0].move, null);
  assert.equal(bestMoves[1].requestId, 94);
  assert.equal(bestMoves[1].move, 'e2e4');
  assert.equal(bestMoves.some(message => message.move === '0000'), false);

  socket.close();
}

async function testStaleTerminalBestMoveIsSwallowed(): Promise<void> {
  const { engine, socket } = await createManualSession();

  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 95, depth: 2, multiPv: 1 })));
  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 96, moves: ['e2e4'], depth: 2, multiPv: 1 })));
  engine.emitBestMove('bestmove 0000');
  await flushEvents();
  engine.emitBestMove('bestmove e7e5');
  await flushEvents();

  const bestMoves = messagesOfType(socket, 'bestmove');
  assert.equal(bestMoves.length, 1);
  assert.equal(bestMoves[0].requestId, 96);
  assert.equal(bestMoves[0].move, 'e7e5');

  socket.close();
}

async function testNewGameSwallowsOldBestMove(): Promise<void> {
  const { engine, socket } = await createManualSession();

  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 1, depth: 2, multiPv: 1 })));
  socket.emitRawMessage(JSON.stringify({ type: 'newgame' }));
  engine.emitBestMove('bestmove e2e4');
  await flushEvents();

  assert.equal(messagesOfType(socket, 'bestmove').length, 0);
  assert.ok(engine.writes.includes('ucinewgame'));
  assert.ok(engine.writes.includes('isready'));

  socket.close();
}

async function testReleaseSessionIgnoresProcessOutput(): Promise<void> {
  const { engine, socket } = await createManualSession();

  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 1, depth: 2, multiPv: 1 })));
  socket.emitRawMessage(JSON.stringify({ type: 'releaseSession' }));
  const sentBeforeOutput = socket.sent.length;
  engine.emitInfo();
  engine.emitBestMove('bestmove e2e4');
  await flushEvents();

  assert.equal(socket.sent.length, sentBeforeOutput);
}

async function testRapidAnalyzeReplacementUsesLatestRequest(): Promise<void> {
  const { engine, socket } = await createManualSession();

  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 1, depth: 2, multiPv: 1 })));
  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 2, moves: ['e2e4'], depth: 2, multiPv: 1 })));
  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 3, moves: ['d2d4'], depth: 2, multiPv: 1 })));

  engine.emitBestMove('bestmove e2e4');
  await flushEvents();
  engine.emitInfo('info depth 1 score cp 33 nodes 1 time 1 pv d7d5');
  engine.emitBestMove('bestmove d7d5');

  const infos = messagesOfType(socket, 'info');
  const bestMoves = messagesOfType(socket, 'bestmove');
  assert.equal(infos.length, 1);
  assert.equal(infos[0].requestId, 3);
  assert.equal(bestMoves.length, 1);
  assert.equal(bestMoves[0].requestId, 3);

  socket.close();
}

async function testSetOptionUsesActiveSessionBeforeSearch(): Promise<void> {
  const { engine, socket } = await createManualSession();

  socket.emitRawMessage(JSON.stringify({ type: 'setoption', name: 'OwnBook', value: false }));
  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 20, depth: 2, multiPv: 1 })));
  await flushEvents();

  assert.deepEqual(engine.writes.slice(0, 4), [
    'setoption name OwnBook value false',
    'setoption name MultiPV value 1',
    'position startpos',
    'go depth 2',
  ]);

  engine.emitInfo('info depth 1 score cp 20 nodes 1 time 1 pv e2e4');
  engine.emitBestMove('bestmove e2e4');
  await flushEvents();
  assert.equal(messagesOfType(socket, 'bestmove')[0].requestId, 20);

  socket.close();
}

async function testSetOptionDoesNotLeakAcrossClients(): Promise<void> {
  const firstEngine = new ManualEngineProcess();
  const secondEngine = new ManualEngineProcess();
  const engines = [firstEngine, secondEngine];
  let spawnIndex = 0;
  const pool = new EnginePoolManager({
    maxConcurrent: 2,
    enginePath: process.execPath,
    idleTimeoutMs: 30_000,
    sessionMaxMs: 30_000,
    spawnEngineProcess: () => {
      const engine = engines[spawnIndex];
      spawnIndex += 1;
      if (!engine) throw new Error('unexpected engine spawn');
      return engine.asChildProcess();
    },
  });
  const firstSocket = new MockSocket();
  const secondSocket = new MockSocket();

  pool.handleConnection(firstSocket.asWebSocket(), 'first');
  pool.handleConnection(secondSocket.asWebSocket(), 'second');
  await waitForMessage(firstSocket, 'ready');
  await waitForMessage(secondSocket, 'ready');
  firstEngine.writes.length = 0;
  secondEngine.writes.length = 0;

  firstSocket.emitRawMessage(JSON.stringify({ type: 'setoption', name: 'OwnBook', value: false }));
  secondSocket.emitRawMessage(JSON.stringify({ type: 'setoption', name: 'OwnBook', value: true }));
  firstSocket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 40, depth: 2, multiPv: 1 })));
  await flushEvents();

  assert.ok(firstEngine.writes.includes('setoption name OwnBook value false'));
  assert.ok(firstEngine.writes.includes('go depth 2'));
  assert.equal(firstEngine.writes.includes('setoption name OwnBook value true'), false);
  assert.deepEqual(secondEngine.writes, ['setoption name OwnBook value true']);

  firstSocket.close();
  secondSocket.close();
}

async function testSetOptionQueuesBehindActiveSearch(): Promise<void> {
  const { engine, socket } = await createManualSession();

  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 30, depth: 3, multiPv: 1 })));
  socket.emitRawMessage(JSON.stringify({ type: 'setoption', name: 'OwnBook', value: false }));
  await flushEvents();

  assert.ok(engine.writes.includes('stop'));
  assert.equal(engine.writes.includes('setoption name OwnBook value false'), false);

  engine.emitBestMove('bestmove e2e4');
  await flushEvents();

  assert.ok(engine.writes.includes('setoption name OwnBook value false'));
  assert.equal(messagesOfType(socket, 'bestmove').length, 0);

  socket.emitRawMessage(JSON.stringify(analysisSearch({ requestId: 31, depth: 2, multiPv: 1 })));
  engine.emitBestMove('bestmove d2d4');
  await flushEvents();

  assert.equal(messagesOfType(socket, 'bestmove')[0].requestId, 31);
  socket.close();
}

async function testNewGameDoesNotResetOwnBookOption(): Promise<void> {
  const { engine, socket } = await createManualSession();

  socket.emitRawMessage(JSON.stringify({ type: 'setoption', name: 'OwnBook', value: false }));
  socket.emitRawMessage(JSON.stringify({ type: 'newgame' }));
  await flushEvents();

  const ownBookWrites = engine.writes.filter(line => line.startsWith('setoption name OwnBook'));
  assert.deepEqual(ownBookWrites, ['setoption name OwnBook value false']);
  assert.ok(engine.writes.includes('ucinewgame'));

  socket.close();
}

async function testDifficultyProfilesGenerateDistinctOpponentCommands(): Promise<void> {
  const blitz = await createManualSession();
  blitz.socket.emitRawMessage(JSON.stringify(opponentSearch({ requestId: 50, difficulty: 'blitz', multiPv: 3 })));
  await flushEvents();
  assert.deepEqual(blitz.engine.writes.slice(0, 3), [
    'setoption name MultiPV value 1',
    'position startpos',
    'go movetime 1000',
  ]);
  blitz.socket.close();

  const standard = await createManualSession();
  standard.socket.emitRawMessage(JSON.stringify(opponentSearch({ requestId: 51, difficulty: 'standard', multiPv: 3 })));
  await flushEvents();
  assert.deepEqual(standard.engine.writes.slice(0, 3), [
    'setoption name MultiPV value 1',
    'position startpos',
    'go movetime 3000',
  ]);
  standard.socket.close();

  const deep = await createManualSession();
  deep.socket.emitRawMessage(JSON.stringify(opponentSearch({ requestId: 52, difficulty: 'deep', multiPv: 3 })));
  await flushEvents();
  assert.deepEqual(deep.engine.writes.slice(0, 3), [
    'setoption name MultiPV value 1',
    'position startpos',
    'go depth 8',
  ]);
  deep.socket.close();
}

async function testReviewAndAnalysisProfilesGenerateStableCommands(): Promise<void> {
  const { engine, socket } = await createManualSession();

  socket.emitRawMessage(JSON.stringify(analysisSearch({
    requestId: 60,
    purpose: 'training-root-review',
    depth: 11,
    multiPv: 3,
  })));
  await flushEvents();
  assert.deepEqual(engine.writes.slice(0, 3), [
    'setoption name MultiPV value 3',
    'position startpos',
    'go depth 11',
  ]);

  engine.emitBestMove('bestmove e2e4');
  await flushEvents();
  engine.writes.length = 0;

  socket.emitRawMessage(JSON.stringify(analysisSearch({
    requestId: 61,
    purpose: 'training-result-review',
    depth: 11,
    multiPv: 3,
    moves: ['e2e4'],
  })));
  await flushEvents();
  assert.deepEqual(engine.writes.slice(0, 3), [
    'setoption name MultiPV value 3',
    'position startpos moves e2e4',
    'go depth 11',
  ]);

  engine.emitBestMove('bestmove e7e5');
  await flushEvents();
  engine.writes.length = 0;

  socket.emitRawMessage(JSON.stringify(analysisSearch({
    requestId: 62,
    purpose: 'analysis',
    depth: 7,
    multiPv: 2,
  })));
  await flushEvents();
  assert.deepEqual(engine.writes.slice(0, 3), [
    'setoption name MultiPV value 2',
    'position startpos',
    'go depth 7',
  ]);

  engine.emitBestMove('bestmove d2d4');
  await flushEvents();
  engine.writes.length = 0;

  socket.emitRawMessage(JSON.stringify(analysisSearch({
    requestId: 63,
    purpose: 'training-hint',
    depth: 11,
    multiPv: 1,
  })));
  await flushEvents();
  assert.deepEqual(engine.writes.slice(0, 3), [
    'setoption name MultiPV value 1',
    'position startpos',
    'go depth 11',
  ]);

  socket.close();
}

async function testClockBasedOpponentKeepsClockCommand(): Promise<void> {
  const { engine, socket } = await createManualSession();

  socket.emitRawMessage(JSON.stringify(opponentSearch({
    requestId: 70,
    difficulty: 'blitz',
    wtime: 180000,
    btime: 180000,
    winc: 2000,
    binc: 2000,
    multiPv: 3,
  })));
  await flushEvents();

  assert.deepEqual(engine.writes.slice(0, 3), [
    'setoption name MultiPV value 3',
    'position startpos',
    'go wtime 180000 btime 180000 winc 2000 binc 2000',
  ]);

  socket.close();
}

async function testDifficultyChangeAffectsNextSearchOnly(): Promise<void> {
  const { engine, socket } = await createManualSession();

  socket.emitRawMessage(JSON.stringify(opponentSearch({ requestId: 80, difficulty: 'blitz' })));
  socket.emitRawMessage(JSON.stringify(opponentSearch({ requestId: 81, difficulty: 'deep' })));
  await flushEvents();

  assert.ok(engine.writes.includes('go movetime 1000'));
  assert.ok(engine.writes.includes('stop'));
  assert.equal(engine.writes.includes('go depth 8'), false);

  engine.emitBestMove('bestmove e2e4');
  await flushEvents();

  assert.ok(engine.writes.includes('go depth 8'));
  assert.equal(messagesOfType(socket, 'bestmove').length, 0);

  engine.emitBestMove('bestmove d2d4');
  await flushEvents();
  assert.equal(messagesOfType(socket, 'bestmove')[0].requestId, 81);

  socket.close();
}

async function run(): Promise<void> {
  testJsonBoundary();
  testFenValidation();
  testMoveValidation();
  testRequestIdValidation();
  testNumericValidation();
  testDifficultyAndPurposeValidation();
  testOptionValidation();
  testUciBuilders();
  await testInvalidInputDoesNotWriteToEngine();
  await testInfoCarriesRequestId();
  await testStopSwallowsTrailingBestMove();
  await testNewerSearchSupersedesOlderSearch();
  await testTerminalBestMoveCarriesClassification();
  await testTerminalBestMoveClearsSearchAndLaterRequestWorks();
  await testStaleTerminalBestMoveIsSwallowed();
  await testNewGameSwallowsOldBestMove();
  await testReleaseSessionIgnoresProcessOutput();
  await testRapidAnalyzeReplacementUsesLatestRequest();
  await testSetOptionUsesActiveSessionBeforeSearch();
  await testSetOptionDoesNotLeakAcrossClients();
  await testSetOptionQueuesBehindActiveSearch();
  await testNewGameDoesNotResetOwnBookOption();
  await testDifficultyProfilesGenerateDistinctOpponentCommands();
  await testReviewAndAnalysisProfilesGenerateStableCommands();
  await testClockBasedOpponentKeepsClockCommand();
  await testDifficultyChangeAffectsNextSearchOnly();

  console.log('engine-session protocol tests passed');
}

run().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
