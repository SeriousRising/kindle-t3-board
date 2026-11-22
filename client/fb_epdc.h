#ifndef T3_FB_EPDC_H
#define T3_FB_EPDC_H

#include <stdbool.h>
#include <stdint.h>
#include "font_atlas.h"

#define LOGICAL_W 800
#define LOGICAL_H 600

/* E-Ink palette (0 = black, 255 = paper white) */
#define GRAY_WHITE      255
#define GRAY_BG         246
#define GRAY_CARD_BG    232
#define GRAY_RULE_LIGHT 160
#define GRAY_RULE_MED   112
#define GRAY_TEXT_MUTED  84
#define GRAY_TEXT_SUB    56
#define GRAY_BLACK       16

typedef enum {
    EPDC_REFRESH_PARTIAL_DU = 0,   /* Fast 1-bit/low-latency partial update */
    EPDC_REFRESH_PARTIAL_GL16 = 1, /* Flashless 16-level grayscale partial */
    EPDC_REFRESH_FULL_GC16 = 2     /* Full 16-level flash to clear ghosting */
} EpdcRefreshMode;

typedef struct {
    uint8_t pixels[LOGICAL_W * LOGICAL_H];
    int dirty_x0, dirty_y0, dirty_x1, dirty_y1;
    bool has_dirty;

    int rotate_deg;   /* 270 (default: home on right) or 90 */
    bool invert;      /* Invert grayscale output to physical /dev/fb0 */

    /* Hardware fbdev fields (unused in host-only mode) */
    int fb_fd;
    uint8_t *fb_mem;
    uint32_t fb_size;
    int phys_w;
    int phys_h;
    int phys_stride;
    int phys_bpp;
    uint32_t update_marker;
    bool is_hardware;
} Framebuffer;

int  fb_init(Framebuffer *fb, bool open_hardware, int rotate_deg, bool invert);
void fb_close(Framebuffer *fb);

void fb_clear(Framebuffer *fb, uint8_t gray);
void fb_mark_dirty(Framebuffer *fb, int x, int y, int w, int h);
void fb_mark_all_dirty(Framebuffer *fb);

void fb_set_pixel(Framebuffer *fb, int x, int y, uint8_t gray);
void fb_fill_rect(Framebuffer *fb, int x, int y, int w, int h, uint8_t gray);
void fb_stroke_rect(Framebuffer *fb, int x, int y, int w, int h, int thickness, uint8_t gray);
void fb_fill_round_rect(Framebuffer *fb, int x, int y, int w, int h, int r, uint8_t gray);
void fb_stroke_round_rect(Framebuffer *fb, int x, int y, int w, int h, int r, int thickness, uint8_t gray);
void fb_hline(Framebuffer *fb, int x, int y, int w, int thickness, uint8_t gray);
void fb_fill_circle(Framebuffer *fb, int cx, int cy, int r, uint8_t gray);
void fb_draw_status_icon(Framebuffer *fb, int cx, int cy, int r, const char *state, uint8_t fg, uint8_t bg);

int  fb_measure_text(const BitmapFont *font, const char *text);
int  fb_draw_text(Framebuffer *fb, const BitmapFont *font, int x, int baseline_y,
                  const char *text, uint8_t fg_gray, int max_width);
int  fb_draw_text_right(Framebuffer *fb, const BitmapFont *font, int right_x, int baseline_y,
                        const char *text, uint8_t fg_gray, int max_width);
int  fb_draw_text_centered(Framebuffer *fb, const BitmapFont *font, int cx, int baseline_y,
                           const char *text, uint8_t fg_gray);

int  fb_flush(Framebuffer *fb, EpdcRefreshMode mode);
int  fb_save_pgm(const Framebuffer *fb, const char *path);

#endif /* T3_FB_EPDC_H */
