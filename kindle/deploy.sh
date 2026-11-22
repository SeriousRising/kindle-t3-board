#!/usr/bin/env bash
# Deploy t3board-kindle and kiosk scripts to a jailbroken Kindle Touch over USBNetwork/Wi-Fi SSH.
# Usage: ./kindle/deploy.sh <kindle-ip> [bridge-host-ip] [bridge-port]

set -euo pipefail

KINDLE_IP="${1:-192.168.15.244}"
BRIDGE_HOST="${2:-192.168.15.201}"
BRIDGE_PORT="${3:-7373}"

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT_DIR/client/t3board-kindle"

if [ ! -f "$BIN" ]; then
  echo "Binary $BIN not found. Build it first with: make -C client kindle"
  exit 1
fi

echo "Deploying to root@${KINDLE_IP}:/mnt/us/t3board ..."
ssh "root@${KINDLE_IP}" "mkdir -p /mnt/us/t3board"
scp "$BIN" \
    "$ROOT_DIR/kindle/launch-kiosk.sh" \
    "$ROOT_DIR/kindle/restore-kindle.sh" \
    "root@${KINDLE_IP}:/mnt/us/t3board/"
ssh "root@${KINDLE_IP}" "chmod +x /mnt/us/t3board/t3board-kindle /mnt/us/t3board/*.sh"

echo "Deployed! Start kiosk mode with:"
echo "  ssh root@${KINDLE_IP} '/mnt/us/t3board/launch-kiosk.sh ${BRIDGE_HOST} ${BRIDGE_PORT} 60 270'"
