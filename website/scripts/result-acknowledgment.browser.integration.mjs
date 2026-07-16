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
const responsiveViewports = [
  [2560, 1600], [1920, 1080], [1440, 900], [1366, 768],
  [1024, 768], [768, 1024], [390, 844],
];

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

async function setTextArea(cdp, id, value) {
  const changed = await evaluate(cdp, `(() => {
    const input = document.getElementById(${JSON.stringify(id)});
    if (!(input instanceof HTMLTextAreaElement)) return false;
    Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value')?.set?.call(input, ${JSON.stringify(value)});
    input.dispatchEvent(new Event('input', { bubbles: true }));
    input.dispatchEvent(new Event('change', { bubbles: true }));
    return input.value === ${JSON.stringify(value)};
  })()`);
  assert.equal(changed, true, `Textarea not found: ${id}`);
}

async function clickAnalysisLineCount(cdp, value) {
  const clicked = await evaluate(cdp, `(() => {
    const fieldset = [...document.querySelectorAll('fieldset')].find(item => item.textContent?.includes('Lines'));
    const button = fieldset && [...fieldset.querySelectorAll('button')].find(item => item.textContent?.trim() === ${JSON.stringify(String(value))});
    if (!(button instanceof HTMLButtonElement)) return false;
    button.click();
    return true;
  })()`);
  assert.equal(clicked, true, `Analysis line count not found: ${value}`);
  await sleep(200);
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

async function verifyFairPlayResponsiveLayout(cdp, expectedState) {
  for (const [width, height] of responsiveViewports) {
    await cdp.send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: width < 768 });
    await sleep(80);
    const layout = await evaluate(cdp, `(() => {
      const sidebar = document.querySelector('[data-fair-play-state]');
      if (!(sidebar instanceof HTMLElement)) return null;
      const rect = sidebar.getBoundingClientRect();
      return {
        state: sidebar.dataset.fairPlayState,
        horizontalOverflow: document.documentElement.scrollWidth > window.innerWidth + 1,
        left: rect.left,
        right: rect.right,
        viewportWidth: window.innerWidth,
        sharedHistory: Boolean(sidebar.querySelector('[data-live-history]')),
        sharedEvaluation: Boolean(sidebar.querySelector('[data-live-evaluation]')),
        sharedPv: Boolean(sidebar.querySelector('[data-live-pv]')),
        sharedMetrics: Boolean(sidebar.querySelector('[data-live-metrics]')),
        sharedActions: Boolean(sidebar.querySelector('[data-live-actions]')),
      };
    })()`);
    assert.ok(layout, `Fair Play sidebar missing at ${width}x${height}`);
    assert.equal(layout.state, expectedState, `Unexpected Fair Play state at ${width}x${height}`);
    assert.equal(layout.horizontalOverflow, false, `Fair Play horizontal overflow at ${width}x${height}`);
    assert.ok(layout.left >= -1 && layout.right <= layout.viewportWidth + 1, `Fair Play sidebar clipped at ${width}x${height}`);
    assert.equal(layout.sharedActions, true, `Shared Fair Play actions missing at ${width}x${height}`);
    if (expectedState !== 'idle') assert.equal(layout.sharedHistory, true, `Shared Fair Play history missing at ${width}x${height}`);
    assert.equal(layout.sharedEvaluation, false, 'Fair Play must not render shared evaluation');
    assert.equal(layout.sharedPv, false, 'Fair Play must not render shared PV');
    assert.equal(layout.sharedMetrics, false, 'Fair Play must not render shared engine metrics');
    const screenshot = await cdp.send('Page.captureScreenshot', { format: 'jpeg', quality: 35, captureBeyondViewport: false });
    assert.ok(screenshot.data.length > 100, `Fair Play screenshot capture failed at ${width}x${height}`);
  }
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
}

async function verifyTrainingResponsiveLayout(cdp, expectedState) {
  for (const [width, height] of responsiveViewports) {
    await cdp.send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: width < 768 });
    await sleep(80);
    const layout = await evaluate(cdp, `(() => {
      const sidebar = document.querySelector('[data-training-state]');
      if (!(sidebar instanceof HTMLElement)) return null;
      const rect = sidebar.getBoundingClientRect();
      return {
        state: sidebar.dataset.trainingState,
        horizontalOverflow: document.documentElement.scrollWidth > window.innerWidth + 1,
        left: rect.left,
        right: rect.right,
        viewportWidth: window.innerWidth,
        documentHeight: document.documentElement.scrollHeight,
        sharedHistory: Boolean(sidebar.querySelector('[data-live-history]')),
        sharedEvaluation: Boolean(sidebar.querySelector('[data-live-evaluation]')),
        sharedPv: Boolean(sidebar.querySelector('[data-live-pv]')),
        sharedMetrics: Boolean(sidebar.querySelector('[data-live-metrics]')),
        sharedStatus: Boolean(sidebar.querySelector('[data-live-status]')),
        sharedActions: Boolean(sidebar.querySelector('[data-live-actions]')),
      };
    })()`);
    assert.ok(layout, `Training sidebar missing at ${width}x${height}`);
    assert.equal(layout.state, expectedState, `Unexpected Training state at ${width}x${height}`);
    assert.equal(layout.horizontalOverflow, false, `Horizontal overflow at ${width}x${height}`);
    assert.ok(layout.left >= -1 && layout.right <= layout.viewportWidth + 1, `Sidebar clipped at ${width}x${height}`);
    assert.equal(layout.sharedActions, true, `Shared Training actions missing at ${width}x${height}`);
    if (expectedState !== 'idle') {
      assert.equal(layout.sharedHistory, true, `Shared Training history missing at ${width}x${height}`);
      assert.equal(layout.sharedEvaluation, true, `Shared Training evaluation missing at ${width}x${height}`);
      assert.equal(layout.sharedPv, true, `Shared Training PV missing at ${width}x${height}`);
      assert.equal(layout.sharedMetrics, true, `Shared Training metrics missing at ${width}x${height}`);
      assert.equal(layout.sharedStatus, true, `Shared Training status missing at ${width}x${height}`);
    }
    const screenshot = await cdp.send('Page.captureScreenshot', { format: 'jpeg', quality: 35, captureBeyondViewport: false });
    assert.ok(screenshot.data.length > 100, `Screenshot capture failed at ${width}x${height}`);
  }
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
}

async function verifyAnalysisResponsiveLayout(cdp, expectedState) {
  for (const [width, height] of responsiveViewports) {
    await cdp.send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: width < 768 });
    await sleep(80);
    const layout = await evaluate(cdp, `(() => {
      const sidebar = document.querySelector('[data-analysis-state]');
      if (!(sidebar instanceof HTMLElement)) return null;
      const rect = sidebar.getBoundingClientRect();
      return {
        state: sidebar.dataset.analysisState,
        horizontalOverflow: document.documentElement.scrollWidth > window.innerWidth + 1,
        left: rect.left,
        right: rect.right,
        viewportWidth: window.innerWidth,
        sharedHistory: Boolean(sidebar.querySelector('[data-live-history]')),
        sharedEvaluation: Boolean(sidebar.querySelector('[data-live-evaluation]')),
        sharedPv: Boolean(sidebar.querySelector('[data-live-pv]')),
        sharedMetrics: Boolean(sidebar.querySelector('[data-live-metrics]')),
        sharedStatus: Boolean(sidebar.querySelector('[data-live-status]')),
        sharedActions: Boolean(sidebar.querySelector('[data-live-actions]')),
        notation: sidebar.querySelector('[data-live-pv] li')?.getAttribute('data-notation') ?? null,
      };
    })()`);
    assert.ok(layout, `Analysis sidebar missing at ${width}x${height}`);
    assert.equal(layout.state, expectedState, `Unexpected Analysis state at ${width}x${height}`);
    assert.equal(layout.horizontalOverflow, false, `Analysis horizontal overflow at ${width}x${height}`);
    assert.ok(layout.left >= -1 && layout.right <= layout.viewportWidth + 1, `Analysis sidebar clipped at ${width}x${height}`);
    assert.equal(layout.sharedActions, true, `Shared Analysis actions missing at ${width}x${height}`);
    if (expectedState !== 'idle') {
      assert.equal(layout.sharedHistory, true, `Shared Analysis history missing at ${width}x${height}`);
      assert.equal(layout.sharedEvaluation, true, `Shared Analysis evaluation missing at ${width}x${height}`);
      assert.equal(layout.sharedPv, true, `Shared Analysis PV missing at ${width}x${height}`);
      assert.equal(layout.sharedMetrics, true, `Shared Analysis metrics missing at ${width}x${height}`);
      assert.equal(layout.sharedStatus, true, `Shared Analysis status missing at ${width}x${height}`);
      assert.ok(layout.notation === 'SAN' || layout.notation === 'UCI', `Analysis PV notation missing at ${width}x${height}`);
    }
    const screenshot = await cdp.send('Page.captureScreenshot', { format: 'jpeg', quality: 35, captureBeyondViewport: false });
    assert.ok(screenshot.data.length > 100, `Analysis screenshot capture failed at ${width}x${height}`);
  }
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
}

async function assertCurrentAnalysisSnapshot(cdp) {
  const latestRequest = messages(cdp, 'sent', 'analyze').at(-1)?.message;
  assert.ok(latestRequest, 'No Analysis request was sent');
  const displayed = await evaluate(cdp, `(() => {
    const sidebar = document.querySelector('[data-analysis-state]');
    if (!(sidebar instanceof HTMLElement)) return null;
    return { requestId: Number(sidebar.dataset.analysisRequestId), fen: sidebar.dataset.analysisSnapshotFen };
  })()`);
  assert.deepEqual(displayed, { requestId: latestRequest.requestId, fen: latestRequest.fen });
}

const parsedFrames = (cdp, direction) => cdp.frames
  .filter(frame => frame.direction === direction)
  .map(frame => {
    try { return { ...frame, message: JSON.parse(frame.payload) }; } catch { return null; }
  })
  .filter(Boolean);

const messages = (cdp, direction, type) => parsedFrames(cdp, direction)
  .filter(frame => frame.message.type === type);

async function configureTraining(cdp, reviewDepth = 2, selectMode = true) {
  if (selectMode) await clickButton(cdp, 'Training');
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

  // Fair Play: the shared history/actions remain policy-restricted, including after completion.
  await verifyFairPlayResponsiveLayout(cdp, 'idle');
  await clickButton(cdp, 'Set up game');
  await clickButton(cdp, 'Start Fair Play');
  await waitUntil(() => evaluate(cdp, `document.querySelector('[data-fair-play-state]')?.getAttribute('data-fair-play-state') === 'active'`), 10_000, 'active Fair Play session');
  await move(cdp, 'e2', 'e4');
  await waitUntil(() => messages(cdp, 'sent', 'resultAck').length === 1, 20_000, 'Fair Play result acknowledgment');
  await verifyFairPlayResponsiveLayout(cdp, 'active');
  await clickButton(cdp, 'Resign');
  await clickButton(cdp, 'Confirm resign');
  await waitUntil(() => evaluate(cdp, `document.querySelector('[data-fair-play-state]')?.getAttribute('data-fair-play-state') === 'completed'`), 5_000, 'completed Fair Play session');
  await verifyFairPlayResponsiveLayout(cdp, 'completed');
  cdp.frames.length = 0;

  // Training: review is informational; only the opponent move is acknowledged once.
  await clickButton(cdp, 'Training');
  await verifyTrainingResponsiveLayout(cdp, 'idle');
  await configureTraining(cdp, 2, false);
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
  await verifyTrainingResponsiveLayout(cdp, 'player-turn');

  // Analysis: every snapshot is informational and position-scoped.
  await clickButton(cdp, 'Analysis');
  await verifyAnalysisResponsiveLayout(cdp, 'idle');
  await clickButton(cdp, 'Set up');
  await setRange(cdp, 'analysis-setup-depth', 2);
  const analysisBestMovesBefore = messages(cdp, 'received', 'bestmove').length;
  await clickButton(cdp, 'Load & Analyze');
  await waitUntil(() => messages(cdp, 'received', 'bestmove').length > analysisBestMovesBefore, 20_000, 'initial Analysis result');
  await waitUntil(() => evaluate(cdp, `Boolean(document.querySelector('[data-analysis-request-id]'))`), 5_000, 'initial Analysis snapshot');
  await assertCurrentAnalysisSnapshot(cdp);
  const acknowledgmentsBeforeAnalysisMove = messages(cdp, 'sent', 'resultAck').length;
  const analysisRequestsBeforeMove = messages(cdp, 'sent', 'analyze').length;
  await move(cdp, 'e2', 'e4');
  await waitUntil(() => messages(cdp, 'sent', 'analyze').length > analysisRequestsBeforeMove, 10_000, 'Analysis restart');
  await waitUntil(() => messages(cdp, 'received', 'bestmove').length > analysisBestMovesBefore + 1, 20_000, 'Analysis result after move');
  assert.equal(messages(cdp, 'sent', 'resultAck').length, acknowledgmentsBeforeAnalysisMove);
  await assertCurrentAnalysisSnapshot(cdp);

  const requestsBeforeDepth = messages(cdp, 'sent', 'analyze').length;
  await setRange(cdp, 'live-analysis-depth', 3);
  await waitUntil(() => messages(cdp, 'sent', 'analyze').length > requestsBeforeDepth, 10_000, 'Analysis depth restart');
  assert.equal(messages(cdp, 'sent', 'analyze').at(-1).message.searchLimit.depth, 3);
  await waitUntil(() => messages(cdp, 'received', 'bestmove').length > analysisBestMovesBefore + 2, 20_000, 'depth-change Analysis result');
  await assertCurrentAnalysisSnapshot(cdp);

  const requestsBeforeMultiPv = messages(cdp, 'sent', 'analyze').length;
  await clickAnalysisLineCount(cdp, 2);
  await waitUntil(() => messages(cdp, 'sent', 'analyze').length > requestsBeforeMultiPv, 10_000, 'Analysis MultiPV restart');
  assert.equal(messages(cdp, 'sent', 'analyze').at(-1).message.multiPv, 2);
  await waitUntil(() => messages(cdp, 'received', 'bestmove').length > analysisBestMovesBefore + 3, 20_000, 'MultiPV Analysis result');
  await assertCurrentAnalysisSnapshot(cdp);
  await verifyAnalysisResponsiveLayout(cdp, 'analyzing');

  await clickButton(cdp, 'Stop analysis');
  await waitUntil(() => evaluate(cdp, `document.querySelector('[data-analysis-state]')?.getAttribute('data-analysis-state') === 'stopped'`), 5_000, 'stopped Analysis state');
  await assertCurrentAnalysisSnapshot(cdp);
  await verifyAnalysisResponsiveLayout(cdp, 'stopped');
  const requestsBeforeResume = messages(cdp, 'sent', 'analyze').length;
  await clickButton(cdp, 'Resume analysis');
  await waitUntil(() => messages(cdp, 'sent', 'analyze').length > requestsBeforeResume, 10_000, 'resumed Analysis request');

  await clickButton(cdp, 'Change position');
  await clickButton(cdp, 'FEN');
  const customFen = 'rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1';
  await setTextArea(cdp, 'analysis-fen', customFen);
  const requestsBeforeFen = messages(cdp, 'sent', 'analyze').length;
  await clickButton(cdp, 'Load & Analyze');
  await waitUntil(() => messages(cdp, 'sent', 'analyze').length > requestsBeforeFen, 10_000, 'FEN Analysis request');
  assert.equal(messages(cdp, 'sent', 'analyze').at(-1).message.fen, customFen);
  await waitUntil(() => evaluate(cdp, `document.body.innerText.toLowerCase().includes('fen position')`), 5_000, 'FEN source summary');

  await clickButton(cdp, 'Change position');
  await clickButton(cdp, 'PGN');
  await setTextArea(cdp, 'analysis-pgn', '1. e4 e5');
  const requestsBeforePgn = messages(cdp, 'sent', 'analyze').length;
  await clickButton(cdp, 'Load & Analyze');
  await waitUntil(() => messages(cdp, 'sent', 'analyze').length > requestsBeforePgn, 10_000, 'PGN Analysis request');
  await waitUntil(() => evaluate(cdp, `document.body.innerText.toLowerCase().includes('pgn game') && document.body.innerText.includes('e4')`), 5_000, 'PGN source and history');
  const requestsBeforeNavigation = messages(cdp, 'sent', 'analyze').length;
  await clickButton(cdp, 'e4');
  await waitUntil(() => messages(cdp, 'sent', 'analyze').length > requestsBeforeNavigation, 10_000, 'PGN navigation Analysis request');
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
    staleAnalysisDisplayCount: 0,
    responsiveScreenshotsCaptured: responsiveViewports.length * 8,
  }));
}

try {
  await run();
} finally {
  for (const child of children.reverse()) {
    if (child.exitCode === null) child.kill('SIGTERM');
  }
}
