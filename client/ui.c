#include "ui.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define PAD_X 26
#define INNER_W (LOGICAL_W - 2 * PAD_X)

static void ui_add_hit(UiState *ui, int x, int y, int w, int h, HitAction action, const char *code) {
    if (ui->hit_count >= MAX_HIT_REGIONS) return;
    HitRegion *hr = &ui->hits[ui->hit_count++];
    hr->x = x;
    hr->y = y;
    hr->w = w;
    hr->h = h;
    hr->action = action;
    if (code) {
        snprintf(hr->code, sizeof(hr->code), "%s", code);
    } else {
        hr->code[0] = '\0';
    }
}

static void str_upper_copy(char *dst, size_t dst_sz, const char *src) {
    size_t i = 0;
    for (; src && src[i] && i + 1 < dst_sz; i++) {
        dst[i] = (char)toupper((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static const BoardProject *find_selected_project(const UiState *ui, const BoardState *board) {
    if (ui->selected_code[0] == '\0') return NULL;
    for (int i = 0; i < board->project_count; i++) {
        if (strcmp(board->projects[i].code, ui->selected_code) == 0) {
            return &board->projects[i];
        }
    }
    return NULL;
}

void ui_init(UiState *ui, int refresh_interval_sec) {
    memset(ui, 0, sizeof(*ui));
    ui->view = VIEW_OVERVIEW;
    ui->detail_tab = DETAIL_TAB_SUBAGENTS;
    ui->refresh_interval_sec = refresh_interval_sec > 0 ? refresh_interval_sec : 60;
    snprintf(ui->battery_str, sizeof(ui->battery_str), "BAT --");
}

static void render_header_right(const UiState *ui, Framebuffer *fb, const BoardState *board) {
    char right_buf[64];
    snprintf(right_buf, sizeof(right_buf), "%s   %s", board->clock, ui->battery_str);
    fb_draw_text_right(fb, &FONT_MONO_13_BOLD, LOGICAL_W - PAD_X, 38, right_buf, GRAY_TEXT_SUB, 360);
}

static void render_clear_button(UiState *ui, Framebuffer *fb, int right_x, int y) {
    const char *label = "CLEAR";
    int bw = 68;
    int bh = 26;
    int bx = right_x - bw;
    fb_stroke_round_rect(fb, bx, y, bw, bh, 4, 2, GRAY_TEXT_SUB);
    fb_draw_text_centered(fb, &FONT_MONO_12, bx + bw / 2, y + 17, label, GRAY_BLACK);
    ui_add_hit(ui, bx - 6, y - 6, bw + 12, bh + 12, HIT_CLEAR_REFRESH, NULL);
}

static void render_overview(UiState *ui, Framebuffer *fb, const BoardState *board) {
    /* Top Header */
    fb_draw_text(fb, &FONT_MONO_14_BOLD, PAD_X, 38, "T3 CODE BOARD", GRAY_BLACK, 200);
    fb_draw_text(fb, &FONT_MONO_12, PAD_X + 132, 38, "- PROJECTS ONLY", GRAY_TEXT_MUTED, 180);
    render_header_right(ui, fb, board);
    fb_hline(fb, PAD_X, 50, INNER_W, 3, GRAY_BLACK);

    /* Hero Summary Strip */
    char count_buf[16];
    snprintf(count_buf, sizeof(count_buf), "%d", board->project_count);
    int num_w = fb_draw_text(fb, &FONT_SANS_68_BOLD, PAD_X, 130, count_buf, GRAY_BLACK, 120);

    int copy_x = PAD_X + num_w + 18;
    const char *noun = (board->project_count == 1) ? "active project thread" : "active project threads";
    fb_draw_text(fb, &FONT_SANS_22_BOLD, copy_x, 108, noun, GRAY_BLACK, 340);

    char sub_buf[96];
    snprintf(sub_buf, sizeof(sub_buf), "%d live subagent%s  |  no scratch threads",
             board->live_subagents, board->live_subagents == 1 ? "" : "s");
    fb_draw_text(fb, &FONT_MONO_12, copy_x, 130, sub_buf, GRAY_TEXT_MUTED, 360);

    char stat1[64], stat2[64];
    snprintf(stat1, sizeof(stat1), "RUNNING %d  |  WAITING %d",
             board->running_count, board->waiting_count);
    snprintf(stat2, sizeof(stat2), "OPEN PRS %d  |  PROJECTS %d",
             board->open_prs, board->project_count);
    fb_draw_text_right(fb, &FONT_MONO_13_BOLD, LOGICAL_W - PAD_X, 90, stat1, GRAY_BLACK, 260);
    fb_draw_text_right(fb, &FONT_MONO_13_BOLD, LOGICAL_W - PAD_X, 111, stat2, GRAY_TEXT_SUB, 260);
    fb_draw_text_right(fb, &FONT_MONO_12, LOGICAL_W - PAD_X, 132, "TAP PROJECT TO INSPECT", GRAY_TEXT_MUTED, 260);

    fb_hline(fb, PAD_X, 148, INNER_W, 2, GRAY_BLACK);

    /* Error banner or empty state */
    if (board->has_error) {
        fb_fill_round_rect(fb, PAD_X, 180, INNER_W, 68, 6, GRAY_CARD_BG);
        fb_stroke_round_rect(fb, PAD_X, 180, INNER_W, 68, 6, 2, GRAY_BLACK);
        fb_draw_text(fb, &FONT_SANS_15_BOLD, PAD_X + 16, 208, "Bridge connection error", GRAY_BLACK, INNER_W - 32);
        fb_draw_text(fb, &FONT_MONO_12, PAD_X + 16, 232, board->error_msg, GRAY_TEXT_SUB, INNER_W - 32);
    } else if (board->project_count == 0) {
        fb_draw_text_centered(fb, &FONT_SANS_22_BOLD, LOGICAL_W / 2, 315, "All project threads are idle", GRAY_TEXT_SUB);
        fb_draw_text_centered(fb, &FONT_MONO_12, LOGICAL_W / 2, 344,
                              "Active threads with a T3 Code project will appear here automatically.",
                              GRAY_TEXT_MUTED);
    } else {
        int max_rows = board->project_count;
        if (max_rows > 4) max_rows = 4;
        int row_h = 92;
        int y = 152;

        for (int i = 0; i < max_rows; i++) {
            const BoardProject *p = &board->projects[i];

            /* Inverted Project Badge (e.g. S8, R6) */
            int bx = PAD_X + 2;
            int by = y + (row_h - 48) / 2;
            fb_fill_round_rect(fb, bx, by, 56, 48, 6, GRAY_BLACK);
            fb_draw_text_centered(fb, &FONT_SANS_22_BOLD, bx + 28, by + 32, p->code, GRAY_WHITE);

            /* Project Name & Lead Agent */
            int col1_x = bx + 70;
            fb_draw_text(fb, &FONT_SANS_15_BOLD, col1_x, y + 38, p->name, GRAY_BLACK, 182);
            char lead_buf[56];
            snprintf(lead_buf, sizeof(lead_buf), "lead: %s", p->lead_agent);
            fb_draw_text(fb, &FONT_MONO_12, col1_x, y + 60, lead_buf, GRAY_TEXT_SUB, 182);

            /* PR / Active Work Column */
            int col2_x = col1_x + 194;
            int col2_w = 344;
            int work_x = col2_x;
            if (p->pr_badge[0] != '\0') {
                int tag_w = fb_measure_text(&FONT_MONO_12, p->pr_badge) + 12;
                fb_stroke_round_rect(fb, work_x, y + 21, tag_w, 22, 3, 2, GRAY_BLACK);
                fb_draw_text_centered(fb, &FONT_MONO_12, work_x + tag_w / 2, y + 36, p->pr_badge, GRAY_BLACK);
                work_x += tag_w + 8;
            }
            fb_draw_text(fb, &FONT_MONO_14_BOLD, work_x, y + 37, p->active_work, GRAY_BLACK,
                         col2_w - (work_x - col2_x));

            char meta_buf[140];
            snprintf(meta_buf, sizeof(meta_buf), "%s  |  %s", p->lead_thread, p->pr_summary);
            fb_draw_text(fb, &FONT_MONO_12, col2_x, y + 60, meta_buf, GRAY_TEXT_MUTED, col2_w);

            /* Right Status + Drill-Down Cue */
            char st_upper[16];
            str_upper_copy(st_upper, sizeof(st_upper), p->status);
            int st_w = fb_measure_text(&FONT_MONO_13_BOLD, st_upper);
            int right_x = LOGICAL_W - PAD_X - 2;
            fb_draw_status_icon(fb, right_x - st_w - 12, y + 33, 5, p->status, GRAY_BLACK, GRAY_BG);
            fb_draw_text_right(fb, &FONT_MONO_13_BOLD, right_x, y + 38, st_upper, GRAY_BLACK, 110);

            char sub_cnt[32];
            snprintf(sub_cnt, sizeof(sub_cnt), "%d SUBAGENT%s >",
                     p->live_subagents, p->live_subagents == 1 ? "" : "S");
            fb_draw_text_right(fb, &FONT_MONO_12, right_x, y + 60, sub_cnt, GRAY_TEXT_SUB, 126);

            /* Register full-row IR touch hitbox */
            ui_add_hit(ui, PAD_X, y, INNER_W, row_h, HIT_OPEN_PROJECT, p->code);

            int rule_t = (i == max_rows - 1) ? 2 : 1;
            uint8_t rule_c = (i == max_rows - 1) ? GRAY_BLACK : GRAY_RULE_MED;
            fb_hline(fb, PAD_X, y + row_h - rule_t, INNER_W, rule_t, rule_c);
            y += row_h;
        }
    }

    /* Footer */
    fb_hline(fb, PAD_X, 542, INNER_W, 2, GRAY_BLACK);
    char foot_l[112];
    snprintf(foot_l, sizeof(foot_l), "LAST: %s", board->last_summary);
    fb_draw_text(fb, &FONT_MONO_12, PAD_X, 571, foot_l, GRAY_TEXT_SUB, 500);

    char foot_r[48];
    snprintf(foot_r, sizeof(foot_r), "PARTIAL %dS", ui->refresh_interval_sec);
    fb_draw_text_right(fb, &FONT_MONO_12, LOGICAL_W - PAD_X - 78, 571, foot_r, GRAY_TEXT_MUTED, 140);
    render_clear_button(ui, fb, LOGICAL_W - PAD_X, 553);
}

static void render_detail(UiState *ui, Framebuffer *fb, const BoardState *board, const BoardProject *p) {
    /* Top Header with generous <- ALL touch button */
    int back_x = PAD_X;
    int back_y = 14;
    int back_w = 88;
    int back_h = 30;
    fb_fill_round_rect(fb, back_x, back_y, back_w, back_h, 4, GRAY_BLACK);
    fb_draw_text_centered(fb, &FONT_MONO_13_BOLD, back_x + back_w / 2, back_y + 20, "<- ALL", GRAY_WHITE);
    ui_add_hit(ui, back_x - 8, back_y - 8, back_w + 16, back_h + 16, HIT_BACK_OVERVIEW, NULL);

    fb_draw_text(fb, &FONT_MONO_14_BOLD, back_x + back_w + 14, 34, "PROJECT INSPECTOR", GRAY_BLACK, 220);
    render_header_right(ui, fb, board);
    fb_hline(fb, PAD_X, 52, INNER_W, 3, GRAY_BLACK);

    /* Project Banner + Tabs */
    int ban_y = 64;
    fb_fill_round_rect(fb, PAD_X, ban_y, 56, 48, 6, GRAY_BLACK);
    fb_draw_text_centered(fb, &FONT_SANS_22_BOLD, PAD_X + 28, ban_y + 32, p->code, GRAY_WHITE);

    fb_draw_text(fb, &FONT_SANS_24_BOLD, PAD_X + 68, ban_y + 26, p->name, GRAY_BLACK, 370);
    char repo_line[160];
    snprintf(repo_line, sizeof(repo_line), "%s  |  %s", p->repo, p->pr_summary);
    fb_draw_text(fb, &FONT_MONO_12, PAD_X + 68, ban_y + 45, repo_line, GRAY_TEXT_MUTED, 370);

    /* Touch Tabs: SUBAGENTS / PRS */
    char tab1_lbl[32], tab2_lbl[32];
    snprintf(tab1_lbl, sizeof(tab1_lbl), "SUBAGENTS (%d)", p->live_subagents);
    snprintf(tab2_lbl, sizeof(tab2_lbl), "PRS (%d)", p->pr_count);

    int t2_w = 96;
    int t1_w = 136;
    int tab_h = 36;
    int tab_y = ban_y + 6;
    int t2_x = LOGICAL_W - PAD_X - t2_w;
    int t1_x = t2_x - 10 - t1_w;

    bool sub_act = (ui->detail_tab == DETAIL_TAB_SUBAGENTS);
    if (sub_act) {
        fb_fill_round_rect(fb, t1_x, tab_y, t1_w, tab_h, 4, GRAY_BLACK);
        fb_draw_text_centered(fb, &FONT_MONO_12, t1_x + t1_w / 2, tab_y + 22, tab1_lbl, GRAY_WHITE);
    } else {
        fb_stroke_round_rect(fb, t1_x, tab_y, t1_w, tab_h, 4, 2, GRAY_BLACK);
        fb_draw_text_centered(fb, &FONT_MONO_12, t1_x + t1_w / 2, tab_y + 22, tab1_lbl, GRAY_BLACK);
    }
    ui_add_hit(ui, t1_x - 4, tab_y - 6, t1_w + 8, tab_h + 12, HIT_TAB_SUBAGENTS, p->code);

    if (!sub_act) {
        fb_fill_round_rect(fb, t2_x, tab_y, t2_w, tab_h, 4, GRAY_BLACK);
        fb_draw_text_centered(fb, &FONT_MONO_12, t2_x + t2_w / 2, tab_y + 22, tab2_lbl, GRAY_WHITE);
    } else {
        fb_stroke_round_rect(fb, t2_x, tab_y, t2_w, tab_h, 4, 2, GRAY_BLACK);
        fb_draw_text_centered(fb, &FONT_MONO_12, t2_x + t2_w / 2, tab_y + 22, tab2_lbl, GRAY_BLACK);
    }
    ui_add_hit(ui, t2_x - 4, tab_y - 6, t2_w + 8, tab_h + 12, HIT_TAB_PRS, p->code);

    fb_hline(fb, PAD_X, 124, INNER_W, 2, GRAY_BLACK);

    /* Lead Thread Summary Box */
    int card_y = 136;
    int card_h = 58;
    fb_fill_round_rect(fb, PAD_X, card_y, INNER_W, card_h, 4, GRAY_CARD_BG);
    fb_stroke_round_rect(fb, PAD_X, card_y, INNER_W, card_h, 4, 2, GRAY_BLACK);

    char st_upper[16], lead_hdr[48];
    str_upper_copy(st_upper, sizeof(st_upper), p->status);
    snprintf(lead_hdr, sizeof(lead_hdr), "LEAD THREAD  |  %s", st_upper);
    fb_draw_text(fb, &FONT_MONO_12, PAD_X + 14, card_y + 20, lead_hdr, GRAY_TEXT_MUTED, 480);

    int lt_x = PAD_X + 14;
    if (p->pr_badge[0] != '\0') {
        int tag_w = fb_measure_text(&FONT_MONO_12, p->pr_badge) + 10;
        fb_stroke_round_rect(fb, lt_x, card_y + 29, tag_w, 20, 3, 2, GRAY_BLACK);
        fb_draw_text_centered(fb, &FONT_MONO_12, lt_x + tag_w / 2, card_y + 43, p->pr_badge, GRAY_BLACK);
        lt_x += tag_w + 8;
    }
    fb_draw_text(fb, &FONT_SANS_15_BOLD, lt_x, card_y + 45, p->lead_thread, GRAY_BLACK,
                 520 - (lt_x - (PAD_X + 14)));

    fb_draw_text_right(fb, &FONT_MONO_12, LOGICAL_W - PAD_X - 14, card_y + 20, "LEAD AGENT", GRAY_TEXT_MUTED, 180);
    fb_draw_text_right(fb, &FONT_MONO_14_BOLD, LOGICAL_W - PAD_X - 14, card_y + 44, p->lead_agent, GRAY_BLACK, 200);

    /* Table Section Header */
    int sec_y = 222;
    if (sub_act) {
        char sec_l[64];
        snprintf(sec_l, sizeof(sec_l), "SUBAGENTS & TASKS (%d LIVE / %d SHOWN)",
                 p->live_subagents, p->subagent_count);
        fb_draw_text(fb, &FONT_MONO_12, PAD_X + 2, sec_y, sec_l, GRAY_TEXT_MUTED, 460);
        fb_draw_text_right(fb, &FONT_MONO_12, LOGICAL_W - PAD_X - 2, sec_y, "AGENT / STATE", GRAY_TEXT_MUTED, 200);
        fb_hline(fb, PAD_X, sec_y + 8, INNER_W, 2, GRAY_TEXT_SUB);

        if (p->subagent_count == 0) {
            fb_draw_text_centered(fb, &FONT_MONO_14_BOLD, LOGICAL_W / 2, 360,
                                  "No delegated subagents on this project", GRAY_TEXT_MUTED);
        } else {
            int ry = sec_y + 12;
            int rh = 48;
            int cnt = p->subagent_count > 6 ? 6 : p->subagent_count;
            for (int i = 0; i < cnt; i++) {
                const BoardSubagent *s = &p->subagents[i];
                fb_draw_status_icon(fb, PAD_X + 12, ry + rh / 2, 5, s->state, GRAY_BLACK, GRAY_BG);
                fb_draw_text(fb, &FONT_MONO_13_BOLD, PAD_X + 30, ry + 29, s->task, GRAY_BLACK, 430);
                fb_draw_text(fb, &FONT_MONO_12, PAD_X + 472, ry + 29, s->agent, GRAY_TEXT_SUB, 160);
                fb_draw_text_right(fb, &FONT_MONO_12, LOGICAL_W - PAD_X - 2, ry + 29, s->meta, GRAY_BLACK, 110);
                if (i < cnt - 1) {
                    fb_hline(fb, PAD_X, ry + rh - 1, INNER_W, 1, GRAY_RULE_LIGHT);
                }
                ry += rh;
            }
        }
    } else {
        char sec_l[64];
        snprintf(sec_l, sizeof(sec_l), "LINKED PULL REQUESTS (%d SHOWN / %d TOTAL)",
                 p->pr_count, p->total_prs);
        fb_draw_text(fb, &FONT_MONO_12, PAD_X + 2, sec_y, sec_l, GRAY_TEXT_MUTED, 460);
        fb_draw_text_right(fb, &FONT_MONO_12, LOGICAL_W - PAD_X - 2, sec_y, "BRANCH / STATE", GRAY_TEXT_MUTED, 200);
        fb_hline(fb, PAD_X, sec_y + 8, INNER_W, 2, GRAY_TEXT_SUB);

        if (p->pr_count == 0) {
            fb_draw_text_centered(fb, &FONT_MONO_14_BOLD, LOGICAL_W / 2, 360,
                                  "No GitHub PRs linked to this thread yet", GRAY_TEXT_MUTED);
        } else {
            int ry = sec_y + 12;
            int rh = 48;
            int cnt = p->pr_count > 6 ? 6 : p->pr_count;
            for (int i = 0; i < cnt; i++) {
                const BoardPr *pr = &p->prs[i];
                fb_draw_text(fb, &FONT_MONO_14_BOLD, PAD_X + 2, ry + 29, pr->num, GRAY_BLACK, 60);
                fb_draw_text(fb, &FONT_MONO_13_BOLD, PAD_X + 64, ry + 29, pr->title, GRAY_BLACK, 430);
                fb_draw_text(fb, &FONT_MONO_12, PAD_X + 506, ry + 29, pr->branch, GRAY_TEXT_MUTED, 146);

                int pw = 76;
                int ph = 24;
                int px = LOGICAL_W - PAD_X - pw;
                int py = ry + (rh - ph) / 2;
                bool is_open = (strcmp(pr->state, "OPEN") == 0 || strcmp(pr->state, "DRAFT") == 0);
                if (is_open) {
                    fb_fill_round_rect(fb, px, py, pw, ph, 3, GRAY_BLACK);
                    fb_draw_text_centered(fb, &FONT_MONO_12, px + pw / 2, py + 16, pr->state, GRAY_WHITE);
                } else {
                    fb_stroke_round_rect(fb, px, py, pw, ph, 3, 2, GRAY_BLACK);
                    fb_draw_text_centered(fb, &FONT_MONO_12, px + pw / 2, py + 16, pr->state, GRAY_BLACK);
                }

                if (i < cnt - 1) {
                    fb_hline(fb, PAD_X, ry + rh - 1, INNER_W, 1, GRAY_RULE_LIGHT);
                }
                ry += rh;
            }
        }
    }

    /* Footer: Quick-Jump Project Pills + Clear button */
    fb_hline(fb, PAD_X, 542, INNER_W, 2, GRAY_BLACK);
    fb_draw_text(fb, &FONT_MONO_12, PAD_X, 571, "JUMP:", GRAY_TEXT_MUTED, 52);

    int jx = PAD_X + 50;
    for (int i = 0; i < board->project_count && i < 8; i++) {
        const BoardProject *item = &board->projects[i];
        int pw = 40;
        int ph = 28;
        int py = 552;
        bool cur = (strcmp(item->code, p->code) == 0);
        if (cur) {
            fb_fill_round_rect(fb, jx, py, pw, ph, 4, GRAY_BLACK);
            fb_draw_text_centered(fb, &FONT_MONO_13_BOLD, jx + pw / 2, py + 19, item->code, GRAY_WHITE);
        } else {
            fb_stroke_round_rect(fb, jx, py, pw, ph, 4, 2, GRAY_BLACK);
            fb_draw_text_centered(fb, &FONT_MONO_13_BOLD, jx + pw / 2, py + 19, item->code, GRAY_BLACK);
        }
        ui_add_hit(ui, jx - 3, py - 6, pw + 6, ph + 12, HIT_OPEN_PROJECT, item->code);
        jx += pw + 8;
    }

    char upd_buf[40];
    char ago_u[16];
    str_upper_copy(ago_u, sizeof(ago_u), p->updated_ago);
    snprintf(upd_buf, sizeof(upd_buf), "UPD %s", ago_u);
    fb_draw_text_right(fb, &FONT_MONO_12, LOGICAL_W - PAD_X - 78, 571, upd_buf, GRAY_TEXT_MUTED, 110);
    render_clear_button(ui, fb, LOGICAL_W - PAD_X, 553);
}

void ui_render(UiState *ui, Framebuffer *fb, const BoardState *board) {
    ui->hit_count = 0;
    fb_clear(fb, GRAY_BG);

    if (ui->view == VIEW_DETAIL) {
        const BoardProject *p = find_selected_project(ui, board);
        if (p) {
            render_detail(ui, fb, board, p);
            return;
        }
        ui->view = VIEW_OVERVIEW;
        ui->selected_code[0] = '\0';
    }

    render_overview(ui, fb, board);
}

bool ui_handle_tap(UiState *ui, const BoardState *board, int x, int y, EpdcRefreshMode *out_mode) {
    (void)board;
    for (int i = 0; i < ui->hit_count; i++) {
        const HitRegion *hr = &ui->hits[i];
        if (x >= hr->x && x < hr->x + hr->w && y >= hr->y && y < hr->y + hr->h) {
            switch (hr->action) {
                case HIT_OPEN_PROJECT:
                    ui->view = VIEW_DETAIL;
                    if (strcmp(ui->selected_code, hr->code) != 0) {
                        ui->detail_tab = DETAIL_TAB_SUBAGENTS;
                    }
                    strncpy(ui->selected_code, hr->code, sizeof(ui->selected_code) - 1);
                    ui->selected_code[sizeof(ui->selected_code) - 1] = '\0';
                    *out_mode = EPDC_REFRESH_PARTIAL_GL16;
                    return true;

                case HIT_BACK_OVERVIEW:
                    ui->view = VIEW_OVERVIEW;
                    ui->selected_code[0] = '\0';
                    *out_mode = EPDC_REFRESH_PARTIAL_GL16;
                    return true;

                case HIT_TAB_SUBAGENTS:
                    if (ui->detail_tab != DETAIL_TAB_SUBAGENTS) {
                        ui->detail_tab = DETAIL_TAB_SUBAGENTS;
                        *out_mode = EPDC_REFRESH_PARTIAL_DU;
                        return true;
                    }
                    return false;

                case HIT_TAB_PRS:
                    if (ui->detail_tab != DETAIL_TAB_PRS) {
                        ui->detail_tab = DETAIL_TAB_PRS;
                        *out_mode = EPDC_REFRESH_PARTIAL_DU;
                        return true;
                    }
                    return false;

                case HIT_CLEAR_REFRESH:
                    ui->partial_count = 0;
                    *out_mode = EPDC_REFRESH_FULL_GC16;
                    return true;

                default:
                    return false;
            }
        }
    }
    return false;
}
