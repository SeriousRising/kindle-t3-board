#ifndef T3_INPUT_TOUCH_H
#define T3_INPUT_TOUCH_H

#include <stdbool.h>

typedef struct {
    int fd;
    int rotate_deg;
    int min_x, max_x;
    int min_y, max_y;
    int cur_x, cur_y;
    bool touching;
    bool saw_coord;
    bool use_mt;
} TouchInput;

typedef struct {
    int x; /* 0..799 logical landscape */
    int y; /* 0..599 logical landscape */
} TouchTap;

int  touch_init(TouchInput *ti, const char *device_override, int rotate_deg);
void touch_close(TouchInput *ti);
bool touch_poll_tap(TouchInput *ti, TouchTap *out_tap);

#endif /* T3_INPUT_TOUCH_H */
