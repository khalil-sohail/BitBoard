import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const WebSocket = require('ws');
const appPort = 31_000 + (process.pid % 1_000);
const debugPort = 32_000 + (process.pid % 1_000);
const appUrl = `http://127.0.0.1:${appPort}/`;
const chromeBinary = process.env.CHROME_BIN || '/usr/bin/google-chrome';
const children = [];

if (!existsSync('.next/BUILD_ID')) throw new Error('Run npm run build before the browser integration test.');
if (!existsSync('../engine/chess-engine')) throw new Error('Build engine/chess-engine before the browser integration test.');
if (!existsSync(chromeBinary)) throw new Error(`Chrome was not found at ${chromeBinary}. Set CHROME_BIN to override.`);

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

async function waitUntil(check, timeoutMs, label) {
  const started = Date.now();
  while (Date.now() - started < timeoutMs) {
    if (await check()) return;
    await sleep(100);
  }
  throw new Error(`Timed out waiting for ${label}`);
}

function startProcess(command, args, options) {
  const child = spawn(command, args, { ...options, stdio: ['ignore', 'pipe', 'pipe'] });
  children.push(child);
  child.stdout.on('data', chunk => process.stdout.write(String(chunk)));
  child.stderr.on('data', chunk => process.stderr.write(String(chunk)));
  return child;
}

async function createBrowserPage() {
  const response = await fetch(`http://127.0.0.1:${debugPort}/json/new?${encodeURIComponent(appUrl)}`, { method: 'PUT' });
  assert.equal(response.ok, true, `Chrome target creation failed: ${response.status}`);
  return response.json();
}

function connectCdp(url) {
  const socket = new WebSocket(url);
  const frames = [];
  const pending = new Map();
  let nextId = 0;
  socket.on('message', raw => {
    const message = JSON.parse(String(raw));
    if (message.id) {
      const call = pending.get(message.id);
      if (!call) return;
      pending.delete(message.id);
      if (message.error) call.reject(new Error(JSON.stringify(message.error)));
      else call.resolve(message.result);
      return;
    }
    if (message.method === 'Network.webSocketFrameSent' || message.method === 'Network.webSocketFrameReceived') {
      frames.push({
        direction: message.method.endsWith('Sent') ? 'sent' : 'received',
        timestamp: Date.now(),
        payload: message.params.response.payloadData,
      });
    }
  });
  return {
    socket,
    frames,
    send(method, params = {}) {
      return new Promise((resolve, reject) => {
        const id = ++nextId;
        pending.set(id, { resolve, reject });
        socket.send(JSON.stringify({ id, method, params }));
      });
    },
  };
}

async function evaluate(cdp, expression) {
  const response = await cdp.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  if (response.exceptionDetails) throw new Error(response.exceptionDetails.text);
  return response.result.value;
}

async function clickButton(cdp, text) {
  const found = await evaluate(cdp, `(() => {
    const button = [...document.querySelectorAll('button')].find(item => item.textContent?.replace(/\\s+/g, ' ').trim().includes(${JSON.stringify(text)}));
    if (!button) return false;
    button.click();
    return true;
  })()`);
  assert.equal(found, true, `Button not found: ${text}`);
  await sleep(200);
}

async function setRange(cdp, id, value) {
  const changed = await evaluate(cdp, `(() => {
    const input = document.getElementById(${JSON.stringify(id)});
    if (!(input instanceof HTMLInputElement)) return false;
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')?.set?.call(input, ${JSON.stringify(String(value))});
    input.dispatchEvent(new Event('input', { bubbles: true }));
    input.dispatchEvent(new Event('change', { bubbles: true }));
    return input.value === ${JSON.stringify(String(value))};
  })()`);
  assert.equal(changed, true, `Range not found: ${id}`);
}

async function move(cdp, from, to, finalSettleMs = 100) {
  for (const [index, square] of [from, to].entries()) {
    const clicked = await evaluate(cdp, `(() => {
      const element = document.querySelector('[data-square=${JSON.stringify(square)}]');
      if (!element) return false;
      element.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true, view: window }));
      return true;
    })()`);
    assert.equal(clicked, true, `Square not found: ${square}`);
    await sleep(index === 0 ? 100 : finalSettleMs);
  }
}

const parsedFrames = (cdp, direction) => cdp.frames
  .filter(frame => frame.direction === direction)
  .map(frame => {
    try { return { ...frame, message: JSON.parse(frame.payload) }; } catch { return null; }
  })
  .filter(Boolean);

const messages = (cdp, direction, type) => parsedFrames(cdp, direction)
  .filter(frame => frame.message.type === type);

async function configureTraining(cdp, reviewDepth = 2) {
  await clickButton(cdp, 'Training');
  await clickButton(cdp, 'Set up');
  await setRange(cdp, 'training-review-depth', reviewDepth);
  await clickButton(cdp, 'Start Training');
  await waitUntil(() => messages(cdp, 'received', 'bestmove').length >= 1, 20_000, 'Training root review');
}

async function run() {
  startProcess(process.execPath, ['dist/server.js'], {
    cwd: process.cwd(),
    env: { ...process.env, NODE_ENV: 'production', PORT: String(appPort), ENGINE_PATH: '../engine/chess-engine' },
  });
  await waitUntil(async () => {
    try { return (await fetch(appUrl)).ok; } catch { return false; }
  }, 20_000, 'production server');

  startProcess(chromeBinary, [
    '--headless=new', '--no-sandbox', '--disable-gpu',
    `--remote-debugging-port=${debugPort}`,
    `--user-data-dir=/tmp/bitboard-result-ack-${process.pid}`,
    'about:blank',
  ], { cwd: process.cwd(), env: process.env });
  await waitUntil(async () => {
    try { return (await fetch(`http://127.0.0.1:${debugPort}/json/version`)).ok; } catch { return false; }
  }, 10_000, 'Chrome debugging endpoint');

  const target = await createBrowserPage();
  const cdp = connectCdp(target.webSocketDebuggerUrl);
  await new Promise(resolve => cdp.socket.once('open', resolve));
  await Promise.all([cdp.send('Page.enable'), cdp.send('Runtime.enable'), cdp.send('Network.enable')]);
  await waitUntil(() => evaluate(cdp, `document.body.innerText.includes('Fair Play')`), 10_000, 'application page');

  // Training: review is informational; only the opponent move is acknowledged once.
  await configureTraining(cdp);
  const firstRoot = messages(cdp, 'received', 'bestmove').at(-1).message;
  await move(cdp, 'e2', 'e4');
  await waitUntil(() => messages(cdp, 'sent', 'analyze').some(frame => frame.message.purpose === 'training-result-review'), 10_000, 'Training result review request');
  await waitUntil(() => messages(cdp, 'sent', 'move').length === 1, 20_000, 'Training opponent search');
  await waitUntil(() => messages(cdp, 'sent', 'resultAck').length === 1, 20_000, 'Training result acknowledgment');
  await waitUntil(() => messages(cdp, 'received', 'move-applied').length === 1, 10_000, 'move-applied response');
  const acknowledgment = messages(cdp, 'sent', 'resultAck')[0].message;
  const applied = messages(cdp, 'received', 'move-applied')[0].message;
  assert.equal(acknowledgment.requestId, applied.requestId);
  assert.equal(acknowledgment.positionKey, applied.positionKey);
  assert.equal(acknowledgment.applied, true);
  assert.equal(messages(cdp, 'received', 'error').some(frame => frame.message.code === 'STALE_APPLICATION_ACK'), false);
  assert.equal(messages(cdp, 'sent', 'resultAck').some(frame => frame.message.requestId === firstRoot.requestId), false);

  // Analysis: position changes restart informational analysis without resultAck.
  await clickButton(cdp, 'Analysis');
  await clickButton(cdp, 'Set up');
  await setRange(cdp, 'analysis-setup-depth', 2);
  const analysisBestMovesBefore = messages(cdp, 'received', 'bestmove').length;
  await clickButton(cdp, 'Load & Analyze');
  await waitUntil(() => messages(cdp, 'received', 'bestmove').length > analysisBestMovesBefore, 20_000, 'initial Analysis result');
  const acknowledgmentsBeforeAnalysisMove = messages(cdp, 'sent', 'resultAck').length;
  const analysisRequestsBeforeMove = messages(cdp, 'sent', 'analyze').length;
  await move(cdp, 'e2', 'e4');
  await waitUntil(() => messages(cdp, 'sent', 'analyze').length > analysisRequestsBeforeMove, 10_000, 'Analysis restart');
  await waitUntil(() => messages(cdp, 'received', 'bestmove').length > analysisBestMovesBefore + 1, 20_000, 'Analysis result after move');
  assert.equal(messages(cdp, 'sent', 'resultAck').length, acknowledgmentsBeforeAnalysisMove);

  // Delayed Training messages after a mode switch cannot acknowledge in Analysis.
  await cdp.send('Page.reload', { ignoreCache: true });
  await sleep(1_000);
  await waitUntil(() => evaluate(cdp, `document.readyState === 'complete' && document.body.innerText.includes('Fair Play')`), 10_000, 'reloaded application');
  cdp.frames.length = 0;
  await configureTraining(cdp, 18);
  await move(cdp, 'e2', 'e4', 0);
  await clickButton(cdp, 'Analysis');
  const acknowledgmentsAtModeSwitch = messages(cdp, 'sent', 'resultAck').length;
  await sleep(5_000);
  assert.equal(messages(cdp, 'sent', 'resultAck').length, acknowledgmentsAtModeSwitch);
  assert.equal(acknowledgmentsAtModeSwitch, 0);
  assert.equal(messages(cdp, 'received', 'error').some(frame => frame.message.code === 'STALE_APPLICATION_ACK'), false);
  const body = await evaluate(cdp, 'document.body.innerText');
  assert.equal(body.includes('Move application acknowledgement did not match the active result.'), false);

  await cdp.send('Page.close');
  console.log(JSON.stringify({
    status: 'passed',
    trainingResultAckCount: 1,
    analysisResultAckCount: 0,
    delayedModeSwitchResultAckCount: acknowledgmentsAtModeSwitch,
    staleApplicationAckCount: 0,
  }));
}

try {
  await run();
} finally {
  for (const child of children.reverse()) {
    if (child.exitCode === null) child.kill('SIGTERM');
  }
}
