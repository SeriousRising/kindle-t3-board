#ifndef T3_UI_H
#define T3_UI_H

#include <stdbool.h>
#include "fb_epdc.h"
#include "http_client.h"

#define MAX_HIT_REGIONS 24

typedef enum {
    VIEW_OVERVIEW = 0,
    VIEW_DETAIL   = 1
} UiView;

typedef enum {
    DETAIL_TAB_SUBAGENTS = 0,
    DETAIL_TAB_PRS       = 1
} UiDetailTab;

typedef enum {
    HIT_NONE = 0,
    HIT_OPEN_PROJECT,
    HIT_BACK_OVERVIEW,
    HIT_TAB_SUBAGENTS,
    HIT_TAB_PRS,
    HIT_CLEAR_REFRESH
} HitAction;

typedef struct {
    int x, y, w, h;
    HitAction action;
    char code[8];
} HitRegion;

typedef struct {
    UiView view;
    UiDetailTab detail_tab;
    char selected_code[8];
    char battery_str[16];
    int  refresh_interval_sec;
    int  partial_count;

    HitRegion hits[MAX_HIT_REGIONS];
    int hit_count;
} UiState;

void ui_init(UiState *ui, int refresh_interval_sec);
void ui_render(UiState *ui, Framebuffer *fb, const BoardState *board);

/* Returns true if a redraw is needed; sets *out_mode to the best EPDC refresh mode. */
bool ui_handle_tap(UiState *ui, const BoardState *board, int x, int y, EpdcRefreshMode *out_mode);

#endif /* T3_UI_H */
