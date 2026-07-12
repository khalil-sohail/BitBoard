import { strict as assert } from 'assert';
import { classifyTerminalPosition } from './engine-terminal';

function testCheckmateClassification(): void {
  const result = classifyTerminalPosition('7k/6Q1/7K/8/8/8/8/8 b - - 0 1', []);
  assert.deepEqual(result, { reason: 'checkmate', winner: 'white' });
}

function testStalemateClassification(): void {
  const result = classifyTerminalPosition('8/8/8/8/8/5kq1/8/7K w - - 0 1', []);
  assert.deepEqual(result, { reason: 'stalemate' });
}

function testDrawClassification(): void {
  const result = classifyTerminalPosition('8/8/8/8/8/8/8/K6k w - - 0 1', []);
  assert.deepEqual(result, { reason: 'draw' });
}

function testNoLegalMoveFallbackForNonTerminal(): void {
  const result = classifyTerminalPosition('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1', []);
  assert.deepEqual(result, { reason: 'no-legal-move' });
}

function testUnknownOnReconstructionFailure(): void {
  const result = classifyTerminalPosition('not a fen', []);
  assert.deepEqual(result, { reason: 'unknown' });
}

function run(): void {
  testCheckmateClassification();
  testStalemateClassification();
  testDrawClassification();
  testNoLegalMoveFallbackForNonTerminal();
  testUnknownOnReconstructionFailure();

  console.log('engine terminal tests passed');
}

run();
