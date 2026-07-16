import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import type { AnalysisSnapshot } from '../../lib/board-arrows';
import { buildAnalysisCompactStatus, buildFairPlayCompactStatus, buildTrainingCompactStatus } from './responsive-session.adapters';

const read = (path: string) => readFileSync(path, 'utf8');

const fair = buildFairPlayCompactStatus({
  lifecycle: 'active', connectionStatus: 'idle', queuePosition: null, searchRetryCount: null,
  waitingForSessionReady: false, currentTurn: 'w', playerColor: 'w', whiteMs: 299_100, blackMs: 300_000, untimed: false,
});
assert.equal(fair.primary, 'Your turn');
assert.deepEqual(fair.values?.map(value => value.label), ['You', 'Engine']);
assert.doesNotMatch(JSON.stringify(fair), /evaluation|principal|depth|nodes|nps/i);

const training = buildTrainingCompactStatus({
  lifecycle: 'active', state: { status: 'reviewing-player-move', playerColor: 'w', reviewId: 4 },
  connectionStatus: 'analyzing', currentTurn: 'b', playerColor: 'w', latestGrade: 'Good', hasLatestFeedback: true,
});
assert.equal(training.primary, 'Reviewing your move');
assert.equal(training.badge, 'Good');

const snapshot: AnalysisSnapshot = {
  requestId: 8, mode: 'analysis', purpose: 'analysis', fen: 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',
  reportedDepth: 12, selectiveDepth: 18, multiPv: 1,
  lines: [{ multipv: 1, evaluation: { kind: 'centipawn', value: 42 }, pv: ['e2e4', 'e7e5'] }],
  status: 'live', createdAt: 1,
};
const analysis = buildAnalysisCompactStatus({ lifecycle: 'active', connectionStatus: 'analyzing', paused: false, snapshot });
assert.match(analysis.primary, /\+0\.42/);
assert.equal(analysis.badge, 'Depth 12');
assert.match(analysis.detail ?? '', /^SAN: e4 e5$/);

const panel = read('src/components/responsive/ResponsiveSessionPanel.tsx');
const bottom = read('src/components/responsive/SessionBottomPanel.tsx');
const strip = read('src/components/responsive/CompactStatusStrip.tsx');
const styles = read('src/components/responsive/ResponsiveSession.module.css');
const workspace = read('src/components/layout/SessionWorkspace.tsx');
const view = read('src/session/SessionControllerView.tsx');
const combined = panel + bottom + strip;

assert.match(workspace, /<ResponsiveSessionPanel/);
assert.match(panel, /<ModeNavigation/);
assert.match(panel, /<SidebarRegion>\{children\}<\/SidebarRegion>/);
assert.doesNotMatch(combined, /useSessionController|useEngine|resultAck|OpponentMoveApplicationReceipt/);
assert.doesNotMatch(panel + workspace, /key=/);
assert.match(strip, /aria-expanded=\{expanded\}/);
assert.match(strip, /aria-controls=\{controls\}/);
assert.match(bottom, /role=\{mobile \? 'dialog' : 'region'\}/);
assert.match(bottom, /aria-modal=\{mobile \? true : undefined\}/);
assert.match(panel, /event\.key === 'Escape'/);
assert.match(panel, /toggleRef\.current.*focus/);
assert.match(panel, /document\.body\.style\.overflow = 'hidden'/);
assert.match(panel, /event\.key !== 'Tab'/);
assert.match(styles, /@media \(min-width: 75rem\)/);
assert.match(styles, /@media \(max-width: 74\.999rem\)/);
assert.match(styles, /@media \(max-width: 47\.999rem\)/);
assert.match(styles, /min-height: 2\.75rem/);
assert.match(styles, /env\(safe-area-inset-bottom\)/);
assert.match(styles, /@media \(prefers-reduced-motion: reduce\)/);
assert.match(styles, /pointer-events: none/);
assert.match(view, /sessionStatus=\{lifecycle\.status\}/);

console.log('responsive session interaction tests passed');
