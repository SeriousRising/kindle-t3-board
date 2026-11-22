#!/bin/sh
# Restore stock Kindle Touch UI after running t3board kiosk mode.

killall t3board-kindle 2>/dev/null || true
lipc-set-prop com.lab126.powerd preventScreenSaver 0 2>/dev/null || true
start framework 2>/dev/null || /etc/init.d/framework start 2>/dev/null || true
start lab126_gui 2>/dev/null || true
echo "Kindle UI restored."
