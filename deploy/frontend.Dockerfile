FROM node:18-bullseye-slim AS builder

WORKDIR /app
COPY website/package*.json ./
RUN npm ci

COPY website/ ./
RUN npm run build

FROM node:18-bullseye-slim AS runner

WORKDIR /app
ENV NODE_ENV=production

# Copy necessary files for Next.js standalone mode
COPY --from=builder /app/public ./public
COPY --from=builder /app/.next/standalone ./
COPY --from=builder /app/.next/static ./.next/static

EXPOSE 3000

CMD ["node", "server.js"]
