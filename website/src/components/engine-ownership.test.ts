import { strict as assert } from 'assert';
import { readFileSync } from 'fs';

function readSource(path: string): string {
  return readFileSync(path, 'utf8');
}

function testPageOwnsSingleEngineHook(): void {
  const source = readSource('src/app/page.tsx');
  const matches = source.match(/useEngine\(/g) ?? [];
  assert.equal(matches.length, 1);
}

function testEngineToggleDoesNotCreateSession(): void {
  const source = readSource('src/components/panels/EngineToggle.tsx');
  assert.equal(source.includes("from '../../hooks/useEngine'"), false);
  assert.equal(source.includes('useEngine('), false);
}

function testEngineToggleReceivesActiveSessionActions(): void {
  const source = readSource('src/components/panels/EngineToggle.tsx');
  assert.ok(source.includes('ownBook: boolean'));
  assert.ok(source.includes('optionsDisabled: boolean'));
  assert.ok(source.includes('onOwnBookChange: (enabled: boolean) => void'));
}

function run(): void {
  testPageOwnsSingleEngineHook();
  testEngineToggleDoesNotCreateSession();
  testEngineToggleReceivesActiveSessionActions();

  console.log('engine ownership tests passed');
}

run();
