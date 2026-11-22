#!/bin/sh
# Custom Kiosk OS Launcher for Kindle Touch (D01200 / K5, Firmware 5.3.7.3)
# Stops Amazon's Java/X11 framework, disables screensaver, runs t3board directly
# on /dev/fb0 + /dev/input/event*, and cleanly restores Amazon UI on exit.

set -u

BRIDGE_HOST="${1:-192.168.1.100}"
BRIDGE_PORT="${2:-7373}"
INTERVAL="${3:-60}"
ROTATE="${4:-270}"

SRC_BIN="/mnt/us/t3board/t3board-kindle"
BIN="/var/tmp/t3board-kindle"
LOG="/mnt/us/t3board/t3board.log"

if [ ! -f "$SRC_BIN" ]; then
    echo "Missing binary: $SRC_BIN"
    exit 1
fi

# Copy off FAT32 (/mnt/us may be mounted noexec) into tmpfs
cp "$SRC_BIN" "$BIN"
chmod +x "$BIN"

restore_ui() {
    echo "[kiosk] Restoring Kindle framework..." >> "$LOG"
    lipc-set-prop com.lab126.powerd preventScreenSaver 0 2>/dev/null || true
    start framework 2>/dev/null || /etc/init.d/framework start 2>/dev/null || true
    start lab126_gui 2>/dev/null || true
}

trap restore_ui INT TERM EXIT

echo "[kiosk] Starting T3 Board kiosk (bridge=${BRIDGE_HOST}:${BRIDGE_PORT})" > "$LOG"

# Keep Wi-Fi awake & prevent screensaver/sleep
lipc-set-prop com.lab126.powerd preventScreenSaver 1 2>/dev/null || true
lipc-set-prop com.lab126.wifid enable 1 2>/dev/null || true

# Stop Amazon's heavy Java/X11 userland (~150 MB RAM freed!)
stop lab126_gui 2>/dev/null || true
stop framework 2>/dev/null || /etc/init.d/framework stop 2>/dev/null || true
sleep 1

"$BIN" \
    --host "$BRIDGE_HOST" \
    --port "$BRIDGE_PORT" \
    --interval "$INTERVAL" \
    --rotate "$ROTATE" \
    >> "$LOG" 2>&1
