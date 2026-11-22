#include "fb_epdc.h"
#include "http_client.h"
#include "input_touch.h"
#include "ui.h"

#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static void read_battery(char *out, size_t out_sz) {
    static const char *paths[] = {
        "/sys/devices/system/yoshi_battery/yoshi_battery0/battery_capacity",
        "/sys/class/power_supply/mc13892_bat/capacity",
        "/sys/class/power_supply/BAT0/capacity",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        FILE *fp = fopen(paths[i], "r");
        if (!fp) continue;
        int pct = -1;
        if (fscanf(fp, "%d", &pct) == 1 && pct >= 0 && pct <= 100) {
            fclose(fp);
            snprintf(out, out_sz, "BAT %d%%", pct);
            return;
        }
        fclose(fp);
    }
    snprintf(out, out_sz, "AC");
}

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  --host <ip>          Bridge host (default: 127.0.0.1)\n"
            "  --port <port>        Bridge port (default: 7373)\n"
            "  --interval <sec>     Refresh interval in seconds (default: 60)\n"
            "  --rotate <90|270>    Landscape rotation (default: 270)\n"
            "  --invert             Invert grayscale values sent to /dev/fb0\n"
            "  --touch <dev>        Override evdev touch device path\n"
            "  --no-hw              Run in host-only mode without /dev/fb0 or evdev\n"
            "  --once               Fetch once, render, optionally dump PGM, and exit\n"
            "  --tap <x,y>          Simulate tap in --once mode (repeatable)\n"
            "  --dump-pgm <path>    Write rendered 800x600 grayscale frame to PGM\n",
            prog);
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    int port = 7373;
    int interval_sec = 60;
    int rotate_deg = 270;
    bool invert = false;
    bool use_hw = true;
    bool once = false;
    const char *touch_dev = NULL;
    const char *dump_pgm = NULL;

    int sim_taps_x[8], sim_taps_y[8];
    int sim_tap_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            interval_sec = atoi(argv[++i]);
            if (interval_sec < 5) interval_sec = 5;
        } else if (strcmp(argv[i], "--rotate") == 0 && i + 1 < argc) {
            rotate_deg = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--invert") == 0) {
            invert = true;
        } else if (strcmp(argv[i], "--touch") == 0 && i + 1 < argc) {
            touch_dev = argv[++i];
        } else if (strcmp(argv[i], "--no-hw") == 0) {
            use_hw = false;
        } else if (strcmp(argv[i], "--once") == 0) {
            once = true;
        } else if (strcmp(argv[i], "--dump-pgm") == 0 && i + 1 < argc) {
            dump_pgm = argv[++i];
        } else if (strcmp(argv[i], "--tap") == 0 && i + 1 < argc) {
            if (sim_tap_count < 8) {
                int tx = 0, ty = 0;
                if (sscanf(argv[++i], "%d,%d", &tx, &ty) == 2) {
                    sim_taps_x[sim_tap_count] = tx;
                    sim_taps_y[sim_tap_count] = ty;
                    sim_tap_count++;
                }
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    Framebuffer fb;
    if (fb_init(&fb, use_hw, rotate_deg, invert) < 0) {
        if (use_hw) {
            fprintf(stderr, "[main] Hardware /dev/fb0 unavailable; continuing in offscreen mode.\n");
            fb_init(&fb, false, rotate_deg, invert);
        }
    }

    TouchInput touch;
    memset(&touch, 0, sizeof(touch));
    touch.fd = -1;
    if (use_hw && !once) {
        if (touch_init(&touch, touch_dev, rotate_deg) < 0) {
            fprintf(stderr, "[main] Touch input not detected; running display-only.\n");
        }
    }

    BoardState board;
    memset(&board, 0, sizeof(board));
    UiState ui;
    ui_init(&ui, interval_sec);
    read_battery(ui.battery_str, sizeof(ui.battery_str));

    /* Initial fetch + full GC16 draw */
    (void)board_fetch_http(host, port, "/api/board.tsv", &board);
    ui_render(&ui, &fb, &board);
    fb_flush(&fb, EPDC_REFRESH_FULL_GC16);

    /* Apply simulated taps if provided */
    for (int t = 0; t < sim_tap_count; t++) {
        EpdcRefreshMode mode = EPDC_REFRESH_PARTIAL_GL16;
        if (ui_handle_tap(&ui, &board, sim_taps_x[t], sim_taps_y[t], &mode)) {
            ui_render(&ui, &fb, &board);
            fb_flush(&fb, mode);
        }
    }

    if (dump_pgm) {
        if (fb_save_pgm(&fb, dump_pgm) == 0) {
            fprintf(stderr, "[main] Wrote %s (800x600 grayscale)\n", dump_pgm);
        }
    }

    if (once) {
        touch_close(&touch);
        fb_close(&fb);
        return 0;
    }

    long next_refresh = now_ms() + (long)interval_sec * 1000L;

    while (g_running) {
        long now = now_ms();
        int timeout_ms = (int)(next_refresh - now);
        if (timeout_ms < 50) timeout_ms = 50;

        if (touch.fd >= 0) {
            struct pollfd pfd;
            pfd.fd = touch.fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            int prc = poll(&pfd, 1, timeout_ms);
            if (prc > 0 && (pfd.revents & POLLIN)) {
                TouchTap tap;
                if (touch_poll_tap(&touch, &tap)) {
                    fprintf(stderr, "[touch] tap at (%d, %d)\n", tap.x, tap.y);
                    EpdcRefreshMode mode = EPDC_REFRESH_PARTIAL_GL16;
                    if (ui_handle_tap(&ui, &board, tap.x, tap.y, &mode)) {
                        ui_render(&ui, &fb, &board);
                        if (mode == EPDC_REFRESH_FULL_GC16) {
                            ui.partial_count = 0;
                        } else {
                            ui.partial_count++;
                        }
                        fb_flush(&fb, mode);
                    }
                }
            }
        } else {
            int sleep_ms = timeout_ms > 500 ? 500 : timeout_ms;
            struct timespec ts;
            ts.tv_sec = sleep_ms / 1000;
            ts.tv_nsec = (sleep_ms % 1000) * 1000000L;
            nanosleep(&ts, NULL);
        }

        if (now_ms() >= next_refresh) {
            read_battery(ui.battery_str, sizeof(ui.battery_str));
            (void)board_fetch_http(host, port, "/api/board.tsv", &board);
            ui_render(&ui, &fb, &board);

            /* Full GC16 flash every 10 partial refreshes to keep Pearl screen crisp */
            EpdcRefreshMode mode = EPDC_REFRESH_PARTIAL_GL16;
            if (++ui.partial_count >= 10) {
                mode = EPDC_REFRESH_FULL_GC16;
                ui.partial_count = 0;
            }
            fb_flush(&fb, mode);
            if (dump_pgm) {
                (void)fb_save_pgm(&fb, dump_pgm);
            }
            next_refresh = now_ms() + (long)interval_sec * 1000L;
        }
    }

    touch_close(&touch);
    fb_close(&fb);
    return 0;
}
