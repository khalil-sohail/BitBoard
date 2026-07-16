import { strict as assert } from 'assert';
import { readFileSync } from 'fs';

function readSource(path: string): string {
  return readFileSync(path, 'utf8');
}

function testPageOwnsSingleEngineHook(): void {
  const page = readSource('src/app/page.tsx');
  const source = readSource('src/session/useSessionControllerValue.tsx');
  const engineHook = readSource('src/hooks/useEngine.ts');
  const matches = source.match(/useEngine\(\)/g) ?? [];
  assert.equal(matches.length, 1);
  assert.equal(page.includes('useEngine('), false);
  assert.ok(page.includes('SessionControllerProvider'));
  assert.match(engineHook, /const infoIdentity = \{/);
  assert.match(engineHook, /setEngineInfo\(prev => \(\{\s*requestId: data\.requestId,\s*\.\.\.infoIdentity,/);
}

function testPresentationLayerDoesNotCreateSession(): void {
  const presentation = [
    'src/components/responsive/ResponsiveSessionPanel.tsx',
    'src/components/setup/SessionSetupHost.tsx',
    'src/components/fair-play/FairPlaySidebar.tsx',
    'src/components/training/TrainingSidebar.tsx',
    'src/components/analysis/AnalysisSidebar.tsx',
  ].map(readSource).join('\n');
  assert.equal(presentation.includes('useEngine('), false);
  assert.equal(presentation.includes('useChessGame('), false);
  assert.equal(presentation.includes('useChessClock('), false);
  assert.equal(presentation.includes('resultAck'), false);
}

function run(): void {
  testPageOwnsSingleEngineHook();
  testPresentationLayerDoesNotCreateSession();

  console.log('engine ownership tests passed');
}

run();
