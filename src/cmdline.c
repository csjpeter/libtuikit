/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-11 */
/* The command line opened by ':'. */

#include "tuikit/cmdline.h"
#include "tuikit/screen.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct cmd {
    char          *name;   /* one block: name, NUL, help, NUL */
    const char    *help;
    tui_cmdline_fn fn;
    void          *user;
} cmd;

static cmd *cmds_of(const tui_cmdline *cl) {
    return (cmd *)cl->cmds;
}

void tui_cmdline_init(tui_cmdline *cl) {
    if (!cl) return;
    memset(cl, 0, sizeof *cl);
    tui_history_init(&cl->history, 0);
}

void tui_cmdline_free(tui_cmdline *cl) {
    if (!cl) return;
    for (int i = 0; i < cl->ncmds; i++) free(cmds_of(cl)[i].name);
    free(cl->cmds);
    tui_history_free(&cl->history);
    tui_lineedit_free(&cl->edit);
    memset(cl, 0, sizeof *cl);
}

static int find(const tui_cmdline *cl, const char *name, size_t n) {
    for (int i = 0; i < cl->ncmds; i++) {
        const char *c = cmds_of(cl)[i].name;
        if (strlen(c) == n && memcmp(c, name, n) == 0) return i;
    }
    return -1;
}

int tui_cmdline_register(tui_cmdline *cl, const char *name, const char *help, tui_cmdline_fn fn,
                         void *user) {
    if (!cl || !name || !name[0] || !fn || strpbrk(name, " \t\r\n")) {
        errno = EINVAL;
        return -1;
    }
    size_t nl = strlen(name), hl = help ? strlen(help) : 0;
    char *blk = malloc(nl + 1 + hl + 1);
    if (!blk) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(blk, name, nl + 1);
    memcpy(blk + nl + 1, help ? help : "", hl + 1);
    int at = find(cl, name, nl);
    if (at < 0) {
        void *grown = realloc(cl->cmds, (size_t)(cl->ncmds + 1) * sizeof(cmd));
        if (!grown) {
            free(blk);
            errno = ENOMEM;
            return -1;
        }
        cl->cmds = grown;
        at = cl->ncmds++;
    } else {
        free(cmds_of(cl)[at].name);
    }
    cmd *c = &cmds_of(cl)[at];
    c->name = blk;
    c->help = blk + nl + 1;
    c->fn = fn;
    c->user = user;
    return 0;
}

int tui_cmdline_count(const tui_cmdline *cl) {
    return cl ? cl->ncmds : 0;
}

const char *tui_cmdline_name(const tui_cmdline *cl, int i) {
    return cl && i >= 0 && i < cl->ncmds ? cmds_of(cl)[i].name : NULL;
}

const char *tui_cmdline_help(const tui_cmdline *cl, int i) {
    return cl && i >= 0 && i < cl->ncmds ? cmds_of(cl)[i].help : NULL;
}

int tui_cmdline_is_open(const tui_cmdline *cl) {
    return cl && cl->open;
}

const char *tui_cmdline_message(const tui_cmdline *cl, int *ok) {
    if (ok) *ok = cl ? cl->msg_ok : 0;
    return cl ? cl->msg : "";
}

/* Tab: completes the command name (the first word) to the longest prefix the
 * matching commands share; a lone match gets a blank after it, several are
 * listed in the hint. */
static void complete(tui_lineedit *le) {
    tui_cmdline *cl = le->user;
    cl->hint[0] = '\0';
    size_t n = le->cur;
    if (memchr(le->buf, ' ', n)) return;   /* only the name is completed */
    size_t common = 0, hl = 0;
    int matches = 0;
    const char *first = NULL;
    for (int i = 0; i < cl->ncmds; i++) {
        const char *c = cmds_of(cl)[i].name;
        if (strncmp(c, le->buf, n) != 0) continue;
        if (!matches++) {
            first = c;
            common = strlen(c);
        } else {
            size_t k = 0;
            while (k < common && c[k] == first[k]) k++;
            common = k;
        }
        size_t cl_len = strlen(c);
        if (hl + cl_len + 2 < sizeof cl->hint) {
            if (hl) cl->hint[hl++] = ' ';
            memcpy(cl->hint + hl, c, cl_len + 1);
            hl += cl_len;
        }
    }
    if (!matches) return;
    char word[TUI_CMDLINE_MAX];
    snprintf(word, sizeof word, "%.*s%s", (int)common, first, matches == 1 ? " " : "");
    if (matches == 1) cl->hint[0] = '\0';
    tui_lineedit_replace(le, 0, n, word);
}

static void close_line(tui_cmdline *cl) {
    cl->open = 0;
    cl->hint[0] = '\0';
    tui_lineedit_free(&cl->edit);
}

static void say(tui_cmdline *cl, int ok, const char *text) {
    snprintf(cl->msg, sizeof cl->msg, "%s", text);
    cl->msg_ok = ok;
}

static void run_line(tui_cmdline *cl) {
    const char *p = cl->buf;
    while (*p == ' ') p++;
    size_t n = strcspn(p, " ");
    if (n == 0) {
        cl->msg[0] = '\0';
        return;
    }
    tui_history_add(&cl->history, cl->buf);
    const char *args = p + n;
    while (*args == ' ') args++;
    int i = find(cl, p, n);
    if (i < 0) {
        char text[TUI_CMDLINE_MAX];
        snprintf(text, sizeof text, "unknown command: %.*s", (int)n, p);
        say(cl, 0, text);
        return;
    }
    cl->msg[0] = '\0';
    int rc = cmds_of(cl)[i].fn(cmds_of(cl)[i].user, args, cl->msg, sizeof cl->msg);
    cl->msg_ok = rc == 0;
}

tui_cmdline_result tui_cmdline_handle(tui_cmdline *cl, const tui_event *ev) {
    if (!cl || !ev) return TUI_CMDLINE_IGNORED;
    if (!cl->open) {
        if (cl->ncmds == 0 || ev->type != TUI_EV_KEY || ev->key != TUI_KEY_CHAR ||
            ev->mods != 0 || ev->cp != ':')
            return TUI_CMDLINE_IGNORED;
        tui_lineedit_init(&cl->edit, cl->buf, sizeof cl->buf, "");
        cl->edit.history = &cl->history;
        cl->edit.tab_fn = complete;
        cl->edit.user = cl;
        cl->msg[0] = '\0';
        cl->open = 1;
        return TUI_CMDLINE_EDITING;
    }
    if (ev->type == TUI_EV_KEY && ev->key == TUI_KEY_BACKSPACE && cl->edit.len == 0) {
        close_line(cl);
        return TUI_CMDLINE_CANCELLED;
    }
    cl->hint[0] = '\0';   /* a Tab below sets it again */
    switch (tui_lineedit_handle(&cl->edit, ev)) {
    case TUI_EDIT_ACCEPT:
        run_line(cl);
        close_line(cl);
        return TUI_CMDLINE_RAN;
    case TUI_EDIT_CANCEL:
    case TUI_EDIT_EOF:
        close_line(cl);
        return TUI_CMDLINE_CANCELLED;
    default:
        return TUI_CMDLINE_EDITING;
    }
}

void tui_cmdline_render(tui_cmdline *cl, tui_canvas c) {
    int rows = tui_canvas_rows(c);
    if (!cl || rows < 1) return;
    int cols = tui_canvas_cols(c);
    tui_canvas last = tui_canvas_sub(c, tui_rect_make(rows - 1, 0, 1, cols));
    if (cl->open) {
        if (rows >= 2 && cl->hint[0]) {
            tui_canvas above = tui_canvas_sub(c, tui_rect_make(rows - 2, 0, 1, cols));
            tui_canvas_clear(above, TUI_STYLE_HINT);
            tui_canvas_put(above, 0, 0, cl->hint, TUI_STYLE_HINT);
        }
        tui_lineedit_render(&cl->edit, last, ":", TUI_STYLE_DEFAULT);
    } else if (cl->msg[0]) {
        tui_style st = cl->msg_ok ? TUI_STYLE_OK : TUI_STYLE_ERROR;
        tui_canvas_clear(last, TUI_STYLE_DEFAULT);
        tui_canvas_put(last, 0, 0, cl->msg, st);
    }
}
