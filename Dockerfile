# ============================================================
# Stage 1: Compile the C++ chess engine
# ============================================================
FROM debian:bookworm-slim AS engine-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build/engine
COPY engine/ .

# Build optimized production binary (no sanitizers, no debug)
RUN g++ -Wall -Wextra -I./include -std=c++23 -O2 -DNDEBUG \
    src/*.cpp src/app/*.cpp src/eval/*.cpp src/search/*.cpp src/move/*.cpp \
    -o chess-engine

# ============================================================
# Stage 2: Build the Next.js website
# ============================================================
FROM node:20-slim AS website-builder

WORKDIR /build/website
COPY website/package.json website/package-lock.json ./
RUN npm ci --production=false

COPY website/ .
RUN npm run build

# ============================================================
# Stage 3: Production runtime
# ============================================================
FROM node:20-slim AS production

# Install minimal C++ runtime libs (libstdc++ for the engine binary)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy compiled engine binary + opening book
COPY --from=engine-builder /build/engine/chess-engine ./engine/chess-engine
COPY --from=engine-builder /build/engine/openings/ ./engine/openings/

# Copy Next.js standalone build
COPY --from=website-builder /build/website/.next/standalone ./
COPY --from=website-builder /build/website/.next/static ./.next/static
COPY --from=website-builder /build/website/public ./public

# Engine binary must be executable
RUN chmod +x ./engine/chess-engine

ENV NODE_ENV=production
ENV PORT=3000
ENV ENGINE_PATH=./engine/chess-engine
ENV ENGINE_BOOK_PATH=./engine/openings

EXPOSE 3000

CMD ["node", "server.js"]
