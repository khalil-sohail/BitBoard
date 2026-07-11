import { loadEnvConfig } from '@next/env';

export const DEFAULT_FRONTEND_PORT = 3000;
export const DEFAULT_BACKEND_PORT = 3001;

export type ServerMode = 'frontend' | 'backend' | 'full-stack';
export type ServerPortSource = 'PORT' | 'FRONTEND_PORT' | 'BACKEND_PORT' | 'default';

export interface PortEnv {
  [key: string]: string | undefined;
  PORT?: string;
  FRONTEND_PORT?: string;
  BACKEND_PORT?: string;
  BACKEND_ONLY?: string;
}

export interface ServerPortConfig {
  mode: ServerMode;
  port: number;
  source: ServerPortSource;
}

export interface LoadedServerEnvironment {
  loadedFiles: string[];
}

export function parsePort(
  value: string | undefined,
  fallback: number,
  variableName: string,
): number {
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

export function resolveServerMode(env: PortEnv = process.env): ServerMode {
  return env.BACKEND_ONLY === 'true' ? 'backend' : 'full-stack';
}

export function resolveServerPortConfig(
  mode: ServerMode,
  env: PortEnv = process.env,
): ServerPortConfig {
  const specificName = mode === 'backend' ? 'BACKEND_PORT' : 'FRONTEND_PORT';
  const fallback = mode === 'backend' ? DEFAULT_BACKEND_PORT : DEFAULT_FRONTEND_PORT;
  const explicitPort = env.PORT;

  if (explicitPort !== undefined) {
    return {
      mode,
      port: parsePort(explicitPort, fallback, 'PORT'),
      source: 'PORT',
    };
  }

  if (env[specificName] !== undefined) {
    return {
      mode,
      port: parsePort(env[specificName], fallback, specificName),
      source: specificName,
    };
  }

  return {
    mode,
    port: fallback,
    source: 'default',
  };
}

export function resolveServerPort(
  mode: ServerMode,
  env: PortEnv = process.env,
): number {
  return resolveServerPortConfig(mode, env).port;
}

export function resolveRuntimePort(env: PortEnv = process.env): number {
  return resolveServerPort(resolveServerMode(env), env);
}

export function loadServerEnvironment(
  projectDir: string,
  devMode: boolean,
  forceReload = false,
): LoadedServerEnvironment {
  const result = loadEnvConfig(projectDir, devMode, console, forceReload);
  return {
    loadedFiles: result.loadedEnvFiles.map((file) => file.path),
  };
}
