import { strict as assert } from 'assert';
import { mkdtempSync, rmSync, writeFileSync } from 'fs';
import { tmpdir } from 'os';
import { join } from 'path';
import { resetEnv, updateInitialEnv } from '@next/env';
import {
  DEFAULT_BACKEND_PORT,
  DEFAULT_FRONTEND_PORT,
  parsePort,
  resolveRuntimePort,
  resolveServerPort,
  resolveServerPortConfig,
  loadServerEnvironment,
} from './server-config';

function assertRejectsPort(value: string, variableName: string): void {
  assert.throws(
    () => parsePort(value, 1234, variableName),
    new RegExp(`${variableName} must be an integer between 1 and 65535\\.`),
  );
}

function assertRejectsResolvedPort(
  mode: 'frontend' | 'backend' | 'full-stack',
  env: Record<string, string>,
  variableName: string,
): void {
  assert.throws(
    () => resolveServerPort(mode, env),
    new RegExp(`${variableName} must be an integer between 1 and 65535\\.`),
  );
}

function testDefaults(): void {
  assert.equal(resolveServerPort('frontend', {}), DEFAULT_FRONTEND_PORT);
  assert.equal(resolveServerPort('full-stack', {}), DEFAULT_FRONTEND_PORT);
  assert.equal(resolveServerPort('backend', {}), DEFAULT_BACKEND_PORT);
  assert.deepEqual(resolveServerPortConfig('frontend', {}), {
    mode: 'frontend',
    port: DEFAULT_FRONTEND_PORT,
    source: 'default',
  });
}

function testModeSpecificValues(): void {
  assert.equal(resolveServerPort('frontend', { FRONTEND_PORT: '3100' }), 3100);
  assert.equal(resolveServerPort('full-stack', { FRONTEND_PORT: '3102' }), 3102);
  assert.equal(resolveServerPort('backend', { BACKEND_PORT: '3101' }), 3101);
}

function testExplicitPortOverride(): void {
  assert.equal(
    resolveServerPort('frontend', { PORT: '3130', FRONTEND_PORT: '3100' }),
    3130,
  );
  assert.equal(
    resolveServerPort('backend', { PORT: '3132', BACKEND_PORT: '3131' }),
    3132,
  );
  assert.equal(resolveServerPort('full-stack', { PORT: '3133' }), 3133);
}

function testIsolation(): void {
  assert.equal(resolveServerPort('frontend', { BACKEND_PORT: '3201' }), DEFAULT_FRONTEND_PORT);
  assert.equal(resolveServerPort('backend', { FRONTEND_PORT: '3200' }), DEFAULT_BACKEND_PORT);
  assert.equal(resolveServerPort('full-stack', { BACKEND_PORT: '3201' }), DEFAULT_FRONTEND_PORT);
}

function testInvalidPorts(): void {
  assertRejectsResolvedPort('frontend', { PORT: '' }, 'PORT');
  assertRejectsResolvedPort('frontend', { FRONTEND_PORT: '' }, 'FRONTEND_PORT');

  for (const value of ['0', '-1', '3000.5', '3e3', '0xBB8', 'abc', '65536']) {
    assertRejectsPort(value, 'PORT');
  }

  assertRejectsPort('   ', 'PORT');
  assertRejectsPort(' 3000', 'PORT');
  assertRejectsPort('3000 ', 'PORT');
  assertRejectsPort('NaN', 'PORT');
  assertRejectsPort('Infinity', 'PORT');
  assertRejectsPort('3000abc', 'PORT');
}

function loadedFilesFor(
  files: Record<string, string>,
  devMode: boolean,
  env: Record<string, string | undefined> = {},
): string[] {
  const dir = mkdtempSync(join(tmpdir(), 'bitboard-env-'));
  const managedKeys = [
    'PORT',
    'FRONTEND_PORT',
    'BACKEND_PORT',
    'NODE_ENV',
    '__NEXT_PROCESSED_ENV',
  ];
  const originalEnv = new Map(managedKeys.map((key) => [key, process.env[key]]));

  try {
    for (const [file, contents] of Object.entries(files)) {
      writeFileSync(join(dir, file), contents);
    }

    for (const key of managedKeys) {
      delete process.env[key];
    }

    Object.assign(process.env, {
      NODE_ENV: devMode ? 'development' : 'production',
      ...env,
    });
    updateInitialEnv({
      PORT: process.env.PORT,
      FRONTEND_PORT: process.env.FRONTEND_PORT,
      BACKEND_PORT: process.env.BACKEND_PORT,
      NODE_ENV: process.env.NODE_ENV,
      __NEXT_PROCESSED_ENV: undefined,
    });

    return loadServerEnvironment(dir, devMode, true).loadedFiles;
  } finally {
    resetEnv();
    for (const key of managedKeys) {
      const value = originalEnv.get(key);
      if (value === undefined) {
        delete process.env[key];
      } else {
        process.env[key] = value;
      }
    }
    rmSync(dir, { recursive: true, force: true });
  }
}

function testLoadingBehavior(): void {
  assert.equal(resolveServerPort('frontend', { FRONTEND_PORT: '5200' }), 5200);

  assert.deepEqual(
    loadedFilesFor({
      '.env': 'FRONTEND_PORT=5100\n',
      '.env.development': 'FRONTEND_PORT=5101\n',
    }, true),
    ['.env.development', '.env'],
  );

  assert.deepEqual(
    loadedFilesFor({
      '.env': 'FRONTEND_PORT=5100\n',
      '.env.production': 'FRONTEND_PORT=5102\n',
    }, false),
    ['.env.production', '.env'],
  );

  assert.deepEqual(
    loadedFilesFor({
      '.env': 'FRONTEND_PORT=5100\n',
    }, true),
    ['.env'],
  );

  assert.deepEqual(
    loadedFilesFor({
      '.env': 'FRONTEND_PORT=5100\n',
      '.env.local': 'FRONTEND_PORT=5101\n',
      '.env.development': 'FRONTEND_PORT=5102\n',
      '.env.development.local': 'FRONTEND_PORT=5103\n',
    }, true),
    ['.env.development.local', '.env.local', '.env.development', '.env'],
  );
}

function testRuntimeModeSelection(): void {
  assert.equal(
    resolveRuntimePort({ BACKEND_ONLY: 'true', BACKEND_PORT: '3201', FRONTEND_PORT: '3200' }),
    3201,
  );
  assert.equal(
    resolveRuntimePort({ FRONTEND_PORT: '3200', BACKEND_PORT: '3201' }),
    3200,
  );
}

testDefaults();
testModeSpecificValues();
testExplicitPortOverride();
testIsolation();
testInvalidPorts();
testLoadingBehavior();
testRuntimeModeSelection();

console.log('server-config tests passed');
