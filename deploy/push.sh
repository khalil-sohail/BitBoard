#!/bin/bash
set -e

cd "$(dirname "$0")/.."
source "./deploy/.env"

IMAGE_TAG="${IMAGE_TAG:-latest}"
REGISTRY="${REGISTRY:-ghcr.io/khalil-sohail}"

echo "--- Building images locally ---"

docker build \
  -f ./deploy/backend.Dockerfile \
  -t $REGISTRY/chess-backend:$IMAGE_TAG \
  .

docker build \
  -f ./deploy/frontend.Dockerfile \
  --build-arg VIRTUAL_HOST=$VIRTUAL_HOST \
  --build-arg LETSENCRYPT_HOST=$LETSENCRYPT_HOST \
  --build-arg NODE_ENV=production \
  -t $REGISTRY/chess-frontend:$IMAGE_TAG \
  .

echo "--- Pushing images to registry ---"

docker push $REGISTRY/chess-backend:$IMAGE_TAG
docker push $REGISTRY/chess-frontend:$IMAGE_TAG

echo "--- Syncing deploy config to Droplet ($DROPLET_IP) ---"

ssh $SSH_USER@$DROPLET_IP "mkdir -p $REMOTE_DIR/deploy"

rsync -avz \
  --exclude='push.sh' \
  ./deploy/ $SSH_USER@$DROPLET_IP:$REMOTE_DIR/deploy/

echo "--- Deploying on Droplet ($DROPLET_IP) ---"

ssh $SSH_USER@$DROPLET_IP "
  cd $REMOTE_DIR/deploy &&
  echo $CR_PAT | docker login ghcr.io -u $GITHUB_USER --password-stdin &&
  REGISTRY=$REGISTRY IMAGE_TAG=$IMAGE_TAG docker compose pull &&
  REGISTRY=$REGISTRY IMAGE_TAG=$IMAGE_TAG docker compose up -d
"

echo "Deployment successful! Live at https://$VIRTUAL_HOST"
