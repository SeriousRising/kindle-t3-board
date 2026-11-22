# kindle-t3-board

A custom Linux kiosk dashboard for a repurposed **Kindle Touch (`D01200`, 800×600 16-level grayscale E-Ink Pearl + IR touchscreen)** that displays active **T3 Code** project threads, linked PRs, leading agents, and tap-to-inspect subagent breakdowns.

**Live Interactive Browser Demo:** https://seriousrising.github.io/kindle-t3-board/

## Architecture (Option A — Custom Kiosk Userland on Stock Kernel)

1. **`docs/`** — Shareable interactive web simulator (`docs/index.html`) deployed to GitHub Pages with realistic `D01200` hardware bezel, clickable Home bar, Live Sim activity mode, DU Partial vs GC16 Full e-ink refresh, and optional `?bridge=http://127.0.0.1:7373` live mode.
2. **`mockup/`** — Compact 800×600 (4:3) 16-level grayscale e-ink UI prototype (`mockup/index.html`).
2. **`bridge/`** — Zero-dependency Node.js 24+ LAN server (`node:sqlite` read-only + `node:http`) that reads `~/.t3/userdata/statev2.sqlite` (`PRAGMA query_only = ON`), excludes scratch / `No project` threads, assigns 2-letter project badges (`bridge/config.json`), and serves:
   - `GET /api/board` — full JSON payload
   - `GET /api/board.tsv` — compact tab-separated wire format for the C client
   - `GET /api/health` — health check
3. **`client/`** — Zero-dependency native C framebuffer & touch client (`t3board`):
   - Pre-rasterized 16-level antialiased DejaVu Sans & Mono glyph atlas (`gen_fonts.py` → `font_atlas.h`) — zero runtime font libraries needed on Linux 2.6.31.
   - Direct `/dev/fb0` landscape rotation + Freescale i.MX50 EPDC `MXCFB_SEND_UPDATE` ioctls (`DU`/`GL16` partial updates + periodic `GC16` ghosting clear).
   - Auto-detected `/dev/input/event*` IR touch input (`evdev`) with row drill-down, Subagents/PRs tabs, and quick-jump pills.
   - Minimal POSIX TCP HTTP/1.0 client (no OpenSSL/TLS library issues on 2011 Linux).
4. **`kindle/`** — Kiosk launcher (`launch-kiosk.sh`), restore script (`restore-kindle.sh`), and SSH deploy helper (`deploy.sh`).

## Quick Start (Host Simulation)

```bash
# 1. Start the T3 Code bridge (read-only SQLite -> HTTP :7373)
node bridge/server.mjs

# 2. Generate fonts & compile the native C client for your host machine
make -C client host

# 3. Render live T3 data into an 800x600 grayscale PGM screenshot
./client/t3board --no-hw --once --host 127.0.0.1 --port 7373 --dump-pgm /tmp/overview.pgm

# 4. Simulate tapping the first project row (x=200, y=200) to render Project Detail
./client/t3board --no-hw --once --host 127.0.0.1 --port 7373 --tap 200,200 --dump-pgm /tmp/detail.pgm
```

## Deploying to the Kindle Touch (`D01200`)

1. Jailbreak the `D01200` (firmware `<= 5.3.7.3`) via the K5 JailBreak + USBNetwork packages from [kindlemodding.org](https://kindlemodding.org) / MobileRead.
2. Cross-compile a Linux 2.6.31-compatible static ARMv7 binary:
   ```bash
   make -C client kindle ARM_CC=<path-to-2.6-compatible-arm-linux-gnueabi-gcc>
   ```
3. Push and launch kiosk mode:
   ```bash
   chmod +x kindle/*.sh
   ./kindle/deploy.sh <kindle-ip> <your-dev-machine-lan-ip> 7373
   ```

## Customizing Project Badges

Edit `bridge/config.json` → `projectCodes` (changes reload automatically on every request):
```json
"projectCodes": {
  "R6V2Fix": "R6",
  "SM80InferrenceEngine": "S8",
  "kindle-t3-board": "KT"
}
```
