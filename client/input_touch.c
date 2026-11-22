#include "input_touch.h"
#include "fb_epdc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/input.h>
#endif

static inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

#ifdef __linux__
#define TEST_BIT(bit, arr) (((arr)[(bit) / 8] >> ((bit) % 8)) & 1)

static bool probe_touch_fd(int fd, TouchInput *ti) {
    unsigned char ev_bits[(EV_MAX / 8) + 1] = {0};
    unsigned char abs_bits[(ABS_MAX / 8) + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0) return false;
    if (!TEST_BIT(EV_ABS, ev_bits)) return false;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0) return false;

    bool has_mt = TEST_BIT(ABS_MT_POSITION_X, abs_bits) && TEST_BIT(ABS_MT_POSITION_Y, abs_bits);
    bool has_st = TEST_BIT(ABS_X, abs_bits) && TEST_BIT(ABS_Y, abs_bits);
    if (!has_mt && !has_st) return false;

    struct input_absinfo ax, ay;
    memset(&ax, 0, sizeof(ax));
    memset(&ay, 0, sizeof(ay));

    if (has_mt) {
        ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax);
        ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay);
        ti->use_mt = true;
    } else {
        ioctl(fd, EVIOCGABS(ABS_X), &ax);
        ioctl(fd, EVIOCGABS(ABS_Y), &ay);
        ti->use_mt = false;
    }

    ti->min_x = ax.minimum;
    ti->max_x = (ax.maximum > ax.minimum) ? ax.maximum : 600;
    ti->min_y = ay.minimum;
    ti->max_y = (ay.maximum > ay.minimum) ? ay.maximum : 800;
    return true;
}
#endif

int touch_init(TouchInput *ti, const char *device_override, int rotate_deg) {
    memset(ti, 0, sizeof(*ti));
    ti->fd = -1;
    ti->rotate_deg = (rotate_deg == 90) ? 90 : 270;

#ifdef __linux__
    if (device_override && *device_override) {
        int fd = open(device_override, O_RDONLY | O_NONBLOCK);
        if (fd >= 0 && probe_touch_fd(fd, ti)) {
            ti->fd = fd;
            fprintf(stderr, "[touch] Opened %s (%s, x=%d..%d y=%d..%d)\n",
                    device_override, ti->use_mt ? "MT" : "ST",
                    ti->min_x, ti->max_x, ti->min_y, ti->max_y);
            return 0;
        }
        if (fd >= 0) close(fd);
        return -1;
    }

    for (int i = 0; i < 8; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        char name[64] = "unknown";
        (void)ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        if (probe_touch_fd(fd, ti)) {
            ti->fd = fd;
            fprintf(stderr, "[touch] Auto-detected %s (%s, %s, x=%d..%d y=%d..%d)\n",
                    path, name, ti->use_mt ? "MT" : "ST",
                    ti->min_x, ti->max_x, ti->min_y, ti->max_y);
            return 0;
        }
        close(fd);
    }
#endif
    return -1;
}

void touch_close(TouchInput *ti) {
    if (ti->fd >= 0) {
        close(ti->fd);
        ti->fd = -1;
    }
}

static void map_to_landscape(const TouchInput *ti, int raw_x, int raw_y, TouchTap *out) {
    /* Normalize raw portrait panel coords -> phys (0..599, 0..799) */
    int span_x = (ti->max_x > ti->min_x) ? (ti->max_x - ti->min_x) : 1;
    int span_y = (ti->max_y > ti->min_y) ? (ti->max_y - ti->min_y) : 1;
    int px = ((raw_x - ti->min_x) * (LOGICAL_H - 1)) / span_x;
    int py = ((raw_y - ti->min_y) * (LOGICAL_W - 1)) / span_y;
    px = clampi(px, 0, LOGICAL_H - 1);
    py = clampi(py, 0, LOGICAL_W - 1);

    /* Inverse of fb_flush rotation */
    if (ti->rotate_deg == 270) {
        out->x = (LOGICAL_W - 1) - py;
        out->y = px;
    } else {
        out->x = py;
        out->y = (LOGICAL_H - 1) - px;
    }
}

bool touch_poll_tap(TouchInput *ti, TouchTap *out_tap) {
#ifdef __linux__
    if (ti->fd < 0) return false;
    struct input_event ev;
    bool got_tap = false;

    while (read(ti->fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                ti->cur_x = ev.value;
                ti->saw_coord = true;
            } else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                ti->cur_y = ev.value;
                ti->saw_coord = true;
            } else if (ev.code == ABS_MT_TRACKING_ID) {
                if (ev.value >= 0) {
                    ti->touching = true;
                } else if (ti->touching && ti->saw_coord) {
                    map_to_landscape(ti, ti->cur_x, ti->cur_y, out_tap);
                    ti->touching = false;
                    ti->saw_coord = false;
                    got_tap = true;
                }
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            if (ev.value == 1) {
                ti->touching = true;
            } else if (ev.value == 0 && ti->touching && ti->saw_coord) {
                map_to_landscape(ti, ti->cur_x, ti->cur_y, out_tap);
                ti->touching = false;
                ti->saw_coord = false;
                got_tap = true;
            }
        }
    }
    return got_tap;
#else
    (void)ti;
    (void)out_tap;
    return false;
#endif
}
