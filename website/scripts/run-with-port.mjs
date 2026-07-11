import { spawn } from 'child_process';
import nextEnv from '@next/env';

const { loadEnvConfig } = nextEnv;

const DEFAULTS = {
  frontend: 3000,
  backend: 3001,
  'full-stack': 3000,
};

const PORT_VARIABLES = {
  frontend: 'FRONTEND_PORT',
  backend: 'BACKEND_PORT',
  'full-stack': 'FRONTEND_PORT',
};

function parsePort(value, fallback, variableName) {
  if (value === undefined) {
    return fallback;
  }

  if (value === '' || value !== value.trim() || !/^\d+$/.test(value)) {
    throw new Error(`${variableName} must be an integer between 1 and 65535.`);
  }

  const port = Number(value);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error(`${variableName} must be an integer between 1 and 65535.`);
  }

  return port;
}

function usage() {
  console.error(
    'Usage: node scripts/run-with-port.mjs <frontend|backend|full-stack> <development|production> [KEY=value ...] -- <command> [args...]',
  );
}

function applyAssignment(argument) {
  const separator = argument.indexOf('=');
  if (separator <= 0) {
    return false;
  }

  const key = argument.slice(0, separator);
  if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(key)) {
    return false;
  }

  process.env[key] = argument.slice(separator + 1);
  return true;
}

const args = process.argv.slice(2);
const mode = args.shift();
const runtime = args.shift();
const separatorIndex = args.indexOf('--');

if (
  !Object.hasOwn(DEFAULTS, mode ?? '') ||
  !['development', 'production'].includes(runtime ?? '') ||
  separatorIndex === -1
) {
  usage();
  process.exit(1);
}

const assignments = args.slice(0, separatorIndex);
const command = args.slice(separatorIndex + 1);

if (command.length === 0 || assignments.some((assignment) => !applyAssignment(assignment))) {
  usage();
  process.exit(1);
}

if (process.env.NODE_ENV === undefined) {
  process.env.NODE_ENV = runtime;
}

loadEnvConfig(process.cwd(), runtime === 'development');

const specificName = PORT_VARIABLES[mode];
const fallback = DEFAULTS[mode];
const port = process.env.PORT !== undefined
  ? parsePort(process.env.PORT, fallback, 'PORT')
  : parsePort(process.env[specificName], fallback, specificName);

process.env.PORT = String(port);

const child = spawn(command[0], command.slice(1), {
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
