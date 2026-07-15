import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

const root = process.cwd();
const read = (relativePath: string) => fs.readFileSync(path.join(root, relativePath), 'utf8');

const page = read('src/app/page.tsx');
const shell = read('src/components/layout/ProductAppShell.tsx');
const workspace = read('src/components/layout/SessionWorkspace.tsx');
const board = read('src/components/layout/BoardRegion.tsx');
const sidebar = read('src/components/layout/SidebarRegion.tsx');
const navigation = read('src/components/layout/ModeNavigation.tsx');
const styles = read('src/components/layout/ProductLayout.module.css');
const globals = read('src/app/globals.css');
const controllerView = read('src/session/SessionControllerView.tsx');
const controller = read('src/session/useSessionControllerValue.tsx');

// Structural components and landmarks remain explicit.
assert.match(shell, /export function ProductAppShell/);
for (const component of ['ProductHeader', 'SessionWorkspace', 'ProductFooter']) {
  assert.match(shell, new RegExp(`<${component}`));
}
assert.match(workspace, /<main/);
assert.match(workspace, /<BoardRegion/);
assert.match(workspace, /<SidebarRegion/);
assert.match(sidebar, /<aside/);
assert.match(navigation, /<nav/);
assert.match(board, /aria-label="Chessboard"/);

// The three modes remain present and mode cleanup stays controller-owned.
for (const mode of ['fair', 'training', 'analysis']) assert.match(navigation, new RegExp(`id: '${mode}'`));
assert.match(controllerView, /onModeChange=\{mode\.change\}/);
assert.match(controller, /const isAnalysis/);
assert.match(controller, /const isTraining/);
assert.match(controller, /const showClock/);
assert.equal((controller.match(/useEngine\(\)/g) ?? []).length, 1);
assert.match(page, /SessionControllerProvider/);
assert.doesNotMatch(shell + workspace + board + sidebar + navigation, /useEngine\(|useChessGame\(|useChessClock\(/);

// Responsive/fullscreen presentation changes do not key or remount session content.
assert.doesNotMatch(shell + workspace, /key=/);
assert.match(shell, /fullscreenchange/);
assert.match(shell, /data-fullscreen=\{isFullscreen\}/);
assert.match(shell, /data-session-active=\{props\.sessionActive\}/);
assert.match(styles, /\[data-fullscreen="true"\]/);
assert.match(styles, /\[data-session-active="true"\] \.footer/);

// The desktop layout is content-driven; the stacked fallback has no fixed panel width.
assert.match(styles, /@media \(min-width: 75rem\)/);
assert.match(styles, /grid-template-columns: minmax\(0, 1fr\) var\(--sidebar-width\)/);
assert.match(styles, /--sidebar-width: clamp\(22\.5rem, 28vw, 28\.75rem\)/);
assert.match(styles, /--workspace-max-width: 112\.5rem/);
assert.match(styles, /@media \(max-width: 74\.999rem\)[\s\S]*\.sidebar \{[\s\S]*width: auto/);
assert.doesNotMatch(page + styles, /540px|h-screen|max-w-\[1500px\]/);

// Dynamic viewport sizing and reachable document scrolling replace the old trap.
assert.match(styles, /min-height: 100dvh/);
assert.match(styles, /overflow-y: auto/);
assert.doesNotMatch(globals, /Hide native scrollbars globally|display: none !important/);

console.log('Responsive layout structural tests passed.');
