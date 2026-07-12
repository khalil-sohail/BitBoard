#!/usr/bin/env node
import { spawn } from 'node:child_process';
import { performance } from 'node:perf_hooks';
import { once } from 'node:events';

const enginePath = process.env.ENGINE ?? './chess-engine';

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function createUci() {
  const child = spawn(enginePath, ['--mode=gui'], { stdio: ['pipe', 'pipe', 'inherit'] });
  const lines = [];
  let buffer = '';

  child.stdout.setEncoding('utf8');
  child.stdout.on('data', chunk => {
    buffer += chunk;
    const parts = buffer.split('\n');
    buffer = parts.pop() ?? '';
    for (const part of parts) {
      const line = part.trim();
      if (line) lines.push(line);
    }
  });

  const send = line => child.stdin.write(`${line}\n`);
  const waitFor = async (predicate, timeoutMs = 5000) => {
    const deadline = performance.now() + timeoutMs;
    while (performance.now() < deadline) {
      const index = lines.findIndex(predicate);
      if (index >= 0) {
        const [line] = lines.splice(index, 1);
        return line;
      }
      await sleep(5);
    }
    throw new Error('Timed out waiting for engine output');
  };

  send('uci');
  await waitFor(line => line === 'uciok');
  send('isready');
  await waitFor(line => line === 'readyok');
  send('setoption name OwnBook value false');

  return {
    child,
    send,
    waitFor,
    close: async () => {
      send('quit');
      await Promise.race([once(child, 'exit'), sleep(1000)]);
      if (child.exitCode === null && child.signalCode === null) child.kill('SIGKILL');
    },
  };
}

async function runPonderHit(delayMs) {
  const uci = await createUci();
  try {
    uci.send('position startpos moves d2d4 g8f6');
    const startedAt = performance.now();
    uci.send('go ponder movetime 1000');
    await sleep(delayMs);
    const hitAt = performance.now();
    uci.send('ponderhit');
    await uci.waitFor(line => line.startsWith('bestmove '), 5000);
    const elapsed = performance.now() - startedAt;
    const afterHit = performance.now() - hitAt;
    return { delayMs, elapsed, afterHit };
  } finally {
    await uci.close();
  }
}

const samples = [
  await runPonderHit(0),
  await runPonderHit(250),
  await runPonderHit(500),
  await runPonderHit(900),
  await runPonderHit(1100),
];

for (const sample of samples) {
  console.log(`delay=${sample.delayMs} elapsed=${Math.round(sample.elapsed)} afterHit=${Math.round(sample.afterHit)}`);
  if (sample.delayMs < 1000 && sample.elapsed > 1500) {
    throw new Error(`Ponderhit granted a fresh budget for delay ${sample.delayMs}`);
  }
  if (sample.delayMs >= 1000 && sample.afterHit > 500) {
    throw new Error(`Completed ponder was not released promptly after late hit ${sample.delayMs}`);
  }
}
