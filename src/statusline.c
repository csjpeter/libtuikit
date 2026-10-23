/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-14 */
/* The status line: hints that fit, a message, a wait text, a position, a prompt. */

#include "tuikit/statusline.h"
#include "tuikit/text.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define LEAD      1      /* a blank before the left text and after the right */
#define SEP       2      /* blanks between hints */
#define MAX_PIECE (2 * TUI_STATUSLINE_HINTS_MAX + 8)
#define TEXT_MAX  4096

/* One laid-out piece of text: at a column, in a style. */
typedef struct {
    int       col;
    int       off;        /* into line.text */
    int       prompt;     /* the prompt editor draws this one */
    tui_style st;
} piece;

typedef struct {
    piece piece[MAX_PIECE];
    int   n;
    char  text[TEXT_MAX];
    int   used;
    int   shown;
    tui_style fill;   /* the bar under the pieces */
} line;

static tui_style bar_of(const tui_statusline *sl) { return sl->bar; }

static tui_style with_attrs(tui_style st, unsigned a) {
    st.attrs = (uint16_t)(st.attrs | a);
    return st;
}

static void add(line *ln, int col, const char *text, tui_style st, int prompt) {
    size_t n = strlen(text);
    if (ln->n >= MAX_PIECE || ln->used + (int)n + 1 > TEXT_MAX) return;
    piece *p = &ln->piece[ln->n++];
    p->col = col;
    p->off = ln->used;
    p->prompt = prompt;
    p->st = st;
    memcpy(ln->text + ln->used, text, n + 1);
    ln->used += (int)n + 1;
}

/* Copies @p src into @p dst (@p size bytes): controls become spaces, a
 * character is never cut. */
static void copy_clean(char *dst, size_t size, const char *src) {
    size_t len = strlen(src);
    if (len > size - 1) {
        len = size - 1;
        while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
    }
    dst[len] = '\0';
}

static int hint_width(const char *key, const char *label) {
    int w = tui_str_width(key ? key : "");
    int lw = tui_str_width(label ? label : "");
    return w + (lw > 0 ? 1 + lw : 0);
}

/* One "key label" entry at @p col: the key bold. */
static void add_entry(line *ln, int col, const char *key, const char *label,
                      tui_style bar) {
    key = key ? key : "";
    add(ln, col, key, with_attrs(bar, TUI_ATTR_BOLD), 0);
    if (label && label[0]) add(ln, col + tui_str_width(key) + 1, label, bar, 0);
}

static void layout_hints(const tui_statusline *sl, int avail, line *ln) {
    int keep[TUI_STATUSLINE_HINTS_MAX] = { 0 };
    int w[TUI_STATUSLINE_HINTS_MAX];
    int used = 0;
    tui_style bar = bar_of(sl);
    int have_help = sl->help_key && sl->help_key[0];
    int hw = have_help ? hint_width(sl->help_key, sl->help_label) : 0;

    for (int i = 0; i < sl->nhints; i++)
        w[i] = hint_width(sl->hints[i].key, sl->hints[i].label);
    if (have_help && hw > avail) {
        /* Not even the help hint fits: it is cut, as a message is. */
        char buf[160], out[512];
        snprintf(buf, sizeof(buf), "%s %s", sl->help_key,
                 sl->help_label ? sl->help_label : "");
        if (avail > 0) {
            tui_str_fit_ellipsis(buf, avail, out, sizeof(out));
            add(ln, LEAD, out, bar, 0);
        }
        return;
    }
    used = hw;
    for (;;) {
        int best = -1;
        for (int i = 0; i < sl->nhints; i++) {
            if (keep[i] || w[i] == 0) continue;
            if (best < 0 || sl->hints[i].priority > sl->hints[best].priority) best = i;
        }
        if (best < 0) break;
        int cost = w[best] + (used > 0 ? SEP : 0);
        if (used + cost > avail) break;
        keep[best] = 1;
        used += cost;
        ln->shown++;
    }
    int col = LEAD;
    for (int i = 0; i < sl->nhints; i++) {
        if (!keep[i]) continue;
        add_entry(ln, col, sl->hints[i].key, sl->hints[i].label, bar);
        col += w[i] + SEP;
    }
    if (have_help) add_entry(ln, col, sl->help_key, sl->help_label, bar);
}

static void layout(const tui_statusline *sl, int cols, int hints_only, line *ln) {
    /* out holds tmp plus the progress suffix (up to 47 bytes): sized so that
     * the concatenation can never be truncated. */
    char tmp[1024], out[1024 + 48], pos[256];
    ln->n = 0;
    ln->used = 0;
    ln->shown = 0;
    ln->fill = bar_of(sl);
    if (cols <= 0) return;

    int show_prompt = sl->prompting && !hints_only;
    int show_msg = !show_prompt && sl->message[0] && !hints_only;
    int show_wait = !show_prompt && !show_msg && sl->wait[0] && !hints_only;
    if (show_msg) ln->fill = with_attrs(sl->msg_style, TUI_ATTR_REVERSE);
    if (show_wait) ln->fill = with_attrs(TUI_STYLE_PROGRESS, TUI_ATTR_REVERSE);

    /* The position is laid out first for its width, added last, so that the
     * pieces stay in column order. */
    int rw = 0;
    if (sl->position[0] && cols > 2 * LEAD)
        rw = tui_str_fit_ellipsis(sl->position, cols - 2 * LEAD, pos, sizeof(pos));
    int avail = cols - 2 * LEAD - (rw > 0 ? rw + 1 : 0);

    if (avail > 0) {
        if (show_prompt) {
            snprintf(tmp, sizeof(tmp), "%s%s", sl->prompt, sl->pbuf);
            tui_str_fit_ellipsis(tmp, avail, out, sizeof(out));
            add(ln, LEAD, out, ln->fill, 1);
        } else if (show_msg) {
            tui_str_fit_ellipsis(sl->message, avail, out, sizeof(out));
            add(ln, LEAD, out, ln->fill, 0);
        } else if (show_wait) {
            char suffix[48] = "";
            if (sl->wait_total > 0)
                snprintf(suffix, sizeof(suffix), " %d/%d", sl->wait_done, sl->wait_total);
            int sw = tui_str_width(suffix);
            if (sw >= avail) { suffix[0] = '\0'; sw = 0; }
            tui_str_fit_ellipsis(sl->wait, avail - sw, tmp, sizeof(tmp));
            snprintf(out, sizeof(out), "%s%s", tmp, suffix);
            add(ln, LEAD, out, ln->fill, 0);
        } else {
            layout_hints(sl, avail, ln);
        }
    }
    if (rw > 0) add(ln, cols - LEAD - rw, pos, ln->fill, 0);
}

/* ── The model ─────────────────────────────────────────────────────── */

void tui_statusline_init(tui_statusline *sl) {
    if (!sl) return;
    memset(sl, 0, sizeof(*sl));
    sl->bar = tui_scheme_style(TUI_ROLE_STATUS);
    sl->help_key = "?";
    sl->help_label = "help";
    sl->msg_style = tui_scheme_style(TUI_ROLE_TEXT);
    tui_lineedit_init(&sl->le, sl->pbuf, sizeof(sl->pbuf), "");
}

void tui_statusline_free(tui_statusline *sl) {
    if (!sl) return;
    tui_lineedit_free(&sl->le);
}

int tui_statusline_set_hints(tui_statusline *sl, const tui_statusline_hint *hints, int n) {
    if (!sl) { errno = EINVAL; return -1; }
    if (!hints || n <= 0) {
        sl->hints = NULL;
        sl->nhints = 0;
        return 0;
    }
    if (n > TUI_STATUSLINE_HINTS_MAX) { errno = EINVAL; return -1; }
    sl->hints = hints;
    sl->nhints = n;
    return 0;
}

void tui_statusline_set_help(tui_statusline *sl, const char *key, const char *label) {
    if (!sl) return;
    sl->help_key = key;
    sl->help_label = label;
}

int tui_statusline_hint_count(const tui_statusline *sl) {
    return sl ? sl->nhints : 0;
}

const tui_statusline_hint *tui_statusline_hint_at(const tui_statusline *sl, int i) {
    if (!sl || i < 0 || i >= sl->nhints) return NULL;
    return &sl->hints[i];
}

int tui_statusline_hints_fit(const tui_statusline *sl, int cols) {
    if (!sl) return 0;
    line ln;
    layout(sl, cols, 1, &ln);
    return ln.shown;
}

void tui_statusline_set_message(tui_statusline *sl, const char *text, tui_style style) {
    if (!sl) return;
    copy_clean(sl->message, sizeof(sl->message), text ? text : "");
    sl->msg_style = style;
}

void tui_statusline_clear_message(tui_statusline *sl) {
    if (sl) sl->message[0] = '\0';
}

const char *tui_statusline_message(const tui_statusline *sl) {
    return sl ? sl->message : "";
}

void tui_statusline_set_wait(tui_statusline *sl, const char *text, int done, int total) {
    if (!sl) return;
    copy_clean(sl->wait, sizeof(sl->wait), text ? text : "");
    sl->wait_done = done;
    sl->wait_total = total;
}

void tui_statusline_clear_wait(tui_statusline *sl) {
    if (sl) sl->wait[0] = '\0';
}

void tui_statusline_set_position(tui_statusline *sl, const char *text) {
    if (!sl) return;
    copy_clean(sl->position, sizeof(sl->position), text ? text : "");
}

void tui_statusline_set_position_index(tui_statusline *sl, int cur, int total) {
    if (!sl) return;
    if (total < 1) {
        sl->position[0] = '\0';
        return;
    }
    snprintf(sl->position, sizeof(sl->position), "[%d/%d]", cur, total);
}

void tui_statusline_begin_prompt(tui_statusline *sl, const char *prompt, const char *initial) {
    if (!sl) return;
    copy_clean(sl->prompt, sizeof(sl->prompt), prompt ? prompt : "");
    tui_lineedit_free(&sl->le);
    tui_lineedit_init(&sl->le, sl->pbuf, sizeof(sl->pbuf), initial);
    sl->prompting = 1;
}

int tui_statusline_prompt_active(const tui_statusline *sl) {
    return sl && sl->prompting;
}

const char *tui_statusline_prompt_text(const tui_statusline *sl) {
    return sl ? sl->pbuf : "";
}

tui_statusline_result tui_statusline_handle(tui_statusline *sl, const tui_event *ev) {
    if (!sl || !ev) return TUI_STATUSLINE_IGNORED;
    if (sl->prompting) {
        switch (tui_lineedit_handle(&sl->le, ev)) {
        case TUI_EDIT_ACCEPT:
            sl->prompting = 0;
            return TUI_STATUSLINE_ACCEPT;
        case TUI_EDIT_CANCEL:
        case TUI_EDIT_EOF:
            sl->prompting = 0;
            return TUI_STATUSLINE_CANCEL;
        case TUI_EDIT_CHANGED:
            return TUI_STATUSLINE_CHANGED;
        default:
            return TUI_STATUSLINE_IGNORED;
        }
    }
    if ((ev->type == TUI_EV_KEY || ev->type == TUI_EV_PASTE) && sl->message[0]) {
        sl->message[0] = '\0';
        return TUI_STATUSLINE_CHANGED;
    }
    return TUI_STATUSLINE_IGNORED;
}

/* ── Drawing and plain text ────────────────────────────────────────── */

void tui_statusline_render(tui_statusline *sl, tui_canvas c) {
    int rows = tui_canvas_rows(c), cols = tui_canvas_cols(c);
    if (!sl || rows <= 0 || cols <= 0) return;
    int row = rows - 1;
    line ln;
    layout(sl, cols, 0, &ln);
    tui_canvas_fill(c, tui_rect_make(row, 0, 1, cols), ln.fill);
    for (int i = 0; i < ln.n; i++) {
        const piece *p = &ln.piece[i];
        if (p->prompt) {
            int rw = cols - LEAD;
            for (int j = 0; j < ln.n; j++)
                if (!ln.piece[j].prompt) rw = ln.piece[j].col - 1;
            tui_canvas sub = tui_canvas_sub(c, tui_rect_make(row, LEAD, 1, rw - LEAD));
            tui_lineedit_render(&sl->le, sub, sl->prompt, ln.fill);
        } else {
            tui_canvas_put(c, row, p->col, ln.text + p->off, p->st);
        }
    }
}

int tui_statusline_format(const tui_statusline *sl, int cols, char *out, size_t outsz) {
    if (!sl || !out || outsz == 0) return -1;
    line ln;
    layout(sl, cols, 0, &ln);
    size_t n = 0;
    int col = 0, rc = 0;
    for (int i = 0; i < ln.n && rc == 0; i++) {
        const piece *p = &ln.piece[i];
        const char *t = ln.text + p->off;
        size_t pad = p->col > col ? (size_t)(p->col - col) : 0;
        size_t need = pad + strlen(t);
        if (n + need + 1 > outsz) {
            rc = -1;
            need = outsz - 1 - n;
            if (pad > need) pad = need;
        }
        memset(out + n, ' ', pad);
        n += pad;
        size_t room = need - pad;
        while (rc != 0 && room > 0 && ((unsigned char)t[room] & 0xC0) == 0x80) room--;
        if (room > 0) memcpy(out + n, t, room);
        n += room;
        col = p->col + tui_str_width(t);
    }
    out[n] = '\0';
    return rc == 0 ? (int)n : -1;
}
