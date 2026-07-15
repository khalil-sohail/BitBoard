import { strict as assert } from 'node:assert';
import { readFileSync } from 'node:fs';
import { validateFen, validatePgn } from './analysis-setup-validation';

const read = (path: string) => readFileSync(path, 'utf8');
const dialog = read('src/components/setup/SessionSetupDialog.tsx');
const advanced = read('src/components/setup/AdvancedSettings.tsx');
const styles = read('src/components/setup/SessionSetup.module.css');
const view = read('src/session/SessionControllerView.tsx');
const controller = read('src/session/useSessionControllerValue.tsx');
const host = read('src/components/setup/SessionSetupHost.tsx');
const forms = [
  read('src/components/setup/FairPlaySetupForm.tsx'),
  read('src/components/setup/TrainingSetupForm.tsx'),
  read('src/components/setup/AnalysisSetupForm.tsx'),
].join('\n');

assert.match(host, /session\.mode\.isAnalysis/);
assert.match(host, /<AnalysisSetupForm/);
assert.match(host, /session\.mode\.isTraining/);
assert.match(host, /<TrainingSetupForm/);
assert.match(host, /<FairPlaySetupForm/);

assert.equal(validateFen(''), 'Enter a FEN position.');
assert.ok(validateFen('not a fen'));
assert.equal(validateFen('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'), null);
assert.equal(validatePgn(''), 'Paste a PGN game.');
assert.ok(validatePgn('not a pgn'));
assert.equal(validatePgn('1. e4 e5 2. Nf3 Nc6'), null);

assert.match(dialog, /previouslyFocused/);
assert.match(dialog, /previouslyFocused\?\.focus\(\)/);
assert.match(dialog, /event\.key === 'Escape'/);
assert.match(dialog, /event\.key !== 'Tab'/);
assert.match(dialog, /aria-modal="true"/);
assert.match(dialog, /document\.body\.style\.overflow = 'hidden'/);
assert.match(dialog, /appShell\.inert = true/);
assert.doesNotMatch(dialog, /event\.key === 'Enter'/);
assert.match(styles, /@media \(max-width: 74\.999rem\)/);
assert.match(styles, /height: 100dvh/);
assert.match(styles, /max-height: calc\(100dvh - 2rem\)/);

assert.match(advanced, /aria-expanded=\{expanded\}/);
assert.match(advanced, /hidden=\{!expanded\}/);
assert.doesNotMatch(advanced, /expanded &&/);

assert.match(view, /<SessionSetupHost/);
assert.match(view, /onClick=\{setup\.open\}/);
assert.match(view, /sidebar=\{lifecycle\.status === 'idle' \? null/);
assert.doesNotMatch(view, /NewGameModal|PositionSetup|EngineToggle/);
assert.match(view, /<AnalysisSearchControls/);
assert.match(controller, /setIsSetupOpen\(false\);\n    stopEngine\(\)/);
assert.doesNotMatch(controller, /isNewGameModalOpen|setIsNewGameModalOpen/);
assert.match(controller, /startFromDefault: startAnalysisFromDefault/);
assert.match(controller, /startFromFen: startAnalysisFromFen/);
assert.match(controller, /startFromPgn: startAnalysisFromPgn/);

assert.doesNotMatch(forms, /useEngine\(|useChessGame\(|useChessClock\(/);
assert.match(forms, /setup\.start\(/);
assert.match(forms, /analysis\.startFromDefault\(\)/);
assert.match(forms, /analysis\.startFromFen\(fen\.trim\(\)\)/);
assert.match(forms, /analysis\.startFromPgn\(pgn\)/);

console.log('session setup tests passed');
