#include "http_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define HTTP_BUF_CAP (128 * 1024)

static void copy_field(char *dst, size_t dst_sz, const char *src, size_t len) {
    if (dst_sz == 0) return;
    if (len >= dst_sz) len = dst_sz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static int split_tabs(char *line, char **cols, int max_cols) {
    int n = 0;
    char *p = line;
    while (n < max_cols) {
        cols[n++] = p;
        char *tab = strchr(p, '\t');
        if (!tab) break;
        *tab = '\0';
        p = tab + 1;
    }
    return n;
}

static BoardProject *find_project(BoardState *st, const char *code) {
    for (int i = 0; i < st->project_count; i++) {
        if (strcmp(st->projects[i].code, code) == 0) {
            return &st->projects[i];
        }
    }
    return NULL;
}

int board_parse_tsv(const char *tsv, BoardState *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->clock, sizeof(out->clock), "-- -- --- --:--");
    snprintf(out->last_summary, sizeof(out->last_summary), "Waiting for T3 bridge...");

    if (!tsv || !*tsv) return -1;

    char *copy = strdup(tsv);
    if (!copy) return -1;

    char *line = copy;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';

        if (*line != '\0') {
            char *cols[16];
            int n = split_tabs(line, cols, 16);

            if (n >= 8 && strcmp(cols[0], "META") == 0) {
                copy_field(out->clock, sizeof(out->clock), cols[1], strlen(cols[1]));
                out->active_projects = atoi(cols[2]);
                out->running_count = atoi(cols[3]);
                out->waiting_count = atoi(cols[4]);
                out->live_subagents = atoi(cols[5]);
                out->open_prs = atoi(cols[6]);
                copy_field(out->last_summary, sizeof(out->last_summary), cols[7], strlen(cols[7]));
            } else if (n >= 13 && strcmp(cols[0], "PROJ") == 0 && out->project_count < MAX_PROJECTS) {
                BoardProject *p = &out->projects[out->project_count++];
                memset(p, 0, sizeof(*p));
                copy_field(p->code, sizeof(p->code), cols[1], strlen(cols[1]));
                copy_field(p->name, sizeof(p->name), cols[2], strlen(cols[2]));
                copy_field(p->repo, sizeof(p->repo), cols[3], strlen(cols[3]));
                copy_field(p->status, sizeof(p->status), cols[4], strlen(cols[4]));
                copy_field(p->lead_agent, sizeof(p->lead_agent), cols[5], strlen(cols[5]));
                copy_field(p->lead_thread, sizeof(p->lead_thread), cols[6], strlen(cols[6]));
                copy_field(p->pr_badge, sizeof(p->pr_badge), cols[7], strlen(cols[7]));
                copy_field(p->active_work, sizeof(p->active_work), cols[8], strlen(cols[8]));
                copy_field(p->pr_summary, sizeof(p->pr_summary), cols[9], strlen(cols[9]));
                p->live_subagents = atoi(cols[10]);
                p->total_prs = atoi(cols[11]);
                copy_field(p->updated_ago, sizeof(p->updated_ago), cols[12], strlen(cols[12]));
            } else if (n >= 6 && strcmp(cols[0], "SUB") == 0) {
                BoardProject *p = find_project(out, cols[1]);
                if (p && p->subagent_count < MAX_SUBAGENTS) {
                    BoardSubagent *s = &p->subagents[p->subagent_count++];
                    copy_field(s->state, sizeof(s->state), cols[2], strlen(cols[2]));
                    copy_field(s->task, sizeof(s->task), cols[3], strlen(cols[3]));
                    copy_field(s->agent, sizeof(s->agent), cols[4], strlen(cols[4]));
                    copy_field(s->meta, sizeof(s->meta), cols[5], strlen(cols[5]));
                }
            } else if (n >= 6 && strcmp(cols[0], "PR") == 0) {
                BoardProject *p = find_project(out, cols[1]);
                if (p && p->pr_count < MAX_PRS) {
                    BoardPr *pr = &p->prs[p->pr_count++];
                    copy_field(pr->num, sizeof(pr->num), cols[2], strlen(cols[2]));
                    copy_field(pr->state, sizeof(pr->state), cols[3], strlen(cols[3]));
                    copy_field(pr->title, sizeof(pr->title), cols[4], strlen(cols[4]));
                    copy_field(pr->branch, sizeof(pr->branch), cols[5], strlen(cols[5]));
                }
            }
        }

        line = nl ? (nl + 1) : NULL;
    }

    free(copy);
    return 0;
}

int board_fetch_http(const char *host, int port, const char *path, BoardState *out) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    int gai = getaddrinfo(host, port_str, &hints, &res);
    if (gai != 0 || !res) {
        out->has_error = true;
        snprintf(out->error_msg, sizeof(out->error_msg), "DNS/host error: %s", host);
        return -1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        out->has_error = true;
        snprintf(out->error_msg, sizeof(out->error_msg), "socket() failed");
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        int e = errno;
        close(fd);
        freeaddrinfo(res);
        out->has_error = true;
        snprintf(out->error_msg, sizeof(out->error_msg), "Connect %s:%d failed (%s)",
                 host, port, strerror(e));
        return -1;
    }
    freeaddrinfo(res);

    char req[512];
    int req_len = snprintf(req, sizeof(req),
                           "GET %s HTTP/1.0\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
                           path, host, port);
    if (send(fd, req, (size_t)req_len, 0) < 0) {
        close(fd);
        out->has_error = true;
        snprintf(out->error_msg, sizeof(out->error_msg), "HTTP send failed");
        return -1;
    }

    char *buf = (char *)malloc(HTTP_BUF_CAP);
    if (!buf) {
        close(fd);
        return -1;
    }

    size_t total = 0;
    while (total + 1 < HTTP_BUF_CAP) {
        ssize_t r = recv(fd, buf + total, HTTP_BUF_CAP - 1 - total, 0);
        if (r <= 0) break;
        total += (size_t)r;
    }
    buf[total] = '\0';
    close(fd);

    char *body = strstr(buf, "\r\n\r\n");
    if (!body) {
        free(buf);
        out->has_error = true;
        snprintf(out->error_msg, sizeof(out->error_msg), "Malformed HTTP response");
        return -1;
    }
    body += 4;

    if (strncmp(buf, "HTTP/1.", 7) != 0 || strstr(buf, " 200 ") == NULL) {
        free(buf);
        out->has_error = true;
        snprintf(out->error_msg, sizeof(out->error_msg), "Bridge returned non-200 status");
        return -1;
    }

    BoardState fresh;
    int rc = board_parse_tsv(body, &fresh);
    free(buf);
    if (rc == 0) {
        *out = fresh;
        out->has_error = false;
        out->error_msg[0] = '\0';
    }
    return rc;
}
