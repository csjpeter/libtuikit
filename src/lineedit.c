/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2026 Péter Császár */

/*
 * The line editor, its history, tui_lineedit_run() and tui_readline().
 *
 * The editing model is pure; the two loops around it are the only parts
 * that read a descriptor.  Positions are byte offsets kept on character
 * boundaries, and a combining mark always moves and is deleted together
 * with the character it belongs to.
 */

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "tuikit/lineedit.h"
#include "tuikit/buf.h"
#include "tuikit/text.h"
#include "tuikit/tty.h"
#include "internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* ── History ─────────────────────────────────────────────────────────── */

void tui_history_init(tui_history *h, int max) {
    if (!h) return;
    h->items = NULL;
    h->count = 0;
    h->max = max > 0 ? max : 256;
}

void tui_history_free(tui_history *h) {
    if (!h) return;
    for (int i = 0; i < h->count; i++) free(h->items[i]);
    free(h->items);
    tui_history_init(h, h->max);
}

int tui_history_add(tui_history *h, const char *line) {
    if (!h || !line) {
        errno = EINVAL;
        return -1;
    }
    if (!line[0]) return 0;
    if (h->count > 0 && strcmp(h->items[h->count - 1], line) == 0) return 0;
    size_t n = strlen(line);
    char *copy = malloc(n + 1);
    if (!copy) return -1;
    memcpy(copy, line, n + 1);
    if (!h->items) {
        h->items = calloc((size_t)h->max, sizeof(char *));
        if (!h->items) {
            free(copy);
            return -1;
        }
    }
    if (h->count == h->max) {
        free(h->items[0]);
        memmove(h->items, h->items + 1, (size_t)(h->max - 1) * sizeof(char *));
        h->count--;
    }
    h->items[h->count++] = copy;
    return 0;
}

int tui_history_count(const tui_history *h) {
    return h ? h->count : 0;
}

const char *tui_history_get(const tui_history *h, int i) {
    if (!h || i < 0 || i >= h->count) return NULL;
    return h->items[i];
}

/* ── Character boundaries ────────────────────────────────────────────── */

static uint32_t cp_at(const tui_lineedit *le, size_t pos, int *bytes) {
    uint32_t cp = 0;
    int k = tui_utf8_decode(le->buf + pos, le->len - pos, &cp);
    if (bytes) *bytes = k;
    return cp;
}

/* One character forward: the code point and the marks that follow it. */
static size_t next_pos(const tui_lineedit *le, size_t pos) {
    if (pos >= le->len) return le->len;
    int k;
    cp_at(le, pos, &k);
    pos += (size_t)k;
    while (pos < le->len && tui_cp_width(cp_at(le, pos, &k)) == 0) pos += (size_t)k;
    return pos;
}

/* Start of the code point before pos. */
static size_t prev_cp(const tui_lineedit *le, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && ((unsigned char)le->buf[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

/* One character back: over marks to their base. */
static size_t prev_pos(const tui_lineedit *le, size_t pos) {
    pos = prev_cp(le, pos);
    while (pos > 0 && tui_cp_width(cp_at(le, pos, NULL)) == 0) pos = prev_cp(le, pos);
    return pos;
}

static int is_space_at(const tui_lineedit *le, size_t pos) {
    return le->buf[pos] == ' ';
}

static size_t word_left(const tui_lineedit *le, size_t pos) {
    while (pos > 0 && is_space_at(le, prev_cp(le, pos))) pos = prev_cp(le, pos);
    while (pos > 0 && !is_space_at(le, prev_cp(le, pos))) pos = prev_pos(le, pos);
    return pos;
}

static size_t word_right(const tui_lineedit *le, size_t pos) {
    while (pos < le->len && is_space_at(le, pos)) pos++;
    while (pos < le->len && !is_space_at(le, pos)) pos = next_pos(le, pos);
    return pos;
}

/* ── Editing primitives ──────────────────────────────────────────────── */

static void remove_range(tui_lineedit *le, size_t a, size_t b) {
    if (b <= a) return;
    memmove(le->buf + a, le->buf + b, le->len - b + 1);
    le->len -= b - a;
    if (le->cur > b) le->cur -= b - a;
    else if (le->cur > a) le->cur = a;
}

static void kill_range(tui_lineedit *le, size_t a, size_t b) {
    if (b <= a) return;
    char *k = malloc(b - a + 1);
    if (k) {
        memcpy(k, le->buf + a, b - a);
        k[b - a] = '\0';
        free(le->kill);
        le->kill = k;
    }
    remove_range(le, a, b);
}

void tui_lineedit_init(tui_lineedit *le, char *buf, size_t size, const char *initial) {
    if (!le) return;
    memset(le, 0, sizeof(*le));
    le->buf = buf;
    le->size = buf ? size : 0;
    le->hist_pos = -1;
    if (le->size > 0) {
        buf[0] = '\0';
        if (initial) tui_lineedit_insert(le, initial);
    }
}

void tui_lineedit_free(tui_lineedit *le) {
    if (!le) return;
    free(le->hist_saved);
    free(le->kill);
    le->hist_saved = NULL;
    le->kill = NULL;
}

int tui_lineedit_insert(tui_lineedit *le, const char *utf8) {
    if (!le || !utf8 || le->size == 0) return 0;
    int any = 0;
    size_t n = strlen(utf8);
    size_t i = 0;
    while (i < n) {
        uint32_t cp;
        int k = tui_utf8_decode(utf8 + i, n - i, &cp);
        const char *src = utf8 + i;
        char enc[4];
        size_t elen = (size_t)k;
        i += (size_t)k;
        if (cp == '\t' || cp == '\n' || cp == '\r') {
            cp = ' ';
        } else if (tui_cp_width(cp) < 0) {
            continue;                       /* a control: never in the text */
        }
        if (cp == ' ' || cp == TUI_CP_REPLACEMENT) {
            elen = (size_t)tui_utf8_encode(cp, enc);
            src = enc;
        }
        if (le->len + elen + 1 > le->size) break;
        memmove(le->buf + le->cur + elen, le->buf + le->cur, le->len - le->cur + 1);
        memcpy(le->buf + le->cur, src, elen);
        le->cur += elen;
        le->len += elen;
        any = 1;
    }
    return any;
}

/* The start of the character containing byte @p pos. */
static size_t boundary(const tui_lineedit *le, size_t pos) {
    if (pos > le->len) pos = le->len;
    while (pos > 0 && pos < le->len && ((unsigned char)le->buf[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

void tui_lineedit_replace(tui_lineedit *le, size_t start, size_t end, const char *utf8) {
    if (!le || le->size == 0) return;
    start = boundary(le, start);
    end = boundary(le, end);
    if (end < start) end = start;
    remove_range(le, start, end);
    le->cur = start;
    if (utf8) tui_lineedit_insert(le, utf8);
}

void tui_lineedit_set_text(tui_lineedit *le, const char *text) {
    if (!le || le->size == 0) return;
    le->len = le->cur = 0;
    le->buf[0] = '\0';
    le->scroll = 0;
    if (text) tui_lineedit_insert(le, text);
}

static void history_move(tui_lineedit *le, int dir) {
    tui_history *h = le->history;
    if (!h || h->count == 0) return;
    int pos = le->hist_pos < 0 ? h->count : le->hist_pos;
    int to = pos + dir;
    if (to < 0 || to > h->count) return;
    if (le->hist_pos < 0) {
        /* Leaving the line being edited: keep it to come back to. */
        free(le->hist_saved);
        le->hist_saved = malloc(le->len + 1);
        if (le->hist_saved) memcpy(le->hist_saved, le->buf, le->len + 1);
    }
    if (to == h->count) {
        tui_lineedit_set_text(le, le->hist_saved ? le->hist_saved : "");
        free(le->hist_saved);
        le->hist_saved = NULL;
        le->hist_pos = -1;
    } else {
        tui_lineedit_set_text(le, h->items[to]);
        le->hist_pos = to;
    }
}

static int is_char(const tui_event *ev, uint32_t c, unsigned mods) {
    return ev->key == TUI_KEY_CHAR && ev->cp == c && ev->mods == mods;
}

tui_edit_result tui_lineedit_handle(tui_lineedit *le, const tui_event *ev) {
    if (!le || !ev || le->size == 0) return TUI_EDIT_IGNORED;
    if (ev->type == TUI_EV_PASTE) {
        char *tmp = malloc(ev->paste_len + 1);
        if (!tmp) return TUI_EDIT_IGNORED;
        memcpy(tmp, ev->paste, ev->paste_len);
        tmp[ev->paste_len] = '\0';
        tui_lineedit_insert(le, tmp);
        free(tmp);
        return TUI_EDIT_CHANGED;
    }
    if (ev->type != TUI_EV_KEY) return TUI_EDIT_IGNORED;

    const unsigned C = TUI_MOD_CTRL, A = TUI_MOD_ALT;
    if (ev->key == TUI_KEY_CHAR && ev->mods == 0) {
        tui_lineedit_insert(le, ev->utf8);
        return TUI_EDIT_CHANGED;
    }
    if (ev->key == TUI_KEY_CHAR && ev->mods == TUI_MOD_SHIFT && ev->utf8[0]) {
        tui_lineedit_insert(le, ev->utf8);
        return TUI_EDIT_CHANGED;
    }
    if (ev->key == TUI_KEY_ENTER) return TUI_EDIT_ACCEPT;
    if (ev->key == TUI_KEY_ESC || is_char(ev, 'c', C)) return TUI_EDIT_CANCEL;

    if (ev->key == TUI_KEY_LEFT && ev->mods == 0) le->cur = prev_pos(le, le->cur);
    else if (ev->key == TUI_KEY_RIGHT && ev->mods == 0) le->cur = next_pos(le, le->cur);
    else if (is_char(ev, 'b', C)) le->cur = prev_pos(le, le->cur);
    else if (is_char(ev, 'f', C)) le->cur = next_pos(le, le->cur);
    else if ((ev->key == TUI_KEY_LEFT && (ev->mods & (C | A))) || is_char(ev, 'b', A))
        le->cur = word_left(le, le->cur);
    else if ((ev->key == TUI_KEY_RIGHT && (ev->mods & (C | A))) || is_char(ev, 'f', A))
        le->cur = word_right(le, le->cur);
    else if (ev->key == TUI_KEY_HOME || is_char(ev, 'a', C)) le->cur = 0;
    else if (ev->key == TUI_KEY_END || is_char(ev, 'e', C)) le->cur = le->len;
    else if ((ev->key == TUI_KEY_BACKSPACE && ev->mods == 0) || is_char(ev, 'h', C))
        remove_range(le, prev_pos(le, le->cur), le->cur);
    else if (ev->key == TUI_KEY_BACKSPACE && (ev->mods & A))
        kill_range(le, word_left(le, le->cur), le->cur);
    else if (ev->key == TUI_KEY_DELETE && ev->mods == 0)
        remove_range(le, le->cur, next_pos(le, le->cur));
    else if (is_char(ev, 'd', C)) {
        if (le->len == 0) return TUI_EDIT_EOF;
        remove_range(le, le->cur, next_pos(le, le->cur));
    }
    else if (is_char(ev, 'k', C)) kill_range(le, le->cur, le->len);
    else if (is_char(ev, 'u', C)) kill_range(le, 0, le->cur);
    else if (is_char(ev, 'w', C)) kill_range(le, word_left(le, le->cur), le->cur);
    else if (is_char(ev, 'y', C)) {
        if (le->kill) tui_lineedit_insert(le, le->kill);
    }
    else if (ev->key == TUI_KEY_UP && ev->mods == 0) {
        if (!le->history) return TUI_EDIT_IGNORED;
        history_move(le, -1);
    }
    else if (ev->key == TUI_KEY_DOWN && ev->mods == 0) {
        if (!le->history) return TUI_EDIT_IGNORED;
        history_move(le, +1);
    }
    else if (ev->key == TUI_KEY_TAB && ev->mods == 0) {
        if (!le->tab_fn) return TUI_EDIT_IGNORED;
        le->tab_fn(le);
    }
    else if (ev->key == TUI_KEY_TAB && ev->mods == TUI_MOD_SHIFT) {
        if (!le->shift_tab_fn) return TUI_EDIT_IGNORED;
        le->shift_tab_fn(le);
    }
    else return TUI_EDIT_IGNORED;
    return TUI_EDIT_CHANGED;
}

/* ── Drawing ─────────────────────────────────────────────────────────── */

int tui_lineedit_cursor_col(const tui_lineedit *le) {
    if (!le || !le->buf) return 0;
    return tui_strn_width(le->buf, le->cur);
}

/* Adjusts le->scroll so the cursor is inside a field of @p avail columns,
 * with room for it after the last character. */
static void fit_scroll(tui_lineedit *le, int avail) {
    if (avail < 1) avail = 1;
    int c = tui_lineedit_cursor_col(le);
    int total = tui_strn_width(le->buf, le->len);
    if (c < le->scroll) le->scroll = c;
    if (c >= le->scroll + avail) le->scroll = c - avail + 1;
    /* No empty field to the right of a shortened text. */
    if (le->scroll > 0 && total - le->scroll < avail - 1) {
        le->scroll = total - avail + 1;
        if (le->scroll < 0) le->scroll = 0;
        if (c < le->scroll) le->scroll = c;
    }
}

int tui_lineedit_render(tui_lineedit *le, tui_canvas c, const char *prompt, tui_style st) {
    if (!le || !le->buf) return 0;
    tui_canvas row = tui_canvas_sub(c, tui_rect_make(0, 0, 1, tui_canvas_cols(c)));
    tui_canvas_clear(row, st);
    int pw = prompt ? tui_canvas_put(row, 0, 0, prompt, st) : 0;
    int avail = tui_canvas_cols(row) - pw;
    fit_scroll(le, avail);
    tui_canvas field = tui_canvas_sub(row, tui_rect_make(0, pw, 1, avail > 0 ? avail : 0));
    tui_canvas_put(field, 0, -le->scroll, le->buf, st);
    int col = pw + tui_lineedit_cursor_col(le) - le->scroll;
    tui_canvas_set_cursor(row, 0, col, 1);
    return col;
}

/* The bytes of the text that fall into columns [scroll, scroll + avail),
 * with a leading blank where a wide character is cut by the left edge. */
static void visible_text(const tui_lineedit *le, int avail, tui_buf *out) {
    int col = 0;
    size_t i = 0;
    while (i < le->len && col < le->scroll) {
        int k;
        uint32_t cp = cp_at(le, i, &k);
        col += tui_cp_width(cp) > 0 ? tui_cp_width(cp) : 0;
        i += (size_t)k;
    }
    while (i < le->len && tui_cp_width(cp_at(le, i, NULL)) == 0) {
        int k;
        cp_at(le, i, &k);
        i += (size_t)k;
    }
    int used = 0;
    if (col > le->scroll) {
        tui_buf_puts(out, " ");
        used = 1;
    }
    while (i < le->len) {
        int k;
        uint32_t cp = cp_at(le, i, &k);
        int w = tui_cp_width(cp);
        if (w < 0) w = 1;
        if (used + w > avail) break;
        tui_buf_append(out, le->buf + i, (size_t)k);
        used += w;
        i += (size_t)k;
    }
}

#if !defined(_WIN32)

#include <poll.h>
#include <time.h>
#include <unistd.h>

/* Reads one event, waking every 100 ms to check the interrupt flag and
 * the idle deadline.  1 = event, 0 = end of input, -1 = error,
 * -3 = interrupted, -4 = timed out. */
static int read_event(int fd, tui_decoder *d, tui_event *ev,
                      volatile sig_atomic_t *interrupt, int64_t deadline) {
    int64_t pending_since = -1;
    for (;;) {
        if (tui_decoder_next(d, ev)) return 1;
        if (interrupt && *interrupt) return -3;
        int64_t now = tui__now_ms();
        if (deadline >= 0 && now >= deadline) return -4;
        int wait = 100;
        if (tui_decoder_pending(d)) {
            if (pending_since < 0) pending_since = now;
            int64_t left = pending_since + TUI_ESC_DELAY_DEFAULT - now;
            if (left <= 0) {
                pending_since = -1;
                if (tui_decoder_timeout(d, ev)) return 1;
                continue;
            }
            if (left < wait) wait = (int)left;
        }
        if (deadline >= 0 && deadline - now < wait) wait = (int)(deadline - now);
        struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
        int r = poll(&pfd, 1, wait);
        if (r < 0) {
            if (errno == EINTR) continue;    /* e.g. SIGINT: check the flag */
            return -1;
        }
        if (r == 0) continue;
        char buf[64];
        size_t room = sizeof(d->in) - (d->in_len - d->in_pos);
        if (room > sizeof(buf)) room = sizeof(buf);
        ssize_t n = read(fd, buf, room);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n == 0 || (n < 0 && errno == EIO)) {
            /* The end of input: a key held for its ESC delay first. */
            if (tui_decoder_timeout(d, ev)) return 1;
            return 0;
        }
        if (n < 0) return -1;
        tui_decoder_feed(d, buf, (size_t)n);
        pending_since = tui__now_ms();
    }
}

int tui_lineedit_run(tui_lineedit *le, int in_fd, int out_fd, int row, int col,
                     const char *prompt) {
    if (!le || !le->buf || le->size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (row < 1) row = 1;
    if (col < 1) col = 1;
    if (!prompt) prompt = "";
    le->trow = row;
    tui_decoder d;
    tui_decoder_init(&d);
    tui_buf out;
    tui_buf_init(&out);
    int pw = tui_str_width(prompt);
    int rc;
    for (;;) {
        int avail = tui_tty_cols(out_fd) - (col - 1) - pw;
        fit_scroll(le, avail);
        tui_buf_reset(&out);
        tui_buf_printf(&out, "\033[%d;1H\033[2K\033[%d;%dH%s", row, row, col, prompt);
        visible_text(le, avail > 0 ? avail : 1, &out);
        if (tui_buf_write_fd(&out, out_fd) != 0) { rc = -1; break; }
        if (le->render_below) le->render_below(le);
        /* render_below may have moved the cursor: put it back on the row. */
        tui_buf_printf(&out, "\033[%d;%dH\033[?25h", row,
                       col + pw + tui_lineedit_cursor_col(le) - le->scroll);
        if (tui_buf_write_fd(&out, out_fd) != 0) { rc = -1; break; }

        tui_event ev;
        int r = read_event(in_fd, &d, &ev, NULL, -1);
        if (r <= 0) { rc = -1; break; }
        tui_edit_result res = tui_lineedit_handle(le, &ev);
        if (res == TUI_EDIT_ACCEPT) { rc = 1; break; }
        if (res == TUI_EDIT_CANCEL) { rc = 0; break; }
    }
    /* email-cli's ending: clear the row below (a completion list), hide
     * the cursor. */
    tui_buf_reset(&out);
    tui_buf_printf(&out, "\033[%d;1H\033[2K\033[?25l", row + 1);
    int e = errno;
    tui_buf_write_fd(&out, out_fd);
    tui_buf_free(&out);
    tui_decoder_free(&d);
    errno = e;
    return rc;
}

void tui_readline_opts_init(tui_readline_opts *o) {
    if (!o) return;
    memset(o, 0, sizeof(*o));
    o->in_fd = STDIN_FILENO;
    o->out_fd = STDOUT_FILENO;
}

/* tg-cli's non-TTY path: the prompt, then a plain line read with read(2). */
static int readline_plain(const tui_readline_opts *o, const char *prompt,
                          char *buf, size_t size) {
    if (prompt && *prompt) tui__write_all(o->out_fd, prompt, strlen(prompt));
    size_t len = 0;
    int got = 0;
    for (;;) {
        unsigned char c;
        ssize_t n = read(o->in_fd, &c, 1);
        if (n < 0 && errno == EINTR) {
            if (o->interrupt && *o->interrupt) return TUI_READLINE_INTERRUPT;
            continue;
        }
        if (n <= 0) {
            if (!got) return TUI_READLINE_EOF;
            break;
        }
        got = 1;
        if (c == '\n') break;
        if (c == '\r') continue;
        if (len + 1 < size) buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return (int)len;
}

int tui_readline(const char *prompt, char *buf, size_t size, const tui_readline_opts *o) {
    tui_readline_opts def;
    if (!o) {
        tui_readline_opts_init(&def);
        o = &def;
    }
    if (!buf || size == 0) {
        errno = EINVAL;
        return TUI_READLINE_EOF;
    }
    buf[0] = '\0';
    if (!prompt) prompt = "";
    if (!isatty(o->in_fd)) return readline_plain(o, prompt, buf, size);
    tui_tty_raw *raw = tui_tty_raw_enter(o->in_fd, o->flags & TUI_TERM_KEEP_ISIG);
    if (!raw) return readline_plain(o, prompt, buf, size);

    tui_lineedit le;
    tui_lineedit_init(&le, buf, size, NULL);
    le.history = o->history;
    tui_decoder d;
    tui_decoder_init(&d);
    tui_buf out;
    tui_buf_init(&out);
    int pw = tui_str_width(prompt);
    int rc;
    int64_t idle = o->timeout_sec > 0 ? (int64_t)o->timeout_sec * 1000 : -1;
    for (;;) {
        int avail = tui_tty_cols(o->out_fd) - pw;
        fit_scroll(&le, avail);
        tui_buf_reset(&out);
        tui_buf_printf(&out, "\r%s", prompt);
        visible_text(&le, avail > 0 ? avail : 1, &out);
        tui_buf_puts(&out, "\033[K\r");
        int cur = pw + tui_lineedit_cursor_col(&le) - le.scroll;
        if (cur > 0) tui_buf_printf(&out, "\033[%dC", cur);
        if (tui_buf_write_fd(&out, o->out_fd) != 0) { rc = TUI_READLINE_EOF; break; }

        tui_event ev;
        int r = read_event(o->in_fd, &d, &ev, o->interrupt,
                           idle >= 0 ? tui__now_ms() + idle : -1);
        if (r == -3) { rc = TUI_READLINE_INTERRUPT; break; }
        if (r == -4) { rc = TUI_READLINE_TIMEOUT; break; }
        if (r <= 0) { rc = TUI_READLINE_EOF; break; }
        if (ev.type == TUI_EV_KEY && ev.key == TUI_KEY_ESC) continue;   /* as tg-cli */
        tui_edit_result res = tui_lineedit_handle(&le, &ev);
        if (res == TUI_EDIT_ACCEPT) { rc = (int)le.len; break; }
        if (res == TUI_EDIT_CANCEL) { rc = TUI_READLINE_CANCEL; break; }
        if (res == TUI_EDIT_EOF) { rc = TUI_READLINE_EOF; break; }
    }
    int e = errno;
    tui__write_all(o->out_fd, "\r\n", 2);
    tui_tty_raw_exit(&raw);
    if (rc >= 0 && o->history) tui_history_add(o->history, buf);
    if (rc < 0) buf[0] = '\0';
    tui_buf_free(&out);
    tui_decoder_free(&d);
    tui_lineedit_free(&le);
    errno = e;
    return rc;
}

#else  /* _WIN32 */

int tui_lineedit_run(tui_lineedit *le, int in_fd, int out_fd, int row, int col,
                     const char *prompt) {
    (void)le; (void)in_fd; (void)out_fd; (void)row; (void)col; (void)prompt;
    errno = ENOTTY;
    return -1;
}

void tui_readline_opts_init(tui_readline_opts *o) {
    if (!o) return;
    memset(o, 0, sizeof(*o));
    o->out_fd = 1;
}

int tui_readline(const char *prompt, char *buf, size_t size, const tui_readline_opts *o) {
    (void)prompt; (void)o;
    if (buf && size) buf[0] = '\0';
    errno = ENOTTY;
    return TUI_READLINE_EOF;
}

#endif
