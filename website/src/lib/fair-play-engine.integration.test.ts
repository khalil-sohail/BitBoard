import { strict as assert } from 'assert';
import { EventEmitter } from 'events';
import fs from 'fs';
import path from 'path';
import { WebSocket } from 'ws';
import { EnginePoolManager } from './engine-session';
import { validateEngineMove } from './fair-play-engine';

const REPORTED_FEN = 'r2r2k1/p1R2pp1/bp2p3/6N1/3P4/P3P3/1n3PPP/5RK1 w - - 2 20';

interface Message { type?: string; [key: string]: unknown }

class IntegrationSocket extends EventEmitter {
  public readyState: number = WebSocket.OPEN;
  public messages: Message[] = [];
  public send(payload: string): void {
    const message = JSON.parse(payload) as Message;
    this.messages.push(message);
    this.emit('sent', message);
  }
  public request(message: object): void { this.emit('message', Buffer.from(JSON.stringify(message))); }
  public close(): void { this.readyState = WebSocket.CLOSED; this.emit('close'); }
  public asWebSocket(): WebSocket { return this as unknown as WebSocket; }
}

function waitFor(socket: IntegrationSocket, type: string, timeoutMs = 10_000): Promise<Message> {
  const existing = socket.messages.find(message => message.type === type);
  if (existing) return Promise.resolve(existing);
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => { socket.off('sent', listener); reject(new Error(`Timed out waiting for ${type}: ${JSON.stringify(socket.messages)}`)); }, timeoutMs);
    const listener = (message: Message) => {
      if (message.type !== type) return;
      clearTimeout(timeout);
      socket.off('sent', listener);
      resolve(message);
    };
    socket.on('sent', listener);
  });
}

async function run(): Promise<void> {
  const enginePath = path.resolve(process.cwd(), '../engine/chess-engine');
  if (!fs.existsSync(enginePath)) throw new Error(`Build the engine first: ${enginePath}`);
  const pool = new EnginePoolManager({ enginePath, maxConcurrent: 1, idleTimeoutMs: 30_000, sessionMaxMs: 30_000 });
  const socket = new IntegrationSocket();
  pool.handleConnection(socket.asWebSocket(), 'fair-play-reported-fen');
  await waitFor(socket, 'ready');
  socket.messages = [];
  socket.request({ type: 'setoption', name: 'OwnBook', value: false });
  socket.request({
    type: 'move', requestId: 54, purpose: 'opponent', ponder: false,
    fen: REPORTED_FEN, moves: [], wtime: 7466, btime: 119093, winc: 0, binc: 0,
    multiPv: 1, difficulty: 'standard',
  });
  const started = await waitFor(socket, 'search-started');
  assert.equal(started.positionCommandSent, `position fen ${REPORTED_FEN}`);
  assert.equal(started.goCommandSent, 'go wtime 7466 btime 119093 winc 0 binc 0');
  assert.equal(started.expectedSide, 'w');

  const result = await waitFor(socket, 'bestmove');
  assert.equal(result.requestId, 54);
  assert.equal(result.positionKey, started.positionKey);
  assert.equal(result.sessionGeneration, started.sessionGeneration);
  assert.equal(typeof result.move, 'string');
  const validation = validateEngineMove(REPORTED_FEN, result.move as string);
  assert.equal(validation.ok, true);
  if (!validation.ok) throw new Error('Real engine returned an invalid move');
  socket.request({
    type: 'resultAck', requestId: 54, positionKey: result.positionKey,
    applied: true, oldFen: REPORTED_FEN, newFen: validation.newFen,
  });
  const applied = await waitFor(socket, 'move-applied');
  assert.equal(applied.newFen, validation.newFen);
  assert.equal(socket.messages.filter(message => message.type === 'bestmove').length, 1);
  assert.equal(socket.messages.filter(message => message.type === 'error').length, 0);
  socket.close();
  console.log(JSON.stringify({
    status: 'passed', requestId: 54, expectedSide: 'w', bestmove: result.move,
    positionKey: result.positionKey, newFen: validation.newFen,
  }));
}

run().catch(error => { console.error(error); process.exitCode = 1; });
