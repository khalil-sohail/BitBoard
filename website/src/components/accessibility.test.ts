import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = (path: string) => readFileSync(path, 'utf8');
const globals = read('src/app/globals.css');
const header = read('src/components/layout/ProductHeader.tsx');
const board = read('src/components/board/ChessBoard.tsx');
const setup = read('src/components/setup/SessionSetupDialog.tsx');
const responsive = read('src/components/responsive/ResponsiveSessionPanel.tsx') + read('src/components/responsive/SessionBottomPanel.tsx');
const resign = read('src/components/fair-play/FairPlayActions.tsx');
const toast = read('src/components/ui/Toast.tsx');
const scrollProgress = read('src/components/ui/ScrollProgress.tsx');
const liveStyles = read('src/components/live-data/LiveData.module.css');
const analysisStyles = read('src/components/analysis/AnalysisSidebar.module.css');

assert.match(globals, /--muted: 215 16% 56%/);
assert.match(globals, /:focus-visible/);
assert.match(globals, /prefers-reduced-motion: reduce/);
assert.match(globals, /forced-colors: active/);

for (const dialog of [header, board, setup, responsive]) {
  assert.match(dialog, /role="dialog"|role=\{mobile \? 'dialog' : 'region'\}/);
  assert.match(dialog, /aria-modal/);
  assert.match(dialog, /event\.key === 'Escape'/);
  assert.match(dialog, /event\.key !== 'Tab'/);
}
assert.match(header, /createPortal/);
assert.match(header, /appShell\.inert = true/);
assert.match(header, /returnFocus\?\.focus\(\)/);
assert.match(board, /boardInteraction\.inert = true/);
assert.match(board, /promotionTriggerRef\.current\.focus\(\)/);
assert.match(board, /pieces: ACCESSIBLE_PIECES/);
assert.match(board, /PIECE_NAMES/);
assert.match(resign, /confirmationRef/);
assert.match(resign, /cancelResignation/);

assert.match(toast, /role=\{toast\.type === 'error' \? 'alert' : 'status'\}/);
assert.match(toast, /clearTimeout/);
assert.match(scrollProgress, /requestAnimationFrame/);
assert.match(scrollProgress, /cancelAnimationFrame/);
assert.match(scrollProgress, /aria-hidden="true"/);

assert.match(liveStyles, /\.actions button \{ min-height: 2\.75rem/);
assert.match(analysisStyles, /\.searchControls button \{ min-width: 2\.75rem; min-height: 2\.75rem/);

console.log('frontend accessibility tests passed');
