/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* The harness programs of email-cli's and tg-cli's PTY tests, rebuilt on libtuikit. */

/*
 * The harness programs of email-cli's and tg-cli's PTY tests, rebuilt on
 * libtuikit with their output kept, so the scenarios of those tests run
 * here unchanged in substance:
 *
 *   input_line [initial]   email-cli input_line_harness: tui_lineedit_run()
 *                          on row 1 with "Path: " and a path completer on
 *                          the Tab / Shift-Tab / render_below hooks;
 *                          prints "RESULT: <text>" or "CANCELLED"
 *   readline               tg-cli rl_harness: tui_readline() in a loop with
 *                          history; prints "ACCEPTED:<line>"
 *   password [big]         tg-cli password_harness "prompt": "ACCEPTED:<pw>"
 *                          or "ERROR"
 *   password_then_echo     the same, then a plain line: "ECHO:<line>"
 *   read_key               tg-cli terminal_coverage_harness read_key:
 *                          "READY", then "KEY:<name>" per key
 *   wait_key               ... wait_key: "READY", then WAIT_KEY:READY /
 *                          TIMEOUT / INTR
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "tuikit/tuikit.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── A path completer on the line editor's hooks ─────────────────────── */

#define MAX_MATCH 64

typedef struct {
    char   names[MAX_MATCH][256];  /* sorted entry names */
    int    count;
    int    idx;                    /* current, -1: not cycling */
    size_t name_start;             /* byte where the completed name begins */
    char   after[4096];            /* the text as completion left it */
    size_t after_cur;
} completer;

static completer g_pc;

static int cmp_names(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

/* Lists the entries of the directory part of the text before the cursor
 * that start with its last component; dot-files only for a dot prefix. */
static void scan(tui_lineedit *le) {
    g_pc.count = 0;
    g_pc.idx = -1;
    char dir[4096] = ".";
    size_t slash = (size_t)-1;
    for (size_t i = 0; i < le->cur; i++)
        if (le->buf[i] == '/') slash = i;
    const char *prefix = le->buf;
    size_t plen = le->cur;
    g_pc.name_start = 0;
    if (slash != (size_t)-1) {
        size_t n = slash + 1;
        memcpy(dir, le->buf, n);
        dir[n] = '\0';
        prefix = le->buf + n;
        plen = le->cur - n;
        g_pc.name_start = n;
    }
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && g_pc.count < MAX_MATCH) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (de->d_name[0] == '.' && !(plen > 0 && prefix[0] == '.')) continue;
        if (strncmp(de->d_name, prefix, plen) != 0) continue;
        snprintf(g_pc.names[g_pc.count++], 256, "%s", de->d_name);
    }
    closedir(d);
    qsort(g_pc.names, (size_t)g_pc.count, sizeof(g_pc.names[0]), cmp_names);
}

static void show(tui_lineedit *le, int idx) {
    g_pc.idx = idx;
    tui_lineedit_replace(le, g_pc.name_start, le->cur, g_pc.names[idx]);
    snprintf(g_pc.after, sizeof(g_pc.after), "%s", le->buf);
    g_pc.after_cur = le->cur;
}

/* Cycling goes on while the text is what the last completion left. */
static int still_cycling(const tui_lineedit *le) {
    return g_pc.idx >= 0 && le->cur == g_pc.after_cur && !strcmp(le->buf, g_pc.after);
}

static void tab(tui_lineedit *le) {
    if (!still_cycling(le)) {
        scan(le);
        if (g_pc.count > 0) show(le, 0);
        return;
    }
    show(le, (g_pc.idx + 1) % g_pc.count);
}

static void shift_tab(tui_lineedit *le) {
    if (!still_cycling(le)) {
        scan(le);
        if (g_pc.count > 0) show(le, g_pc.count - 1);
        return;
    }
    show(le, (g_pc.idx + g_pc.count - 1) % g_pc.count);
}

/* The completion bar on the row below the input, as email-cli draws it. */
static void below(const tui_lineedit *le) {
    tui_buf b;
    tui_buf_init(&b);
    tui_buf_printf(&b, "\033[%d;1H\033[2K", le->trow + 1);
    if (still_cycling(le) && g_pc.count > 1) {
        for (int i = 0; i < g_pc.count; i++)
            tui_buf_printf(&b, "%s%s", i ? "  " : "  ", g_pc.names[i]);
    }
    tui_buf_write_fd(&b, STDOUT_FILENO);
    tui_buf_free(&b);
}

static int mode_input_line(const char *initial) {
    tui_tty_raw *raw = tui_tty_raw_enter(STDIN_FILENO, 0);
    if (!raw) {
        fprintf(stderr, "raw mode failed\n");
        return 1;
    }
    printf("\033[2J\033[H\033[?25l");
    fflush(stdout);
    char buf[4096];
    tui_lineedit il;
    tui_lineedit_init(&il, buf, sizeof(buf), initial);
    il.tab_fn = tab;
    il.shift_tab_fn = shift_tab;
    il.render_below = below;
    /* email-cli drew "  Path: ": the prompt from column 3. */
    int ok = tui_lineedit_run(&il, STDIN_FILENO, STDOUT_FILENO, 1, 3, "Path: ");
    tui_tty_raw_exit(&raw);
    printf("\033[3;1H\033[?25l");
    if (ok == 1) printf("RESULT: %s\n", il.buf);
    else printf("CANCELLED\n");
    fflush(stdout);
    tui_lineedit_free(&il);
    return 0;
}

/* ── tg-cli ──────────────────────────────────────────────────────────── */

static int mode_readline(void) {
    tui_history hist;
    tui_history_init(&hist, 256);
    tui_readline_opts o;
    tui_readline_opts_init(&o);
    o.history = &hist;
    char buf[256];
    for (;;) {
        int rc = tui_readline("rl> ", buf, sizeof(buf), &o);
        if (rc < 0) break;
        printf("ACCEPTED:%s\n", buf);
        fflush(stdout);
    }
    tui_history_free(&hist);
    return 0;
}

static int mode_password(size_t size, int then_echo) {
    char pw[2048];
    memset(pw, 0, sizeof(pw));
    if (size > sizeof(pw)) size = sizeof(pw);
    int rc = tui_tty_read_password(STDIN_FILENO, STDOUT_FILENO, "Password", pw, size);
    if (rc < 0) {
        printf("ERROR\n");
        fflush(stdout);
        return 1;
    }
    printf("ACCEPTED:%s\n", pw);
    fflush(stdout);
    if (!then_echo) return 0;
    char line[256];
    if (!fgets(line, sizeof(line), stdin)) {
        printf("ECHO_ERROR\n");
        return 1;
    }
    line[strcspn(line, "\r\n")] = '\0';
    printf("ECHO:%s\n", line);
    fflush(stdout);
    return 0;
}

/* tg-cli's key names. */
static const char *tg_name(const tui_event *ev, char *buf, size_t size) {
    if (TUI_EV_IS_CTRL(ev, 'c')) return "QUIT";
    if (TUI_EV_IS_CTRL(ev, 'd')) return "CTRL_D";
    if (TUI_EV_IS_CTRL(ev, 'a')) return "CTRL_A";
    if (TUI_EV_IS_CTRL(ev, 'e')) return "CTRL_E";
    if (TUI_EV_IS_CTRL(ev, 'k')) return "CTRL_K";
    if (TUI_EV_IS_CTRL(ev, 'w')) return "CTRL_W";
    if (ev->mods == 0) {
        switch (ev->key) {
        case TUI_KEY_ENTER:     return "ENTER";
        case TUI_KEY_ESC:       return "ESC";
        case TUI_KEY_BACKSPACE: return "BACK";
        case TUI_KEY_LEFT:      return "LEFT";
        case TUI_KEY_RIGHT:     return "RIGHT";
        case TUI_KEY_UP:        return "UP";
        case TUI_KEY_DOWN:      return "DOWN";
        case TUI_KEY_HOME:      return "HOME";
        case TUI_KEY_END:       return "END";
        case TUI_KEY_DELETE:    return "DELETE";
        case TUI_KEY_PGUP:      return "PGUP";
        case TUI_KEY_PGDN:      return "PGDN";
        case TUI_KEY_CHAR:
            snprintf(buf, size, "CHAR:%s", ev->utf8);
            return buf;
        default: break;
        }
    }
    snprintf(buf, size, "OTHER:");
    tui_event_name(ev, buf + 6, size - 6);
    return buf;
}

static int mode_read_key(void) {
    tui_tty_raw *raw = tui_tty_raw_enter(STDIN_FILENO, 0);
    if (!raw) {
        printf("RAW:FAIL\n");
        return 1;
    }
    printf("READY\r\n");
    fflush(stdout);
    for (int i = 0; i < 64; i++) {
        if (tui_tty_wait(STDIN_FILENO, 3000) <= 0) {
            printf("TIMEOUT\r\n");
            break;
        }
        tui_event ev;
        if (tui_tty_read_key(STDIN_FILENO, &ev) != 1) break;
        char name[64];
        if (TUI_EV_IS_CHAR(&ev, 'q')) {
            printf("KEY:q\r\n");
            break;
        }
        const char *n = tg_name(&ev, name, sizeof(name));
        printf("KEY:%s\r\n", n);
        fflush(stdout);
        if (!strcmp(n, "QUIT") || !strcmp(n, "CTRL_D")) break;
    }
    fflush(stdout);
    tui_tty_raw_exit(&raw);
    return 0;
}

static int mode_wait_key(void) {
    printf("READY\n");
    fflush(stdout);
    int rc = tui_tty_wait(STDIN_FILENO, 3000);
    printf("WAIT_KEY:%s\n", rc > 0 ? "READY" : rc == 0 ? "TIMEOUT" : "INTR");
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: compat_harness <mode> [arg]\n");
        return 2;
    }
    const char *m = argv[1];
    if (!strcmp(m, "input_line")) return mode_input_line(argc > 2 ? argv[2] : "");
    if (!strcmp(m, "readline")) return mode_readline();
    if (!strcmp(m, "password")) return mode_password(argc > 2 ? 2048 : 64, 0);
    if (!strcmp(m, "password_then_echo")) return mode_password(64, 1);
    if (!strcmp(m, "read_key")) return mode_read_key();
    if (!strcmp(m, "wait_key")) return mode_wait_key();
    fprintf(stderr, "unknown mode: %s\n", m);
    return 2;
}
