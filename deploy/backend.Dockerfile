# Builder Stage
FROM debian:bullseye-slim AS builder

RUN apt-get update && apt-get install -y build-essential make

WORKDIR /app/engine
COPY engine/ ./
RUN make

# Runner Stage
FROM node:18-bullseye-slim

WORKDIR /app

# Copy the compiled engine and openings
COPY --from=builder /app/engine/chess-engine /app/engine/chess-engine
COPY --from=builder /app/engine/openings /app/engine/openings

WORKDIR /app/website
COPY website/package*.json ./

# Install dependencies (using npm ci for predictability)
RUN npm ci

COPY website/ ./

# Expose backend port
EXPOSE 8080

# Run the backend server using the existing ts-node script
CMD ["npm", "run", "dev:server"]
