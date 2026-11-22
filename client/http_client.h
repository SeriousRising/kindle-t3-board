#ifndef T3_HTTP_CLIENT_H
#define T3_HTTP_CLIENT_H

#include <stdbool.h>

#define MAX_PROJECTS  10
#define MAX_SUBAGENTS  8
#define MAX_PRS        8

typedef struct {
    char state[16];  /* "running", "waiting", "done" */
    char task[96];
    char agent[32];
    char meta[24];
} BoardSubagent;

typedef struct {
    char num[16];    /* "#70" */
    char state[16];  /* "OPEN", "DRAFT", "MERGED", "CLOSED" */
    char title[96];
    char branch[48];
} BoardPr;

typedef struct {
    char code[8];
    char name[48];
    char repo[64];
    char status[16]; /* "running", "waiting" */
    char lead_agent[40];
    char lead_thread[96];
    char pr_badge[16];
    char active_work[96];
    char pr_summary[64];
    int  live_subagents;
    int  total_prs;
    char updated_ago[16];

    BoardSubagent subagents[MAX_SUBAGENTS];
    int subagent_count;

    BoardPr prs[MAX_PRS];
    int pr_count;
} BoardProject;

typedef struct {
    char clock[32];
    int  active_projects;
    int  running_count;
    int  waiting_count;
    int  live_subagents;
    int  open_prs;
    char last_summary[96];

    BoardProject projects[MAX_PROJECTS];
    int project_count;

    bool has_error;
    char error_msg[96];
} BoardState;

int board_fetch_http(const char *host, int port, const char *path, BoardState *out);
int board_parse_tsv(const char *tsv, BoardState *out);

#endif /* T3_HTTP_CLIENT_H */
