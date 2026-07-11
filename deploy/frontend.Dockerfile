FROM node:20-bullseye-slim AS builder

WORKDIR /app
COPY website/package*.json ./
RUN npm ci


ARG VIRTUAL_HOST
ARG LETSENCRYPT_HOST
ARG NODE_ENV=production

ENV VIRTUAL_HOST=$VIRTUAL_HOST
ENV LETSENCRYPT_HOST=$LETSENCRYPT_HOST
ENV NODE_ENV=$NODE_ENV
ENV NEXT_PRIVATE_STANDALONE=true

COPY website/ ./
RUN npm run build:frontend

FROM node:20-bullseye-slim AS runner

WORKDIR /app
ENV NODE_ENV=production

COPY --from=builder /app/public ./public
COPY --from=builder /app/.next/standalone ./
COPY --from=builder /app/.next/static ./.next/static
COPY --from=builder /app/scripts ./scripts

# Default frontend port. Runtime can override with PORT/FRONTEND_PORT.
EXPOSE 3000

# The standalone artifact is copied to /app; the launcher resolves PORT from
# runtime env before starting the generated server.
CMD ["node", "scripts/run-with-port.mjs", "frontend", "production", "--", "node", "server.js"]
