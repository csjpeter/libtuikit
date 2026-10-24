/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-24 */
/* Unit tests of tui_tabrow: layout, scrolling, active tab by attribute, keys. */

#include "test_helpers.h"
#include "tuikit/tabrow.h"
#include "tuikit/text.h"

#include <errno.h>
#include <stdlib.h>

int g_tests_run = 0;
int g_tests_failed = 0;

typedef struct {
    tui_screen *scr;
    tui_buf     buf;
    PtySession *s;
} Fx;

static int fx_open(Fx *f, int rows, int cols) {
    f->scr = tui_screen_new(rows, cols);
    tui_buf_init(&f->buf);
    f->s = pty_open(cols, rows);
    return (f->scr && f->s) ? 0 : -1;
}

static void fx_show(Fx *f, tui_tabrow *tr) {
    tui_screen_clear(f->scr, TUI_STYLE_DEFAULT);
    tui_tabrow_render(tr, tui_canvas_of(f->scr));
    tui_buf_reset(&f->buf);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
}

static void fx_close(Fx *f) {
    tui_screen_free(f->scr);
    tui_buf_free(&f->buf);
    if (f->s) pty_close(f->s);
}

static const char *fmt(const tui_tabrow *tr, int cols) {
    static char buf[1024];
    int n = tui_tabrow_format(tr, cols, buf, sizeof(buf));
    if (n < 0) { g_tests_failed++; printf("FAIL: format did not fit\n"); }
    return buf;
}

static tui_event key(uint32_t cp) {
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_KEY;
    ev.key = TUI_KEY_CHAR;
    ev.cp = cp;
    return ev;
}

static tui_event special(tui_key k, unsigned mods) {
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_KEY;
    ev.key = k;
    ev.mods = mods;
    return ev;
}

static const char *const L3[] = { "One", "Two", "Three" };
static const char *const L9[] = { "alpha", "beta", "gamma", "delta", "epsilon",
                                  "zeta", "eta", "theta", "iota" };

static void test_fits(void) {
    tui_tabrow tr;
    tui_tabrow_init(&tr);
    ASSERT_EQ_STR(fmt(&tr, 40), "", "an empty row");
    ASSERT_EQ_INT(tui_tabrow_set(&tr, L3, 3, 1), 0, "set");
    ASSERT_EQ_INT(tui_tabrow_count(&tr), 3, "count");
    ASSERT_EQ_INT(tui_tabrow_active(&tr), 1, "active");
    ASSERT_EQ_STR(fmt(&tr, 40), " One   Two   Three", "a blank each side, one between");
    ASSERT_EQ_STR(fmt(&tr, 19), " One   Two   Three", "exactly fitting: no marks");
    ASSERT_EQ_STR(fmt(&tr, 0), "", "no columns");
}

static void test_set_and_clamp(void) {
    tui_tabrow tr;
    tui_tabrow_init(&tr);
    tui_tabrow_set(&tr, L3, 3, 99);
    ASSERT_EQ_INT(tui_tabrow_active(&tr), 2, "clamped high");
    tui_tabrow_set(&tr, L3, 3, -4);
    ASSERT_EQ_INT(tui_tabrow_active(&tr), 0, "clamped low");
    tui_tabrow_set_active(&tr, 1);
    ASSERT_EQ_INT(tui_tabrow_active(&tr), 1, "set_active");
    tui_tabrow_set_active(&tr, 7);
    ASSERT_EQ_INT(tui_tabrow_active(&tr), 2, "set_active clamps");
    tui_tabrow_set(&tr, NULL, 0, 5);
    ASSERT_EQ_INT(tui_tabrow_active(&tr), 0, "empty: 0");
    const char *const bad[] = { "a", NULL };
    errno = 0;
    ASSERT_EQ_INT(tui_tabrow_set(&tr, bad, 2, 0), -1, "a NULL label");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_tabrow_set(&tr, NULL, 2, 0), -1, "NULL labels");
    ASSERT_EQ_INT(tui_tabrow_set(&tr, L3, TUI_TABROW_MAX + 1, 0), -1, "too many");
    ASSERT_EQ_INT(tui_tabrow_set(&tr, L3, -1, 0), -1, "negative count");
    ASSERT_EQ_INT(tui_tabrow_set(NULL, L3, 3, 0), -1, "NULL row");
    tui_tabrow_set_active(NULL, 1);
    tui_tabrow_init(NULL);
    ASSERT_EQ_INT(tui_tabrow_active(NULL), 0, "NULL active");
    ASSERT_EQ_INT(tui_tabrow_count(NULL), 0, "NULL count");
    tui_tabrow_render(NULL, tui_canvas_of(NULL));
    char out[8];
    ASSERT_EQ_INT(tui_tabrow_format(NULL, 10, out, sizeof out), -1, "NULL format");
    ASSERT_EQ_INT(tui_tabrow_format(&tr, 10, NULL, 0), -1, "no buffer");
}

static void test_scrolls_to_active(void) {
    tui_tabrow tr;
    tui_tabrow_init(&tr);
    tui_tabrow_set(&tr, L9, 9, 0);
    /* Whole row is 9 tabs: wider than 40 columns. */
    const char *t = fmt(&tr, 40);
    ASSERT(strstr(t, " alpha ") != NULL, "the first tab starts the window");
    ASSERT(strstr(t, "\xE2\x80\xBA") != NULL, "tabs hidden on the right: a mark");
    ASSERT(strstr(t, "\xE2\x80\xB9") == NULL, "none hidden on the left: no mark");
    ASSERT_EQ_INT(tui_str_width(t), 40, "the mark is in the last column");

    for (int a = 0; a < 9; a++) {
        tui_tabrow_set_active(&tr, a);
        char buf[1024];
        tui_tabrow_format(&tr, 40, buf, sizeof buf);
        char needle[32];
        snprintf(needle, sizeof needle, a == 8 ? " %s" : " %s ", L9[a]);   /* the last blank is trimmed */
        ASSERT(strstr(buf, needle) != NULL, "the active tab is always whole on the row");
        ASSERT(tui_str_width(buf) <= 40, "never wider than the row");
    }
    tui_tabrow_set_active(&tr, 8);
    t = fmt(&tr, 40);
    ASSERT(strstr(t, "\xE2\x80\xB9") != NULL, "last active: a mark on the left");
    ASSERT(strstr(t, "\xE2\x80\xBA") == NULL, "and none on the right");
    ASSERT(strstr(t, " iota") != NULL, "iota is shown");

    /* Every active tab at every width from 14: whole, and within the row. */
    for (int cols = 14; cols <= 70; cols += 7) {
        for (int a = 0; a < 9; a++) {
            tui_tabrow_set_active(&tr, a);
            char buf[1024], needle[32];
            tui_tabrow_format(&tr, cols, buf, sizeof buf);
            snprintf(needle, sizeof needle, " %s", L9[a]);
            ASSERT(strstr(buf, needle) != NULL, "the active tab is shown at every width");
            ASSERT(tui_str_width(buf) <= cols, "and the row is never wider");
        }
    }
}

static void test_scroll_is_stable_and_returns(void) {
    Fx f;
    ASSERT(fx_open(&f, 2, 30) == 0, "fixtures");
    tui_tabrow tr;
    tui_tabrow_init(&tr);
    tui_tabrow_set(&tr, L9, 9, 0);
    fx_show(&f, &tr);
    ASSERT_EQ_INT(tr.first, 0, "window at the start");
    tui_tabrow_set_active(&tr, 4);
    fx_show(&f, &tr);
    int first = tr.first;
    ASSERT(first > 0, "the window moved to show tab 4");
    tui_tabrow_set_active(&tr, 3);
    fx_show(&f, &tr);
    ASSERT(tr.first <= first, "a step back inside the window does not move it more than needed");
    tui_tabrow_set_active(&tr, 0);
    fx_show(&f, &tr);
    ASSERT_EQ_INT(tr.first, 0, "back to the first tab: the window returns");
    fx_close(&f);
}

static void test_narrow(void) {
    tui_tabrow tr;
    tui_tabrow_init(&tr);
    tui_tabrow_set(&tr, L9, 9, 4);
    ASSERT_EQ_STR(fmt(&tr, 2), "", "no room for a window");
    ASSERT_EQ_STR(fmt(&tr, 1), "", "one column");
    /* " epsilon " is 9 wide: a window of 5 cuts it with an ellipsis. */
    const char *t = fmt(&tr, 7);
    ASSERT(strstr(t, "\xE2\x80\xA6") != NULL, "the active tab alone wider than the row is cut");
    ASSERT_EQ_INT(tui_str_width(t), 7, "marks at both ends, the tab between");
}

static void test_label_cleaning(void) {
    const char *const lab[] = { "a\tb", "\x01x" };
    tui_tabrow tr;
    tui_tabrow_init(&tr);
    tui_tabrow_set(&tr, lab, 2, 0);
    ASSERT_EQ_STR(fmt(&tr, 40), " a b    x", "controls are blanks");
    char long_label[400];
    memset(long_label, 'x', sizeof long_label - 1);
    long_label[sizeof long_label - 1] = '\0';
    long_label[255] = '\xC3';   /* a character that the limit would cut in two */
    memcpy(long_label + 254, "\xC3\xA9", 2);
    const char *const big[] = { long_label };
    tui_tabrow_set(&tr, big, 1, 0);
    ASSERT(tui_str_width(fmt(&tr, 600)) <= 258, "a long label is cut, at a character");
}

static void test_keys(void) {
    tui_tabrow tr;
    tui_tabrow_init(&tr);
    tui_event tab = special(TUI_KEY_TAB, 0), back = special(TUI_KEY_TAB, TUI_MOD_SHIFT);
    ASSERT_EQ_INT(tui_tabrow_handle(&tr, &tab), TUI_TABROW_IGNORED, "no tabs");
    tui_tabrow_set(&tr, L3, 3, 0);
    ASSERT_EQ_INT(tui_tabrow_handle(&tr, &tab), TUI_TABROW_CHANGED, "Tab");
    ASSERT_EQ_INT(tui_tabrow_active(&tr), 1, "next");
    tui_tabrow_handle(&tr, &tab);
    tui_tabrow_handle(&tr, &tab);
    ASSERT_EQ_INT(tui_tabrow_active(&tr), 0, "wraps forward");
    ASSERT_EQ_INT(tui_tabrow_handle(&tr, &back), TUI_TABROW_CHANGED, "Shift-Tab");
    ASSERT_EQ_INT(tui_tabrow_active(&tr), 2, "wraps back");
    tui_event d2 = key('2'), d9 = key('9'), d0 = key('0'), x = key('x');
    ASSERT_EQ_INT(tui_tabrow_handle(&tr, &d2), TUI_TABROW_CHANGED, "digit");
    ASSERT_EQ_INT(tui_tabrow_active(&tr), 1, "2 jumps to the second tab");
    ASSERT_EQ_INT(tui_tabrow_handle(&tr, &d2), TUI_TABROW_IGNORED, "already there");
    ASSERT_EQ_INT(tui_tabrow_handle(&tr, &d9), TUI_TABROW_IGNORED, "no ninth tab");
    ASSERT_EQ_INT(tui_tabrow_handle(&tr, &d0), TUI_TABROW_IGNORED, "0 is not a tab");
    ASSERT_EQ_INT(tui_tabrow_handle(&tr, &x), TUI_TABROW_IGNORED, "a letter");
    tui_event ctrl = key('1');
    ctrl.mods = TUI_MOD_CTRL;
    ASSERT_EQ_INT(tui_tabrow_handle(&tr, &ctrl), TUI_TABROW_IGNORED, "Ctrl-1 is the program's");
    tui_event enter = special(TUI_KEY_ENTER, 0), rz;
    memset(&rz, 0, sizeof rz);
    rz.type = TUI_EV_RESIZE;
    ASSERT_EQ_INT(tui_tabrow_handle(&tr, &enter), TUI_TABROW_IGNORED, "another key");
    ASSERT_EQ_INT(tui_tabrow_handle(&tr, &rz), TUI_TABROW_IGNORED, "not a key");
    ASSERT_EQ_INT(tui_tabrow_handle(NULL, &tab), TUI_TABROW_IGNORED, "NULL row");
    ASSERT_EQ_INT(tui_tabrow_handle(&tr, NULL), TUI_TABROW_IGNORED, "NULL event");
    tui_tabrow_set(&tr, L3, 1, 0);
    ASSERT_EQ_INT(tui_tabrow_handle(&tr, &tab), TUI_TABROW_IGNORED, "one tab: Tab stays");
}

/* The picture: the active tab is bold and underlined, the others are not. */
static void test_render_attributes(void) {
    Fx f;
    ASSERT(fx_open(&f, 3, 40) == 0, "fixtures");
    tui_tabrow tr;
    tui_tabrow_init(&tr);
    tui_tabrow_set(&tr, L3, 3, 1);
    fx_show(&f, &tr);
    char row[128];
    ASSERT_EQ_STR(pty_row_text(f.s, 0, row, sizeof row), " One   Two   Three", "text");
    /* " One " 0-4, gap 5, " Two " 6-10, gap 11, " Three " 12-18 */
    for (int c = 6; c <= 10; c++) {
        ASSERT_EQ_INT(pty_cell_attr(f.s, 0, c) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "active: bold");
        ASSERT_EQ_INT(pty_cell_attr(f.s, 0, c) & PTY_ATTR_UNDERLINE, PTY_ATTR_UNDERLINE, "active: underlined");
    }
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 7), 36, "active: the tab.active colour (cyan)");
    for (int c = 0; c <= 4; c++)
        ASSERT_EQ_INT(pty_cell_attr(f.s, 0, c) & (PTY_ATTR_BOLD | PTY_ATTR_UNDERLINE), 0,
                      "inactive: neither bold nor underlined");
    for (int c = 12; c <= 18; c++)
        ASSERT_EQ_INT(pty_cell_attr(f.s, 0, c) & (PTY_ATTR_BOLD | PTY_ATTR_UNDERLINE), 0, "the others too");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 1, 7) & PTY_ATTR_UNDERLINE, 0, "the next row is untouched");

    tui_tabrow_set_active(&tr, 2);
    fx_show(&f, &tr);
    ASSERT_EQ_INT(pty_cell_attr(f.s, 0, 14) & PTY_ATTR_UNDERLINE, PTY_ATTR_UNDERLINE, "the mark moves");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 0, 7) & PTY_ATTR_UNDERLINE, 0, "off the old one");
    fx_close(&f);
}

static void test_render_overflow_picture(void) {
    Fx f;
    ASSERT(fx_open(&f, 2, 30) == 0, "fixtures");
    tui_tabrow tr;
    tui_tabrow_init(&tr);
    tui_tabrow_set(&tr, L9, 9, 5);
    fx_show(&f, &tr);
    char row[256];
    pty_row_text(f.s, 0, row, sizeof row);
    ASSERT(strstr(row, " zeta ") != NULL, "the active tab is on the row");
    ASSERT(strncmp(row, "\xE2\x80\xB9", 3) == 0, "a mark in column 0");
    int found = 0;
    for (int c = 0; c < 30; c++)
        if ((pty_cell_attr(f.s, 0, c) & PTY_ATTR_UNDERLINE) && strcmp(pty_cell_text(f.s, 0, c), "z") == 0)
            found = 1;
    ASSERT(found, "and it is underlined there");
    tui_tabrow_render(&tr, tui_canvas_of(f.scr));
    tui_canvas zero = tui_canvas_sub(tui_canvas_of(f.scr), tui_rect_make(0, 0, 0, 0));
    tui_tabrow_render(&tr, zero);
    fx_close(&f);
}

static void test_format_limits(void) {
    tui_tabrow tr;
    tui_tabrow_init(&tr);
    tui_tabrow_set(&tr, L3, 3, 0);
    char out[8];
    ASSERT_EQ_INT(tui_tabrow_format(&tr, 40, out, sizeof out), -1, "too small a buffer");
    ASSERT_EQ_STR(out, " One", "cut and terminated");
    char out2[4];
    ASSERT_EQ_INT(tui_tabrow_format(&tr, 40, out2, sizeof out2), -1, "smaller than the first blanks and text");
    tui_tabrow_set(&tr, L9, 9, 8);
    char out3[10];
    ASSERT_EQ_INT(tui_tabrow_format(&tr, 30, out3, sizeof out3), -1, "a cut inside a multi-byte mark");
    ASSERT(strlen(out3) < sizeof out3, "terminated");
    char out4[2];
    ASSERT_EQ_INT(tui_tabrow_format(&tr, 30, out4, sizeof out4), -1, "a one byte buffer");
}

static const TestCase tests[] = {
    TEST_CASE(test_fits),
    TEST_CASE(test_set_and_clamp),
    TEST_CASE(test_scrolls_to_active),
    TEST_CASE(test_scroll_is_stable_and_returns),
    TEST_CASE(test_narrow),
    TEST_CASE(test_label_cleaning),
    TEST_CASE(test_keys),
    TEST_CASE(test_render_attributes),
    TEST_CASE(test_render_overflow_picture),
    TEST_CASE(test_format_limits),
};

int main(int argc, char **argv) {
    return RUN_SUITE("tabrow", tests, 140, 60);
}
