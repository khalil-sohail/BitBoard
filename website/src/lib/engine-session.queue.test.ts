import { EventEmitter } from 'events';
import { ChildProcess } from 'child_process';
import { PassThrough } from 'stream';
import { strict as assert } from 'assert';
import { WebSocket } from 'ws';
import { EnginePoolManager } from './engine-session';

interface ServerMessage {
  type?: string;
  position?: number;
  message?: string;
}

class MockSocket extends EventEmitter {
  public readyState: number = WebSocket.OPEN;
  public readonly sent: ServerMessage[] = [];

  public send(payload: string): void {
    if (this.readyState !== WebSocket.OPEN) {
      throw new Error('Cannot send to closed socket');
    }

    const parsed = JSON.parse(payload) as ServerMessage;
    this.sent.push(parsed);
    this.emit('sent', parsed);
  }

  public close(): void {
    if (this.readyState === WebSocket.CLOSED) {
      return;
    }

    this.readyState = WebSocket.CLOSED;
    this.emit('close');
  }

  public markClosedWithoutEvent(): void {
    this.readyState = WebSocket.CLOSED;
  }

  public messagesOfType(type: string): ServerMessage[] {
    return this.sent.filter(message => message.type === type);
  }

  public asWebSocket(): WebSocket {
    return this as unknown as WebSocket;
  }
}

class FakeEngineProcess extends EventEmitter {
  public readonly stdin = new PassThrough();
  public readonly stdout = new PassThrough();
  public readonly stderr = new PassThrough();
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

function createPool(): EnginePoolManager {
  return new EnginePoolManager({
    maxConcurrent: 1,
    enginePath: process.execPath,
    idleTimeoutMs: 30_000,
    sessionMaxMs: 30_000,
    spawnEngineProcess: () => new FakeEngineProcess().asChildProcess(),
  });
}

function lastMessageOfType(socket: MockSocket, type: string): ServerMessage | undefined {
  const messages = socket.messagesOfType(type);
  return messages[messages.length - 1];
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

async function cleanupSockets(...sockets: MockSocket[]): Promise<void> {
  sockets.forEach(socket => socket.close());
  await new Promise(resolve => setTimeout(resolve, 20));
}

async function testDisconnectWhileQueued(): Promise<void> {
  const pool = createPool();
  const active = new MockSocket();
  const queued = new MockSocket();
  const next = new MockSocket();

  pool.handleConnection(active.asWebSocket(), 'active');
  await waitForMessage(active, 'ready');

  pool.handleConnection(queued.asWebSocket(), 'queued');
  assert.equal(lastMessageOfType(queued, 'queued')?.position, 1);

  queued.close();
  assert.deepEqual(pool.getDebugState().queuedIds, []);

  pool.handleConnection(next.asWebSocket(), 'next');
  assert.equal(lastMessageOfType(next, 'queued')?.position, 1);

  active.close();
  await waitForMessage(next, 'ready');

  assert.equal(queued.messagesOfType('ready').length, 0);
  assert.deepEqual(pool.getDebugState().activeIds, ['next']);

  await cleanupSockets(next);
}

async function testQueuePositionsUpdate(): Promise<void> {
  const pool = createPool();
  const active = new MockSocket();
  const first = new MockSocket();
  const middle = new MockSocket();
  const last = new MockSocket();

  pool.handleConnection(active.asWebSocket(), 'active');
  await waitForMessage(active, 'ready');

  pool.handleConnection(first.asWebSocket(), 'first');
  pool.handleConnection(middle.asWebSocket(), 'middle');
  pool.handleConnection(last.asWebSocket(), 'last');

  assert.equal(lastMessageOfType(first, 'queued')?.position, 1);
  assert.equal(lastMessageOfType(middle, 'queued')?.position, 2);
  assert.equal(lastMessageOfType(last, 'queued')?.position, 3);

  middle.close();

  assert.deepEqual(pool.getDebugState().queuedIds, ['first', 'last']);
  assert.equal(lastMessageOfType(first, 'queued')?.position, 1);
  assert.equal(lastMessageOfType(last, 'queued')?.position, 2);

  active.close();
  await waitForMessage(first, 'ready');
  assert.equal(lastMessageOfType(last, 'queued')?.position, 1);

  await cleanupSockets(first, last);
}

async function testDeadSocketSkippedDuringAssignment(): Promise<void> {
  const pool = createPool();
  const active = new MockSocket();
  const dead = new MockSocket();
  const next = new MockSocket();

  pool.handleConnection(active.asWebSocket(), 'active');
  await waitForMessage(active, 'ready');

  pool.handleConnection(dead.asWebSocket(), 'dead');
  pool.handleConnection(next.asWebSocket(), 'next');
  dead.markClosedWithoutEvent();

  active.close();
  await waitForMessage(next, 'ready');

  assert.equal(dead.messagesOfType('ready').length, 0);
  assert.deepEqual(pool.getDebugState().activeIds, ['next']);
  assert.deepEqual(pool.getDebugState().queuedIds, []);

  await cleanupSockets(next);
}

async function testActiveCloseDoesNotRunWaitingCleanup(): Promise<void> {
  const pool = createPool();
  const active = new MockSocket();
  const assigned = new MockSocket();
  const waiting = new MockSocket();

  pool.handleConnection(active.asWebSocket(), 'active');
  await waitForMessage(active, 'ready');

  pool.handleConnection(assigned.asWebSocket(), 'assigned');
  pool.handleConnection(waiting.asWebSocket(), 'waiting');

  active.close();
  await waitForMessage(assigned, 'ready');

  assert.deepEqual(pool.getDebugState().queuedIds, ['waiting']);

  assigned.close();
  await waitForMessage(waiting, 'ready');

  assert.deepEqual(pool.getDebugState().activeIds, ['waiting']);
  assert.deepEqual(pool.getDebugState().queuedIds, []);

  await cleanupSockets(waiting);
}

async function testIdempotentRemoval(): Promise<void> {
  const pool = createPool();
  const active = new MockSocket();
  const queued = new MockSocket();

  pool.handleConnection(active.asWebSocket(), 'active');
  await waitForMessage(active, 'ready');

  pool.handleConnection(queued.asWebSocket(), 'queued');
  queued.close();
  queued.close();

  assert.deepEqual(pool.getDebugState().queuedIds, []);

  active.close();
  assert.deepEqual(pool.getDebugState().activeIds, []);

  await cleanupSockets();
}

async function testImmediateDisconnectAroundEnqueue(): Promise<void> {
  const pool = createPool();
  const active = new MockSocket();
  const queued = new MockSocket();

  pool.handleConnection(active.asWebSocket(), 'active');
  await waitForMessage(active, 'ready');

  pool.handleConnection(queued.asWebSocket(), 'queued');
  queued.close();

  assert.deepEqual(pool.getDebugState().queuedIds, []);

  active.close();
  assert.deepEqual(pool.getDebugState().activeIds, []);
}

async function run(): Promise<void> {
  await testDisconnectWhileQueued();
  await testQueuePositionsUpdate();
  await testDeadSocketSkippedDuringAssignment();
  await testActiveCloseDoesNotRunWaitingCleanup();
  await testIdempotentRemoval();
  await testImmediateDisconnectAroundEnqueue();

  console.log('engine-session queue tests passed');
}

run().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
