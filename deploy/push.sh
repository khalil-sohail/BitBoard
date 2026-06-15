#!/bin/bash
set -e

# Configuration
# Replace this with your actual DigitalOcean Droplet IP
DROPLET_IP="<YOUR_DROPLET_IP>"
SSH_USER="root"
REMOTE_DIR="/opt/chess-engine"

echo "Syncing local repository to DigitalOcean Droplet ($DROPLET_IP)..."

# Sync files using rsync, excluding local builds and dependencies
rsync -avz --delete \
  --exclude='node_modules' \
  --exclude='.git' \
  --exclude='.next' \
  --exclude='deploy/push.sh' \
  ../ $SSH_USER@$DROPLET_IP:$REMOTE_DIR

echo "Sync complete. Building and restarting containers remotely..."

# Execute remote docker-compose deployment
ssh $SSH_USER@$DROPLET_IP "cd $REMOTE_DIR/deploy && docker compose up -d --build"

echo "Deployment successful! Your app should be live at https://engine-room.ksohail.com shortly."
