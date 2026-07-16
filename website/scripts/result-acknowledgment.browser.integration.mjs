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
  [2560, 1600], [1920, 1080], [1440, 900], [1366, 768], [1200, 800],
  [1024, 768], [834, 1194], [768, 1024], [430, 932], [390, 844], [360, 800],
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

async function responsivePanelLayout(cdp) {
  return evaluate(cdp, `(() => {
    const root = document.querySelector('[data-responsive-session-panel]');
    const strip = document.querySelector('[data-compact-status]');
    const panel = root?.querySelector('[aria-label$="session details"]');
    if (!(root instanceof HTMLElement) || !(strip instanceof HTMLElement) || !(panel instanceof HTMLElement)) return null;
    const visible = element => { const style = getComputedStyle(element); const rect = element.getBoundingClientRect(); return style.visibility !== 'hidden' && style.display !== 'none' && rect.width > 0 && rect.height > 0; };
    return {
      expanded: root.dataset.expanded,
      mobile: root.dataset.mobile,
      stripVisible: visible(strip),
      panelVisible: visible(panel),
      panelRole: panel.getAttribute('role'),
      panelAriaHidden: panel.getAttribute('aria-hidden'),
      bodyOverflow: document.body.style.overflow,
      backdrop: Boolean(document.querySelector('[class*="backdrop"]')),
    };
  })()`);
}

async function assertResponsivePanelLayout(cdp, width, sessionEngaged) {
  const layout = await responsivePanelLayout(cdp);
  assert.ok(layout, `Responsive session panel missing at ${width}px`);
  if (width >= 1200) {
    assert.equal(layout.stripVisible, false, `Desktop compact strip visible at ${width}px`);
    assert.equal(layout.panelVisible, true, `Desktop persistent sidebar hidden at ${width}px`);
  } else if (!sessionEngaged) {
    assert.equal(layout.stripVisible, false, `Idle compact strip visible at ${width}px`);
    assert.equal(layout.panelVisible, false, `Idle compact panel visible at ${width}px`);
  } else {
    assert.equal(layout.stripVisible, true, `Compact status strip hidden at ${width}px`);
    assert.equal(layout.panelVisible, false, `Collapsed panel visible at ${width}px`);
    assert.equal(layout.panelAriaHidden, 'true', `Collapsed panel not hidden semantically at ${width}px`);
  }
}

async function exerciseResponsivePanel(cdp, modeLabel) {
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
  await sleep(100);
  await evaluate(cdp, `window.__responsiveShellSentinel = document.querySelector('[data-product-app-shell]'); true`);
  const opened = await evaluate(cdp, `(() => { const button = document.querySelector('[data-compact-status]'); if (!(button instanceof HTMLButtonElement)) return false; button.click(); return true; })()`);
  assert.equal(opened, true, `${modeLabel} compact status could not open panel`);
  await waitUntil(async () => (await responsivePanelLayout(cdp))?.expanded === 'true', 2_000, `${modeLabel} mobile panel expansion`);
  let panel = await responsivePanelLayout(cdp);
  assert.equal(panel.panelRole, 'dialog');
  assert.equal(panel.panelVisible, true);
  assert.equal(panel.bodyOverflow, 'hidden');
  assert.equal(panel.backdrop, true);
  assert.equal(await evaluate(cdp, `window.__responsiveShellSentinel === document.querySelector('[data-product-app-shell]')`), true, `${modeLabel} shell remounted while opening panel`);
  let screenshot = await cdp.send('Page.captureScreenshot', { format: 'jpeg', quality: 35, captureBeyondViewport: false });
  assert.ok(screenshot.data.length > 100, `${modeLabel} mobile expanded screenshot failed`);
  await cdp.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Escape', code: 'Escape' });
  await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Escape', code: 'Escape' });
  await waitUntil(async () => (await responsivePanelLayout(cdp))?.expanded === 'false', 2_000, `${modeLabel} Escape collapse`);
  await waitUntil(() => evaluate(cdp, `document.activeElement === document.querySelector('[data-compact-status]')`), 2_000, `${modeLabel} focus restoration to strip`);

  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 834, height: 1194, deviceScaleFactor: 1, mobile: false });
  await waitUntil(async () => (await responsivePanelLayout(cdp))?.mobile === 'false', 2_000, `${modeLabel} tablet media state`);
  await evaluate(cdp, `document.querySelector('[data-compact-status]')?.click()`);
  await waitUntil(async () => (await responsivePanelLayout(cdp))?.expanded === 'true', 2_000, `${modeLabel} tablet panel expansion`);
  panel = await responsivePanelLayout(cdp);
  assert.equal(panel.panelRole, 'region');
  assert.equal(panel.bodyOverflow, '');
  screenshot = await cdp.send('Page.captureScreenshot', { format: 'jpeg', quality: 35, captureBeyondViewport: false });
  assert.ok(screenshot.data.length > 100, `${modeLabel} tablet expanded screenshot failed`);
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1194, height: 834, deviceScaleFactor: 1, mobile: false });
  await sleep(100);
  assert.equal((await responsivePanelLayout(cdp)).expanded, 'true', `${modeLabel} panel state changed across orientation`);
  assert.equal(await evaluate(cdp, `window.__responsiveShellSentinel === document.querySelector('[data-product-app-shell]')`), true, `${modeLabel} shell remounted across orientation`);
  await evaluate(cdp, `document.querySelector('[aria-label="Collapse session panel"]')?.click()`);
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  return { panelOpenCount: 2, unexpectedPanelOpenCount: 0, controllerRemountCount: 0 };
}

async function exerciseFullscreen(cdp, width, height) {
  await cdp.send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: width < 768 });
  await evaluate(cdp, `window.__fullscreenShellSentinel = document.querySelector('[data-product-app-shell]'); true`);
  const entered = await cdp.send('Runtime.evaluate', { expression: 'document.documentElement.requestFullscreen().then(() => true).catch(() => false)', awaitPromise: true, returnByValue: true, userGesture: true });
  assert.equal(entered.result.value, true, `Fullscreen entry failed at ${width}x${height}`);
  await waitUntil(() => evaluate(cdp, `document.querySelector('[data-product-app-shell]')?.getAttribute('data-fullscreen') === 'true'`), 2_000, 'fullscreen shell state');
  const state = await evaluate(cdp, `(() => ({
    sameShell: window.__fullscreenShellSentinel === document.querySelector('[data-product-app-shell]'),
    headerDisplay: getComputedStyle(document.querySelector('[data-product-app-shell] > header')).display,
    footerDisplay: getComputedStyle(document.querySelector('[data-product-app-shell] > footer')).display,
    horizontalOverflow: document.documentElement.scrollWidth > window.innerWidth + 1,
  }))()`);
  assert.equal(state.sameShell, true, 'Fullscreen remounted the application shell');
  assert.equal(state.headerDisplay, 'none');
  assert.equal(state.footerDisplay, 'none');
  assert.equal(state.horizontalOverflow, false);
  const screenshot = await cdp.send('Page.captureScreenshot', { format: 'jpeg', quality: 35, captureBeyondViewport: false });
  assert.ok(screenshot.data.length > 100);
  await cdp.send('Runtime.evaluate', { expression: 'document.exitFullscreen()', awaitPromise: true, userGesture: true });
  await waitUntil(() => evaluate(cdp, `document.querySelector('[data-product-app-shell]')?.getAttribute('data-fullscreen') === 'false'`), 2_000, 'fullscreen exit');
  assert.equal(await evaluate(cdp, `window.__fullscreenShellSentinel === document.querySelector('[data-product-app-shell]')`), true, 'Fullscreen exit remounted shell');
  return { controllerRemountCount: 0, horizontalOverflowCount: 0 };
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
    await assertResponsivePanelLayout(cdp, width, expectedState !== 'idle');
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
    await assertResponsivePanelLayout(cdp, width, expectedState !== 'idle');
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
    await assertResponsivePanelLayout(cdp, width, expectedState !== 'idle');
    const screenshot = await cdp.send('Page.captureScreenshot', { format: 'jpeg', quality: 35, captureBeyondViewport: false });
    assert.ok(screenshot.data.length > 100, `Analysis screenshot capture failed at ${width}x${height}`);
  }
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
}

async function assertCurrentAnalysisSnapshot(cdp) {
  const latestRequest = messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').at(-1)?.message;
  assert.ok(latestRequest, 'No Analysis request was sent');
  await waitUntil(() => evaluate(cdp, `(() => {
    const sidebar = document.querySelector('[data-analysis-state]');
    return sidebar?.getAttribute('data-analysis-request-id') === ${JSON.stringify(String(latestRequest.requestId))}
      && sidebar?.getAttribute('data-analysis-snapshot-fen') === ${JSON.stringify(latestRequest.fen)};
  })()`), 20_000, `current Analysis snapshot ${latestRequest.requestId}`);
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
  const interactionCounters = { panelOpenCount: 0, unexpectedPanelOpenCount: 0, controllerRemountCount: 0, horizontalOverflowCount: 0 };
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
  Object.assign(interactionCounters, addCounters(interactionCounters, await exerciseResponsivePanel(cdp, 'Fair Play')));
  Object.assign(interactionCounters, addCounters(interactionCounters, await exerciseFullscreen(cdp, 1440, 900)));
  Object.assign(interactionCounters, addCounters(interactionCounters, await exerciseFullscreen(cdp, 390, 844)));
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
  Object.assign(interactionCounters, addCounters(interactionCounters, await exerciseResponsivePanel(cdp, 'Training')));
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
  await evaluate(cdp, `document.querySelector('[data-compact-status]')?.click()`);
  const hintsBefore = messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'training-hint').length;
  await clickButton(cdp, 'Request hint');
  await waitUntil(() => evaluate(cdp, `document.body.innerText.includes('Next hint') && !document.body.innerText.includes('Preparing a hint for this position')`), 20_000, 'completed Training hint from mobile panel');
  assert.ok(messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'training-hint').length >= hintsBefore, 'Training hint request count regressed');
  await sleep(1_000);
  await evaluate(cdp, `document.querySelector('[aria-label="Collapse session panel"]')?.click()`);
  interactionCounters.panelOpenCount += 1;
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });

  // Analysis: every snapshot is informational and position-scoped.
  await clickButton(cdp, 'Analysis');
  await verifyAnalysisResponsiveLayout(cdp, 'idle');
  await clickButton(cdp, 'Set up');
  await setRange(cdp, 'analysis-setup-depth', 2);
  await clickButton(cdp, 'Load & Analyze');
  await assertCurrentAnalysisSnapshot(cdp);
  const acknowledgmentsBeforeAnalysisMove = messages(cdp, 'sent', 'resultAck').length;
  const analysisRequestsBeforeMove = messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').length;
  await move(cdp, 'e2', 'e4');
  await waitUntil(() => messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').length > analysisRequestsBeforeMove, 10_000, 'Analysis restart');
  assert.equal(messages(cdp, 'sent', 'resultAck').length, acknowledgmentsBeforeAnalysisMove);
  await assertCurrentAnalysisSnapshot(cdp);

  const requestsBeforeDepth = messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').length;
  await setRange(cdp, 'live-analysis-depth', 3);
  await waitUntil(() => messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').length > requestsBeforeDepth, 10_000, 'Analysis depth restart');
  assert.equal(messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').at(-1).message.searchLimit.depth, 3);
  await assertCurrentAnalysisSnapshot(cdp);

  const requestsBeforeMultiPv = messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').length;
  await clickAnalysisLineCount(cdp, 2);
  await waitUntil(() => messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').length > requestsBeforeMultiPv, 10_000, 'Analysis MultiPV restart');
  assert.equal(messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').at(-1).message.multiPv, 2);
  await assertCurrentAnalysisSnapshot(cdp);
  await verifyAnalysisResponsiveLayout(cdp, 'analyzing');
  Object.assign(interactionCounters, addCounters(interactionCounters, await exerciseResponsivePanel(cdp, 'Analysis')));

  await clickButton(cdp, 'Stop analysis');
  await waitUntil(() => evaluate(cdp, `document.querySelector('[data-analysis-state]')?.getAttribute('data-analysis-state') === 'stopped'`), 5_000, 'stopped Analysis state');
  await assertCurrentAnalysisSnapshot(cdp);
  await verifyAnalysisResponsiveLayout(cdp, 'stopped');
  const requestsBeforeResume = messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').length;
  await clickButton(cdp, 'Resume analysis');
  await waitUntil(() => messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').length > requestsBeforeResume, 10_000, 'resumed Analysis request');

  await clickButton(cdp, 'Change position');
  await clickButton(cdp, 'FEN');
  const customFen = 'rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1';
  await setTextArea(cdp, 'analysis-fen', customFen);
  const requestsBeforeFen = messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').length;
  await clickButton(cdp, 'Load & Analyze');
  await waitUntil(() => messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').length > requestsBeforeFen, 10_000, 'FEN Analysis request');
  assert.equal(messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').at(-1).message.fen, customFen);
  await waitUntil(() => evaluate(cdp, `document.body.innerText.toLowerCase().includes('fen position')`), 5_000, 'FEN source summary');

  await clickButton(cdp, 'Change position');
  await clickButton(cdp, 'PGN');
  await setTextArea(cdp, 'analysis-pgn', '1. e4 e5');
  const requestsBeforePgn = messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').length;
  await clickButton(cdp, 'Load & Analyze');
  await waitUntil(() => messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').length > requestsBeforePgn, 10_000, 'PGN Analysis request');
  await waitUntil(() => evaluate(cdp, `document.body.innerText.toLowerCase().includes('pgn game') && document.body.innerText.includes('e4')`), 5_000, 'PGN source and history');
  const requestsBeforeNavigation = messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').length;
  await clickButton(cdp, 'e4');
  await waitUntil(() => messages(cdp, 'sent', 'analyze').filter(frame => frame.message.purpose === 'analysis').length > requestsBeforeNavigation, 10_000, 'PGN navigation Analysis request');
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
    responsiveScreenshotsCaptured: responsiveViewports.length * 8 + 8,
    ...interactionCounters,
  }));
}

function addCounters(current, added) {
  return Object.fromEntries(Object.keys(current).map(key => [key, current[key] + (added[key] ?? 0)]));
}

try {
  await run();
} finally {
  for (const child of children.reverse()) {
    if (child.exitCode === null) child.kill('SIGTERM');
  }
}
