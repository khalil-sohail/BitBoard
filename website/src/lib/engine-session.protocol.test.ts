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

function parsePayload(payload: unknown) {
  return parseClientMessage(Buffer.from(JSON.stringify(payload)));
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
  assertValid({ type: 'move', fen: DEFAULT_START_FEN, moves: ['e2e4', 'e7e8q'] });
  assertInvalid({ type: 'move', fen: DEFAULT_START_FEN, moves: ['i2e4'] }, 'INVALID_MOVE');
  assertInvalid({ type: 'move', fen: DEFAULT_START_FEN, moves: ['e7e8x'] }, 'INVALID_MOVE');
  assertInvalid({ type: 'move', fen: DEFAULT_START_FEN, moves: [42] }, 'INVALID_MOVE');
  assertInvalid({ type: 'move', fen: DEFAULT_START_FEN, moves: Array.from({ length: 513 }, () => 'e2e4') }, 'INVALID_MOVE');
  assertInvalid({ type: 'move', fen: DEFAULT_START_FEN, moves: ['e2e4\nstop'] }, 'INVALID_MOVE');
}

function testNumericValidation(): void {
  assertValid({
    type: 'move',
    fen: DEFAULT_START_FEN,
    moves: ['e2e4'],
    wtime: 180000,
    btime: 180000,
    winc: 2000,
    binc: 2000,
    depth: 10,
    multiPv: 3,
  });

  assertInvalid({ type: 'move', fen: DEFAULT_START_FEN, moves: [], depth: -1 }, 'INVALID_NUMBER');
  assertInvalid({ type: 'move', fen: DEFAULT_START_FEN, moves: [], depth: 0 }, 'INVALID_NUMBER');
  assertInvalid({ type: 'move', fen: DEFAULT_START_FEN, moves: [], depth: 1.5 }, 'INVALID_NUMBER');
  assertInvalid({ type: 'move', fen: DEFAULT_START_FEN, moves: [], depth: 65 }, 'INVALID_NUMBER');

  const nanResult = validateClientMessage({ type: 'move', fen: DEFAULT_START_FEN, moves: [], depth: NaN });
  assert.equal(nanResult.ok, false);
  if (!nanResult.ok) assert.equal(nanResult.error.code, 'INVALID_NUMBER');

  const infinityResult = validateClientMessage({ type: 'move', fen: DEFAULT_START_FEN, moves: [], wtime: Infinity });
  assert.equal(infinityResult.ok, false);
  if (!infinityResult.ok) assert.equal(infinityResult.error.code, 'INVALID_NUMBER');
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
  socket.emitRawMessage(JSON.stringify({ type: 'move', fen: DEFAULT_START_FEN, moves: ['bad'] }));
  socket.emitRawMessage(JSON.stringify({ type: 'move', fen: DEFAULT_START_FEN, moves: ['e2e4'], depth: 1, multiPv: 1 }));
  await new Promise(resolve => setTimeout(resolve, 20));
  assert.deepEqual(engine.writes, [
    'setoption name MultiPV value 1',
    'position startpos moves e2e4',
    'go depth 1',
  ]);

  socket.close();
}

async function run(): Promise<void> {
  testJsonBoundary();
  testFenValidation();
  testMoveValidation();
  testNumericValidation();
  testOptionValidation();
  testUciBuilders();
  await testInvalidInputDoesNotWriteToEngine();

  console.log('engine-session protocol tests passed');
}

run().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
