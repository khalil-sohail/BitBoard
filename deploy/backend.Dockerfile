# Builder Stage — compile the C++ engine
FROM debian:bookworm-slim AS engine-builder

RUN apt-get update && apt-get install -y build-essential make

WORKDIR /app/engine
COPY engine/ ./
RUN make -j$(nproc)

# Runner Stage
FROM node:20-bookworm-slim

WORKDIR /app

# Copy the compiled engine binary and openings book
COPY --from=engine-builder /app/engine/chess-engine /app/engine/chess-engine
COPY --from=engine-builder /app/engine/openings /app/engine/openings

WORKDIR /app/website
COPY website/package*.json ./

# Install ALL deps (need typescript and ts-node to compile server.ts)
RUN npm ci

COPY website/ ./

# Compile server.ts to dist/server.js — no ts-node at runtime
RUN npx tsc --project tsconfig.server.json

# Expose backend WebSocket port
EXPOSE 8080

CMD ["npm", "run", "start:server"]
