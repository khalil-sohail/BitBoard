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
RUN npm run build

FROM node:20-bullseye-slim AS runner

WORKDIR /app
ENV NODE_ENV=production

COPY --from=builder /app/public ./public
COPY --from=builder /app/.next/standalone ./
COPY --from=builder /app/.next/static ./.next/static

EXPOSE 3000

CMD ["node", "server.js"]