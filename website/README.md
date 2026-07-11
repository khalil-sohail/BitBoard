# Bitboard Website

Next.js frontend plus a Node WebSocket backend that bridges browser clients to
the C++ UCI engine.

## Development

```bash
npm run dev:frontend
```

Starts only the Next.js frontend on the default Next port.

Use `FRONTEND_PORT` in the website environment files or shell to change it:

```bash
FRONTEND_PORT=3100 npm run dev:frontend
```

```bash
npm run dev:backend
```

Starts only the WebSocket backend with `BACKEND_ONLY=true` and
`ENGINE_PATH=../engine/chess-engine`.

Use `BACKEND_PORT` to change it:

```bash
BACKEND_PORT=3101 npm run dev:backend
```

```bash
npm run dev
```

Compatibility alias for `npm run dev:full`, which runs `server.ts` in combined
mode so one process serves both Next.js and `/api/engine`.

Port precedence is `PORT`, then the mode-specific variable, then the default:
`FRONTEND_PORT=3000` for frontend/full-stack mode and `BACKEND_PORT=3001` for
backend-only mode.

The launcher uses Next's environment loader, so local npm commands read
`.env`, `.env.local`, `.env.development`, `.env.development.local`,
`.env.production`, and `.env.production.local` as appropriate. Explicit shell
values are preserved over file values.

```text
dev:frontend      -> FRONTEND_PORT
dev:full          -> FRONTEND_PORT
start:frontend    -> FRONTEND_PORT
start             -> FRONTEND_PORT
start:full        -> FRONTEND_PORT
start:server      -> FRONTEND_PORT (alias for start:full)

dev:backend       -> BACKEND_PORT
start:backend     -> BACKEND_PORT
```

## Build

```bash
npm run build
```

Builds the complete production website package: frontend standalone output plus
the compiled WebSocket backend.

Focused builds are also available:

```bash
npm run build:frontend
npm run build:server
npm run build:all
```

## Production Start

```bash
FRONTEND_PORT=3050 ENGINE_PATH=../engine/chess-engine npm start
```

Starts the complete local production application with the compiled custom
server from `dist/server.js`: Next.js frontend routes and same-origin
`/api/engine` WebSocket upgrades are both served by one process.

```bash
FRONTEND_PORT=3020 npm run start:frontend
```

Prepares `.next/static` and `public` beside the standalone artifact, then starts
`.next/standalone/server.js`. This is frontend-only; it does not install the
custom `/api/engine` upgrade handler. Use it only when a reverse proxy or split
deployment routes `/api/engine` to a backend service.

```bash
BACKEND_PORT=3021 ENGINE_PATH=../engine/chess-engine npm run start:backend
```

Starts the compiled backend from `dist/server.js` with `BACKEND_ONLY=true`.

`npm run start:full` is the explicit full-stack command, and `npm run
start:server` remains a compatibility alias for it. The backend respects
`HOSTNAME`, `PORT`, `BACKEND_PORT`, `BACKEND_ONLY`, `ENGINE_PATH`, and `DEBUG`.

Engine settings in the UI are sent through the page-owned active engine
connection. They persist across new games in the current page session and are
reapplied after reconnect when a new engine process is created.

## Tests

```bash
npm run test:queue
npm run test:protocol
npm run test:server
```

`test:server` runs the focused WebSocket queue and protocol tests.

## Docker

The frontend Dockerfile runs `npm run build:frontend` and starts the copied
standalone artifact with `node server.js`.

The backend Dockerfile builds the engine, runs `npm run build:server`, and
starts with `npm run start:backend`.

Docker Compose reads deployment values from `deploy/.env`, not from
`website/.env.local`. It uses `FRONTEND_PORT` for the frontend container
listener/upstream and `BACKEND_PORT` for the backend container listener,
upstream, and health check:

```bash
FRONTEND_PORT=8180 BACKEND_PORT=8181 docker compose -f ../deploy/docker-compose.yml up -d --build
```

The browser still connects to `/api/engine` on the current host; nginx-proxy
routes that path to the backend internally. nginx remains public on fixed ports
80 and 443, and SSH remains fixed on 22 outside application configuration.
