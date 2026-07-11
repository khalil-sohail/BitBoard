# Builder Stage — compile the C++ engine
FROM debian:bookworm-slim AS engine-builder

RUN apt-get update && apt-get install -y build-essential make g++

WORKDIR /app/engine
COPY engine/ ./

# Use a generic x86-64 baseline for production images so builds do not
# inherit host-native CPU instructions from the Docker build machine.
ARG ENGINE_ARCH_FLAGS="-march=x86-64"
RUN make -j$(nproc) ARCH_FLAGS="${ENGINE_ARCH_FLAGS}"

# Runner Stage
FROM node:20-bookworm-slim

WORKDIR /app

# Copy the compiled engine binary and openings book
COPY --from=engine-builder /app/engine/chess-engine /app/engine/chess-engine
COPY --from=engine-builder /app/engine/openings /app/engine/openings

WORKDIR /app/website
COPY website/package*.json ./

# Install ALL deps (need TypeScript to compile server.ts)
RUN npm ci

COPY website/ ./

# Compile server.ts to dist/server.js — no ts-node at runtime
RUN npm run build:server

# Default backend WebSocket port. Runtime can override with PORT/BACKEND_PORT.
EXPOSE 3001

CMD ["npm", "run", "start:backend"]
