#include "fb_epdc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/fb.h>
#endif

/*
 * Freescale i.MX50 EPDC definitions for Amazon Kindle Touch (D01200 / yoshi, Linux 2.6.31).
 * Matches Kindle K5 mxcfb.h ABI.
 */
#define WAVEFORM_MODE_INIT  0x0
#define WAVEFORM_MODE_DU    0x1
#define WAVEFORM_MODE_GC16  0x2
#define WAVEFORM_MODE_GL16  0x3
#define WAVEFORM_MODE_A2    0x4
#define WAVEFORM_MODE_AUTO  0x101

#define UPDATE_MODE_PARTIAL 0x0
#define UPDATE_MODE_FULL    0x1
#define TEMP_USE_AMBIENT    0x1001

struct mxcfb_rect {
    uint32_t top;
    uint32_t left;
    uint32_t width;
    uint32_t height;
};

struct mxcfb_alt_buffer_data {
    uint32_t phys_addr;
    uint32_t width;
    uint32_t height;
    struct mxcfb_rect alt_update_region;
};

struct mxcfb_update_data {
    struct mxcfb_rect update_region;
    uint32_t waveform_mode;
    uint32_t update_mode;
    uint32_t update_marker;
    int temp;
    uint32_t flags;
    struct mxcfb_alt_buffer_data alt_buffer_data;
};

#define MXCFB_SEND_UPDATE             _IOW('F', 0x2E, struct mxcfb_update_data)
#define MXCFB_WAIT_FOR_UPDATE_COMPLETE _IOW('F', 0x2F, uint32_t)

static inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int fb_init(Framebuffer *fb, bool open_hardware, int rotate_deg, bool invert) {
    memset(fb, 0, sizeof(*fb));
    fb->fb_fd = -1;
    fb->rotate_deg = (rotate_deg == 90) ? 90 : 270;
    fb->invert = invert;
    fb->update_marker = 1;
    fb_clear(fb, GRAY_BG);

    if (!open_hardware) {
        fb->is_hardware = false;
        return 0;
    }

#ifdef __linux__
    fb->fb_fd = open("/dev/fb0", O_RDWR);
    if (fb->fb_fd < 0) {
        fprintf(stderr, "[fb] open(/dev/fb0) failed: %s\n", strerror(errno));
        return -1;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fb->fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(fb->fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        fprintf(stderr, "[fb] FBIOGET_*SCREENINFO failed: %s\n", strerror(errno));
        close(fb->fb_fd);
        fb->fb_fd = -1;
        return -1;
    }

    fb->phys_w = (int)vinfo.xres;
    fb->phys_h = (int)vinfo.yres;
    fb->phys_stride = (int)finfo.line_length;
    fb->phys_bpp = (int)vinfo.bits_per_pixel;
    fb->fb_size = finfo.smem_len;

    fb->fb_mem = (uint8_t *)mmap(NULL, fb->fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fb_fd, 0);
    if (fb->fb_mem == MAP_FAILED) {
        fprintf(stderr, "[fb] mmap(/dev/fb0) failed: %s\n", strerror(errno));
        fb->fb_mem = NULL;
        close(fb->fb_fd);
        fb->fb_fd = -1;
        return -1;
    }

    fb->is_hardware = true;
    fprintf(stderr, "[fb] /dev/fb0 ready: %dx%d @ %dbpp (stride=%d, rotate=%d)\n",
            fb->phys_w, fb->phys_h, fb->phys_bpp, fb->phys_stride, fb->rotate_deg);
    return 0;
#else
    fb->is_hardware = false;
    return 0;
#endif
}

void fb_close(Framebuffer *fb) {
    if (fb->fb_mem && fb->fb_size > 0) {
        munmap(fb->fb_mem, fb->fb_size);
        fb->fb_mem = NULL;
    }
    if (fb->fb_fd >= 0) {
        close(fb->fb_fd);
        fb->fb_fd = -1;
    }
    fb->is_hardware = false;
}

void fb_mark_dirty(Framebuffer *fb, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    int x0 = clampi(x, 0, LOGICAL_W);
    int y0 = clampi(y, 0, LOGICAL_H);
    int x1 = clampi(x + w, 0, LOGICAL_W);
    int y1 = clampi(y + h, 0, LOGICAL_H);
    if (x1 <= x0 || y1 <= y0) return;

    if (!fb->has_dirty) {
        fb->dirty_x0 = x0;
        fb->dirty_y0 = y0;
        fb->dirty_x1 = x1;
        fb->dirty_y1 = y1;
        fb->has_dirty = true;
    } else {
        if (x0 < fb->dirty_x0) fb->dirty_x0 = x0;
        if (y0 < fb->dirty_y0) fb->dirty_y0 = y0;
        if (x1 > fb->dirty_x1) fb->dirty_x1 = x1;
        if (y1 > fb->dirty_y1) fb->dirty_y1 = y1;
    }
}

void fb_mark_all_dirty(Framebuffer *fb) {
    fb->dirty_x0 = 0;
    fb->dirty_y0 = 0;
    fb->dirty_x1 = LOGICAL_W;
    fb->dirty_y1 = LOGICAL_H;
    fb->has_dirty = true;
}

void fb_clear(Framebuffer *fb, uint8_t gray) {
    memset(fb->pixels, gray, sizeof(fb->pixels));
    fb_mark_all_dirty(fb);
}

void fb_set_pixel(Framebuffer *fb, int x, int y, uint8_t gray) {
    if ((unsigned)x >= LOGICAL_W || (unsigned)y >= LOGICAL_H) return;
    fb->pixels[y * LOGICAL_W + x] = gray;
}

static inline void fb_blend_pixel(Framebuffer *fb, int x, int y, uint8_t fg, uint8_t alpha) {
    if ((unsigned)x >= LOGICAL_W || (unsigned)y >= LOGICAL_H || alpha == 0) return;
    uint8_t *dst = &fb->pixels[y * LOGICAL_W + x];
    if (alpha == 255) {
        *dst = fg;
        return;
    }
    int bg = *dst;
    *dst = (uint8_t)((fg * alpha + bg * (255 - alpha) + 127) / 255);
}

void fb_fill_rect(Framebuffer *fb, int x, int y, int w, int h, uint8_t gray) {
    int x0 = clampi(x, 0, LOGICAL_W);
    int y0 = clampi(y, 0, LOGICAL_H);
    int x1 = clampi(x + w, 0, LOGICAL_W);
    int y1 = clampi(y + h, 0, LOGICAL_H);
    if (x1 <= x0 || y1 <= y0) return;

    for (int py = y0; py < y1; py++) {
        memset(&fb->pixels[py * LOGICAL_W + x0], gray, (size_t)(x1 - x0));
    }
    fb_mark_dirty(fb, x0, y0, x1 - x0, y1 - y0);
}

void fb_hline(Framebuffer *fb, int x, int y, int w, int thickness, uint8_t gray) {
    fb_fill_rect(fb, x, y, w, thickness, gray);
}

void fb_stroke_rect(Framebuffer *fb, int x, int y, int w, int h, int t, uint8_t gray) {
    if (t <= 0 || w <= 0 || h <= 0) return;
    fb_fill_rect(fb, x, y, w, t, gray);
    fb_fill_rect(fb, x, y + h - t, w, t, gray);
    fb_fill_rect(fb, x, y, t, h, gray);
    fb_fill_rect(fb, x + w - t, y, t, h, gray);
}

static bool inside_round_rect(int px, int py, int x, int y, int w, int h, int r) {
    if (px < x || py < y || px >= x + w || py >= y + h) return false;
    int rx = 0, ry = 0;
    if (px < x + r) rx = (x + r) - px;
    else if (px >= x + w - r) rx = px - (x + w - r - 1);
    if (py < y + r) ry = (y + r) - py;
    else if (py >= y + h - r) ry = py - (y + h - r - 1);
    if (rx == 0 || ry == 0) return true;
    return (rx * rx + ry * ry) <= r * r;
}

void fb_fill_round_rect(Framebuffer *fb, int x, int y, int w, int h, int r, uint8_t gray) {
    if (r <= 0) {
        fb_fill_rect(fb, x, y, w, h, gray);
        return;
    }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    int x0 = clampi(x, 0, LOGICAL_W);
    int y0 = clampi(y, 0, LOGICAL_H);
    int x1 = clampi(x + w, 0, LOGICAL_W);
    int y1 = clampi(y + h, 0, LOGICAL_H);
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            if (inside_round_rect(px, py, x, y, w, h, r)) {
                fb->pixels[py * LOGICAL_W + px] = gray;
            }
        }
    }
    fb_mark_dirty(fb, x0, y0, x1 - x0, y1 - y0);
}

void fb_stroke_round_rect(Framebuffer *fb, int x, int y, int w, int h, int r, int t, uint8_t gray) {
    if (t <= 0 || w <= 0 || h <= 0) return;
    int ir = (r > t) ? (r - t) : 0;
    int x0 = clampi(x, 0, LOGICAL_W);
    int y0 = clampi(y, 0, LOGICAL_H);
    int x1 = clampi(x + w, 0, LOGICAL_W);
    int y1 = clampi(y + h, 0, LOGICAL_H);
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            if (inside_round_rect(px, py, x, y, w, h, r) &&
                !inside_round_rect(px, py, x + t, y + t, w - 2 * t, h - 2 * t, ir)) {
                fb->pixels[py * LOGICAL_W + px] = gray;
            }
        }
    }
    fb_mark_dirty(fb, x0, y0, x1 - x0, y1 - y0);
}

void fb_fill_circle(Framebuffer *fb, int cx, int cy, int r, uint8_t gray) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                fb_set_pixel(fb, cx + dx, cy + dy, gray);
            }
        }
    }
    fb_mark_dirty(fb, cx - r, cy - r, 2 * r + 1, 2 * r + 1);
}

void fb_draw_status_icon(Framebuffer *fb, int cx, int cy, int r, const char *state, uint8_t fg, uint8_t bg) {
    if (strcmp(state, "running") == 0) {
        fb_fill_circle(fb, cx, cy, r, fg);
    } else if (strcmp(state, "waiting") == 0) {
        /* Outer ring + filled left semicircle */
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                int d2 = dx * dx + dy * dy;
                if (d2 <= r * r) {
                    if (dx <= 0 || d2 >= (r - 2) * (r - 2)) {
                        fb_set_pixel(fb, cx + dx, cy + dy, fg);
                    } else {
                        fb_set_pixel(fb, cx + dx, cy + dy, bg);
                    }
                }
            }
        }
        fb_mark_dirty(fb, cx - r, cy - r, 2 * r + 1, 2 * r + 1);
    } else {
        /* Crisp checkmark for done */
        for (int i = 0; i <= r; i++) {
            int x1 = cx - r + i / 2;
            int y1 = cy + i / 2;
            fb_fill_rect(fb, x1, y1, 2, 2, fg);
        }
        for (int i = 0; i <= r * 2; i++) {
            int x2 = cx - r / 2 + i / 2;
            int y2 = cy + r / 2 - i / 2;
            fb_fill_rect(fb, x2, y2, 2, 2, fg);
        }
        fb_mark_dirty(fb, cx - r - 1, cy - r - 1, 2 * r + 3, 2 * r + 3);
    }
}

static const GlyphInfo *lookup_glyph(const BitmapFont *font, unsigned char ch) {
    if (ch < FONT_FIRST_CHAR || ch > FONT_LAST_CHAR) ch = '?';
    return &font->glyphs[ch - FONT_FIRST_CHAR];
}

int fb_measure_text(const BitmapFont *font, const char *text) {
    if (!text) return 0;
    int width = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        width += lookup_glyph(font, *p)->advance;
    }
    return width;
}

static void draw_glyph(Framebuffer *fb, const BitmapFont *font, const GlyphInfo *g,
                       int pen_x, int baseline_y, uint8_t fg_gray) {
    if (g->w == 0 || g->h == 0) return;
    int gx = pen_x + g->xoff;
    int gy = baseline_y + g->yoff;
    const uint8_t *src = &font->bitmap[g->offset];
    for (int row = 0; row < g->h; row++) {
        for (int col = 0; col < g->w; col++) {
            uint8_t a = src[row * g->w + col];
            if (a > 8) {
                fb_blend_pixel(fb, gx + col, gy + row, fg_gray, a);
            }
        }
    }
    fb_mark_dirty(fb, gx, gy, g->w, g->h);
}

int fb_draw_text(Framebuffer *fb, const BitmapFont *font, int x, int baseline_y,
                 const char *text, uint8_t fg_gray, int max_width) {
    if (!text) return 0;
    int full_w = fb_measure_text(font, text);
    bool need_ellipsis = (max_width > 0 && full_w > max_width);
    int ell_w = need_ellipsis ? fb_measure_text(font, "...") : 0;
    int limit = need_ellipsis ? (max_width - ell_w) : max_width;

    int pen_x = x;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        const GlyphInfo *g = lookup_glyph(font, *p);
        if (max_width > 0 && (pen_x - x + g->advance) > limit) {
            break;
        }
        draw_glyph(fb, font, g, pen_x, baseline_y, fg_gray);
        pen_x += g->advance;
    }
    if (need_ellipsis && limit > 0) {
        for (int i = 0; i < 3; i++) {
            const GlyphInfo *g = lookup_glyph(font, '.');
            draw_glyph(fb, font, g, pen_x, baseline_y, fg_gray);
            pen_x += g->advance;
        }
    }
    return pen_x - x;
}

int fb_draw_text_right(Framebuffer *fb, const BitmapFont *font, int right_x, int baseline_y,
                       const char *text, uint8_t fg_gray, int max_width) {
    int w = fb_measure_text(font, text);
    if (max_width > 0 && w > max_width) w = max_width;
    return fb_draw_text(fb, font, right_x - w, baseline_y, text, fg_gray, max_width);
}

int fb_draw_text_centered(Framebuffer *fb, const BitmapFont *font, int cx, int baseline_y,
                          const char *text, uint8_t fg_gray) {
    int w = fb_measure_text(font, text);
    return fb_draw_text(fb, font, cx - w / 2, baseline_y, text, fg_gray, 0);
}

int fb_flush(Framebuffer *fb, EpdcRefreshMode mode) {
    if (!fb->has_dirty) return 0;

    int lx0 = fb->dirty_x0;
    int ly0 = fb->dirty_y0;
    int lx1 = fb->dirty_x1;
    int ly1 = fb->dirty_y1;
    fb->has_dirty = false;

    if (!fb->is_hardware || !fb->fb_mem) {
        return 0;
    }

    /* Blit logical dirty rect into physical portrait /dev/fb0 with 90/270 deg rotation */
    int px_min = fb->phys_w, py_min = fb->phys_h, px_max = 0, py_max = 0;

    for (int ly = ly0; ly < ly1; ly++) {
        for (int lx = lx0; lx < lx1; lx++) {
            uint8_t val = fb->pixels[ly * LOGICAL_W + lx];
            if (fb->invert) val = (uint8_t)(255 - val);

            int px, py;
            if (fb->rotate_deg == 270) {
                /* Home button on right edge (natural landscape) */
                px = ly;
                py = (LOGICAL_W - 1) - lx;
            } else {
                /* Home button on left edge */
                px = (LOGICAL_H - 1) - ly;
                py = lx;
            }

            if ((unsigned)px >= (unsigned)fb->phys_w || (unsigned)py >= (unsigned)fb->phys_h) {
                continue;
            }

            if (fb->phys_bpp == 8) {
                fb->fb_mem[py * fb->phys_stride + px] = val;
            } else if (fb->phys_bpp == 4) {
                uint8_t nib = (uint8_t)(val >> 4);
                uint8_t *byte = &fb->fb_mem[py * fb->phys_stride + (px >> 1)];
                if (px & 1) {
                    *byte = (uint8_t)((*byte & 0xF0) | nib);
                } else {
                    *byte = (uint8_t)((*byte & 0x0F) | (nib << 4));
                }
            }

            if (px < px_min) px_min = px;
            if (py < py_min) py_min = py;
            if (px + 1 > px_max) px_max = px + 1;
            if (py + 1 > py_max) py_max = py + 1;
        }
    }

    if (px_max <= px_min || py_max <= py_min) return 0;

    struct mxcfb_update_data upd;
    memset(&upd, 0, sizeof(upd));
    if (mode == EPDC_REFRESH_FULL_GC16) {
        upd.update_region.left = 0;
        upd.update_region.top = 0;
        upd.update_region.width = (uint32_t)fb->phys_w;
        upd.update_region.height = (uint32_t)fb->phys_h;
        upd.waveform_mode = WAVEFORM_MODE_GC16;
        upd.update_mode = UPDATE_MODE_FULL;
    } else {
        upd.update_region.left = (uint32_t)px_min;
        upd.update_region.top = (uint32_t)py_min;
        upd.update_region.width = (uint32_t)(px_max - px_min);
        upd.update_region.height = (uint32_t)(py_max - py_min);
        upd.waveform_mode = (mode == EPDC_REFRESH_PARTIAL_DU)
            ? WAVEFORM_MODE_DU
            : WAVEFORM_MODE_GL16;
        upd.update_mode = UPDATE_MODE_PARTIAL;
    }
    upd.update_marker = fb->update_marker++;
    upd.temp = TEMP_USE_AMBIENT;
    upd.flags = 0;

    if (ioctl(fb->fb_fd, MXCFB_SEND_UPDATE, &upd) < 0) {
        fprintf(stderr, "[epdc] MXCFB_SEND_UPDATE failed: %s\n", strerror(errno));
        return -1;
    }
    if (mode == EPDC_REFRESH_FULL_GC16) {
        uint32_t marker = upd.update_marker;
        (void)ioctl(fb->fb_fd, MXCFB_WAIT_FOR_UPDATE_COMPLETE, &marker);
    }
    return 0;
}

int fb_save_pgm(const Framebuffer *fb, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "[fb] fopen(%s) failed: %s\n", path, strerror(errno));
        return -1;
    }
    fprintf(fp, "P5\n%d %d\n255\n", LOGICAL_W, LOGICAL_H);
    size_t written = fwrite(fb->pixels, 1, sizeof(fb->pixels), fp);
    fclose(fp);
    return (written == sizeof(fb->pixels)) ? 0 : -1;
}
