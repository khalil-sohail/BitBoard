import { spawn } from 'child_process';
import { cpSync, existsSync, mkdirSync } from 'fs';

const standaloneNextDir = '.next/standalone/.next';

mkdirSync(standaloneNextDir, { recursive: true });

if (existsSync('.next/static')) {
  cpSync('.next/static', `${standaloneNextDir}/static`, { recursive: true });
}

if (existsSync('public')) {
  cpSync('public', '.next/standalone/public', { recursive: true });
}

const child = spawn(process.execPath, ['.next/standalone/server.js'], {
  env: process.env,
  stdio: 'inherit',
});

let forwardingSignal = false;
for (const signal of ['SIGINT', 'SIGTERM']) {
  process.on(signal, () => {
    if (child.killed) {
      return;
    }
    forwardingSignal = true;
    child.kill(signal);
  });
}

child.on('exit', (code, signal) => {
  if (signal && !forwardingSignal) {
    process.kill(process.pid, signal);
    return;
  }

  process.exit(code ?? (signal ? 1 : 0));
});

child.on('error', (error) => {
  console.error(error.message);
  process.exit(1);
});
