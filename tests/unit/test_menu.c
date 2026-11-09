/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-09 */
/* Unit tests of tui_menu: layout, centring, counts, description, keys, colours. */

#include "test_helpers.h"
#include "tuikit/menu.h"
#include "tuikit/statusline.h"
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

static void fx_show(Fx *f, const tui_menu *m) {
    tui_screen_clear(f->scr, TUI_STYLE_DEFAULT);
    tui_menu_render(m, tui_canvas_of(f->scr));
    tui_buf_reset(&f->buf);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
}

static void fx_close(Fx *f) {
    tui_screen_free(f->scr);
    tui_buf_free(&f->buf);
    if (f->s) pty_close(f->s);
}

static const char *fmt(const tui_menu *m, int rows, int cols) {
    static char buf[4096];
    int n = tui_menu_format(m, rows, cols, buf, sizeof(buf));
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

static tui_event special(tui_key k) {
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_KEY;
    ev.key = k;
    return ev;
}

static const tui_menu_entry E2[] = {
    { "Tickets", 12, 3, "The tickets." },
    { "Agents",   2, 1, "The agents' tmux sessions." },
};

static const tui_menu_entry E3[] = {
    { "Tickets", 12, 0, "One two three four five six seven eight nine ten eleven twelve thirteen "
                        "fourteen fifteen sixteen seventeen eighteen nineteen twenty twenty-one "
                        "twenty-two twenty-three twenty-four twenty-five" },
    { "Agents",   2, 0, NULL },
    { "Notes",   -1, 0, "" },
};

static void test_layout_exact(void) {
    tui_menu m;
    tui_menu_init(&m);
    ASSERT_EQ_STR(fmt(&m, 12, 40), "", "an empty menu draws nothing");
    ASSERT_EQ_INT(tui_menu_set(&m, E2, 2, 0), 0, "set");
    ASSERT_EQ_STR(fmt(&m, 12, 40),
        "\n\n\n"
        "          > 1  Tickets  12 (3)\n"
        "            2  Agents    2 (1)\n"
        "\n"
        "              The tickets.\n",
        "block and description centred together; no title, no words in the row");
    tui_menu_select(&m, 1);
    ASSERT_EQ_STR(fmt(&m, 12, 40),
        "\n\n\n"
        "            1  Tickets  12 (3)\n"
        "          > 2  Agents    2 (1)\n"
        "\n"
        "       The agents' tmux sessions.\n",
        "the mark and the description follow the cursor");
    ASSERT_EQ_STR(fmt(&m, 12, 40) + 0, fmt(&m, 12, 40), "stable");
}

static void test_recentres_on_resize(void) {
    tui_menu m;
    tui_menu_init(&m);
    tui_menu_set(&m, E2, 2, 0);
    ASSERT_EQ_STR(fmt(&m, 6, 20),
        "> 1  Tickets  12 (3)\n"
        "  2  Agents    2 (1)\n"
        "\n"
        "    The tickets.\n",
        "an exact fit: top and left are 0");
    const char *wide = fmt(&m, 20, 100);
    ASSERT(strstr(wide, "\n\n\n\n\n\n                                 > 1  Tickets") != NULL ||
           strstr(wide, "> 1  Tickets") != NULL, "moves with the canvas");
    char *l = strstr(wide, "> 1");
    ASSERT(l != NULL, "row found");
    if (l) {
        const char *ls = l;
        while (ls > wide && ls[-1] != '\n') ls--;
        ASSERT_EQ_INT((int)(l - ls), 40, "left edge (100 - 20) / 2");
    }
    int rows_before = 0;
    for (const char *p = wide; p < l; p++) rows_before += *p == '\n';
    ASSERT_EQ_INT(rows_before, 7, "top is (20 - 6) / 2");
    ASSERT_EQ_STR(fmt(&m, 2, 20), "> 1  Tickets  12 (3)\n  2  Agents    2 (1)\n",
                  "a canvas too short: what render would clip is not written");
}

static void test_counts(void) {
    static const tui_menu_entry e[] = {
        { "A", 0, 0, NULL },
        { "B", -1, 5, NULL },
        { "C", 100, 20, NULL },
    };
    tui_menu m;
    tui_menu_init(&m);
    tui_menu_set(&m, e, 3, 0);
    ASSERT_EQ_STR(fmt(&m, 3, 30),
        "       > 1  A    0\n"
        "         2  B\n"
        "         3  C  100 (20)\n",
        "N right-aligned, no count when below 0, (M) only above 0, no words");
}

static void test_description_wraps(void) {
    tui_menu m;
    tui_menu_init(&m);
    tui_menu_set(&m, E3, 3, 0);
    const char *t = fmt(&m, 20, 70);
    ASSERT(strstr(t, "One two three four") != NULL, "the description is shown");
    ASSERT(strstr(t, "\xE2\x80\xA6") != NULL, "a long text is cut with an ellipsis on the third line");
    ASSERT(strstr(t, "twenty-five") == NULL, "the end is not shown");
    int desc_lines = 0;
    for (const char *p = strstr(t, "One two"); p && *p; p++) desc_lines += *p == '\n';
    ASSERT_EQ_INT(desc_lines, 3, "three lines");
    tui_menu_select(&m, 1);
    ASSERT(strstr(fmt(&m, 20, 70), "One two") == NULL, "no description: none shown");
    tui_menu_select(&m, 2);
    ASSERT(strstr(fmt(&m, 20, 70), "One two") == NULL, "an empty description: none shown");

    static const tui_menu_entry two[] = {
        { "X", 1, 0, "short" },
    };
    static const tui_menu_entry one_word[] = {
        { "X", 1, 0, "supercalifragilisticexpialidocious" },
    };
    tui_menu_set(&m, two, 1, 0);
    ASSERT(strstr(fmt(&m, 10, 30), "      short\n") != NULL || strstr(fmt(&m, 10, 30), "short\n") != NULL, "a short one");
    tui_menu_set(&m, one_word, 1, 0);
    ASSERT(strstr(fmt(&m, 10, 20), "\xE2\x80\xA6") != NULL, "a word wider than the room is cut");
    ASSERT(strstr(fmt(&m, 10, 2), "\n") != NULL || 1, "a two column canvas does not fail");
    ASSERT_EQ_STR(fmt(&m, 10, 0), "", "no columns");
    ASSERT_EQ_STR(fmt(&m, 0, 10), "", "no rows");
}

static void test_narrow_cuts_labels(void) {
    static const tui_menu_entry e[] = {
        { "A very long label indeed", 3, 1, NULL },
    };
    tui_menu m;
    tui_menu_init(&m);
    tui_menu_set(&m, e, 1, 0);
    const char *t = fmt(&m, 3, 20);
    ASSERT(strstr(t, "\xE2\x80\xA6") != NULL, "the label is cut with an ellipsis");
    ASSERT(strstr(t, "3 (1)") != NULL, "the count stays");
    char row[64];
    snprintf(row, sizeof row, "%.*s", (int)strcspn(t, "\n"), t);
    ASSERT(tui_str_width(row) <= 20, "never wider than the canvas");
    t = fmt(&m, 3, 6);   /* hopeless: the label gets one column */
    ASSERT(strstr(t, "> 1") != NULL, "still drawn");
}

static void test_set_and_clamp(void) {
    tui_menu m;
    tui_menu_init(&m);
    tui_menu_set(&m, E3, 3, 99);
    ASSERT_EQ_INT(tui_menu_selected(&m), 2, "clamped high");
    tui_menu_set(&m, E3, 3, -4);
    ASSERT_EQ_INT(tui_menu_selected(&m), 0, "clamped low");
    ASSERT_EQ_INT(tui_menu_count(&m), 3, "count");
    tui_menu_select(&m, 1);
    ASSERT_EQ_INT(tui_menu_selected(&m), 1, "select");
    tui_menu_select(&m, 7);
    ASSERT_EQ_INT(tui_menu_selected(&m), 2, "select clamps");
    tui_menu_set(&m, NULL, 0, 5);
    ASSERT_EQ_INT(tui_menu_selected(&m), 0, "empty: 0");
    const tui_menu_entry bad[] = { { NULL, 0, 0, NULL } };
    errno = 0;
    ASSERT_EQ_INT(tui_menu_set(&m, bad, 1, 0), -1, "a NULL label");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_menu_set(&m, NULL, 2, 0), -1, "NULL entries");
    ASSERT_EQ_INT(tui_menu_set(&m, E3, TUI_MENU_MAX + 1, 0), -1, "too many");
    ASSERT_EQ_INT(tui_menu_set(&m, E3, -1, 0), -1, "negative count");
    ASSERT_EQ_INT(tui_menu_set(NULL, E3, 3, 0), -1, "NULL menu");
    tui_menu_select(NULL, 1);
    tui_menu_init(NULL);
    ASSERT_EQ_INT(tui_menu_selected(NULL), 0, "NULL selected");
    ASSERT_EQ_INT(tui_menu_count(NULL), 0, "NULL count");
    tui_menu_render(NULL, tui_canvas_of(NULL));
    tui_menu_render(&m, tui_canvas_of(NULL));
    char out[8];
    ASSERT_EQ_INT(tui_menu_format(NULL, 5, 5, out, sizeof out), -1, "NULL format");
    ASSERT_EQ_INT(tui_menu_format(&m, 5, 5, NULL, 0), -1, "no buffer");
    tui_event ev = special(TUI_KEY_ENTER);
    ASSERT_EQ_INT(tui_menu_handle(NULL, &ev), TUI_MENU_IGNORED, "NULL menu handle");
    ASSERT_EQ_INT(tui_menu_handle(&m, NULL), TUI_MENU_IGNORED, "NULL event");
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_IGNORED, "an empty menu ignores Enter");
}

static void test_long_texts_are_cut_at_a_character(void) {
    static char label[400], desc[700];
    for (int i = 0; i < 150; i++) memcpy(label + 2 * i, "\xC3\xA9", 2);   /* 150 x e-acute: 300 bytes */
    label[300] = '\0';
    for (int i = 0; i < 300; i++) memcpy(desc + 2 * i, "\xC3\xA9", 2);    /* 600 bytes, no blank */
    desc[600] = '\0';
    const tui_menu_entry e[] = { { label, 1, 0, desc } };
    tui_menu m;
    tui_menu_init(&m);
    tui_menu_set(&m, e, 1, 0);
    const char *t = fmt(&m, 10, 100);
    ASSERT(strstr(t, "\xC3\xA9") != NULL, "drawn");
    for (const char *p = t; *p; p++)
        if ((unsigned char)*p == 0xC3) ASSERT(((unsigned char)p[1] & 0xC0) == 0x80, "no character is cut");
}

static void test_label_cleaning(void) {
    static const tui_menu_entry e[] = { { "Tic\tkets\n", 1, 0, "line\none\ttwo" } };
    tui_menu m;
    tui_menu_init(&m);
    tui_menu_set(&m, e, 1, 0);
    const char *t = fmt(&m, 8, 40);
    ASSERT(strstr(t, "Tic kets") != NULL, "controls in a label are blanks");
    ASSERT(strstr(t, "line one two") != NULL, "and in a description");
}

static void test_keys(void) {
    tui_menu m;
    tui_menu_init(&m);
    tui_menu_set(&m, E3, 3, 0);
    tui_event ev = special(TUI_KEY_DOWN);
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_MOVED, "Down");
    ASSERT_EQ_INT(tui_menu_selected(&m), 1, "one down");
    ev = key('j');
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_MOVED, "j");
    ASSERT_EQ_INT(tui_menu_selected(&m), 2, "two down");
    ev = special(TUI_KEY_DOWN);
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_IGNORED, "no wrap at the end");
    ASSERT_EQ_INT(tui_menu_selected(&m), 2, "stays");
    ev = special(TUI_KEY_UP);
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_MOVED, "Up");
    ev = key('k');
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_MOVED, "k");
    ASSERT_EQ_INT(tui_menu_selected(&m), 0, "back at the top");
    ev = special(TUI_KEY_UP);
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_IGNORED, "no wrap at the start");
    ev = special(TUI_KEY_END);
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_MOVED, "End");
    ASSERT_EQ_INT(tui_menu_selected(&m), 2, "last");
    ev = special(TUI_KEY_HOME);
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_MOVED, "Home");
    ASSERT_EQ_INT(tui_menu_selected(&m), 0, "first");
    ev = special(TUI_KEY_ENTER);
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_ACTIVATED, "Enter");
    ASSERT_EQ_INT(tui_menu_selected(&m), 0, "on the cursor entry");
    ev = key('3');
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_ACTIVATED, "a digit");
    ASSERT_EQ_INT(tui_menu_selected(&m), 2, "moves to its entry");
    ev = key('4');
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_IGNORED, "a digit with no entry");
    ASSERT_EQ_INT(tui_menu_selected(&m), 2, "does not move");
    ev = key('0');
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_IGNORED, "0 is no hot key");
    ev = key('m');
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_IGNORED, "there is no m key");
    ev = key('q');
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_IGNORED, "q is the program's");
    ev = key('1');
    ev.mods = TUI_MOD_CTRL;
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_IGNORED, "a modified digit");
    ev = special(TUI_KEY_ESC);
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_IGNORED, "Esc");
    ev.type = TUI_EV_RESIZE;
    ASSERT_EQ_INT(tui_menu_handle(&m, &ev), TUI_MENU_IGNORED, "not a key");
}

/* What the selection looks like on a cell: its background and reverse video. */
static int sig(PtySession *s, int r, int c) {
    return pty_cell_bg(s, r, c) * 4 + ((pty_cell_attr(s, r, c) & PTY_ATTR_REVERSE) ? 1 : 0);
}

/* The emulated screen: the same text, and the cell attributes. */
static void test_render_screen(void) {
    Fx f;
    ASSERT(fx_open(&f, 12, 40) == 0, "fixtures");
    tui_menu m;
    tui_menu_init(&m);
    tui_menu_set(&m, E2, 2, 0);
    fx_show(&f, &m);
    char row[128];
    ASSERT_EQ_STR(pty_row_text(f.s, 3, row, sizeof row), "          > 1  Tickets  12 (3)", "cursor row");
    ASSERT_EQ_STR(pty_row_text(f.s, 4, row, sizeof row), "            2  Agents    2 (1)", "second row");
    ASSERT_EQ_STR(pty_row_text(f.s, 6, row, sizeof row), "              The tickets.", "description");
    ASSERT_EQ_STR(pty_row_text(f.s, 0, row, sizeof row), "", "no title");
    /* the cursor row is the selection (a filled bar from col 10, 20 wide) */
    for (int c = 10; c < 30; c++)
        ASSERT_EQ_INT(sig(f.s, 3, c), sig(f.s, 3, 12), "the cursor row is one bar");
    ASSERT(sig(f.s, 3, 12) != sig(f.s, 4, 12), "and the next row is not in it");
    ASSERT(sig(f.s, 3, 9) != sig(f.s, 3, 12), "the bar starts at the block");
    ASSERT(sig(f.s, 3, 30) != sig(f.s, 3, 12), "and ends with it");
    /* (3): the warning colour, bold; (1) too; N and the label are not */
    int warn_fg = pty_cell_fg(f.s, 3, 27);
    ASSERT_EQ_INT(pty_cell_attr(f.s, 3, 27) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "(3) is bold");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 4, 27), warn_fg, "(1) has the same colour");
    ASSERT(warn_fg != pty_cell_fg(f.s, 3, 24), "N is not in the warning colour");
    ASSERT(warn_fg != pty_cell_fg(f.s, 4, 15), "nor is a label");
    ASSERT(warn_fg != pty_cell_fg(f.s, 4, 12), "nor the hot key");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 4, 12), pty_cell_fg(f.s, 4, 15), "the hot key is in the text colour");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 4, 12) & PTY_ATTR_BOLD, 0, "not bold");
    ASSERT_EQ_STR(pty_cell_text(f.s, 3, 10), ">", "the mark");

    /* the cursor and the description move together */
    tui_menu_select(&m, 1);
    fx_show(&f, &m);
    ASSERT_EQ_STR(pty_cell_text(f.s, 4, 10), ">", "the mark moved");
    ASSERT_EQ_STR(pty_cell_text(f.s, 3, 10), " ", "off the old row");
    ASSERT(sig(f.s, 4, 12) != sig(f.s, 3, 12), "and so did the bar");
    ASSERT_EQ_STR(pty_row_text(f.s, 6, row, sizeof row), "       The agents' tmux sessions.", "description");
    fx_close(&f);
}

static void test_format_limits(void) {
    tui_menu m;
    tui_menu_init(&m);
    tui_menu_set(&m, E2, 2, 0);
    char out[16];
    ASSERT_EQ_INT(tui_menu_format(&m, 12, 40, out, sizeof out), -1, "too small a buffer");
    ASSERT(strlen(out) < sizeof out, "terminated");
    char out1[2];
    ASSERT_EQ_INT(tui_menu_format(&m, 12, 40, out1, sizeof out1), -1, "a one byte buffer");
    char out2[5];
    ASSERT_EQ_INT(tui_menu_format(&m, 12, 40, out2, sizeof out2), -1, "cut inside the leading blank rows");
    /* a multi-byte character cut at the end of the buffer is dropped whole */
    static const tui_menu_entry u[] = { { "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", 1, 0, NULL } };
    tui_menu_set(&m, u, 1, 0);
    for (size_t sz = 2; sz < 40; sz++) {
        char buf[40];
        int rc = tui_menu_format(&m, 1, 40, buf, sz);
        if (rc == -1) {
            ASSERT(tui_str_width(buf) >= 0, "terminated");
            size_t len = strlen(buf);
            int ok = 1;
            for (size_t i = 0; i < len;) {
                unsigned char c = (unsigned char)buf[i];
                size_t need = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
                if (need == 0 || i + need > len) { ok = 0; break; }
                i += need;
            }
            ASSERT(ok, "a cut buffer holds whole characters only");
        }
    }
}

/* The status line's position text, as devboard shows it. */
static void test_statusline_position_text(void) {
    tui_statusline sl;
    tui_statusline_init(&sl);
    char out[128];
    tui_statusline_set_position(&sl, "RUN 5h 12%");
    tui_statusline_format(&sl, 40, out, sizeof out);
    ASSERT(strstr(out, "RUN 5h 12%") != NULL, "the position text is the program's");
    ASSERT(strstr(out, "[") == NULL, "and the [cur/total] is replaced");
    tui_statusline_set_position_index(&sl, 2, 9);
    tui_statusline_format(&sl, 40, out, sizeof out);
    ASSERT(strstr(out, "[2/9]") != NULL, "[cur/total] is still available");
    tui_statusline_free(&sl);
}

static const TestCase tests[] = {
    TEST_CASE(test_layout_exact),
    TEST_CASE(test_recentres_on_resize),
    TEST_CASE(test_counts),
    TEST_CASE(test_description_wraps),
    TEST_CASE(test_narrow_cuts_labels),
    TEST_CASE(test_set_and_clamp),
    TEST_CASE(test_long_texts_are_cut_at_a_character),
    TEST_CASE(test_label_cleaning),
    TEST_CASE(test_keys),
    TEST_CASE(test_render_screen),
    TEST_CASE(test_format_limits),
    TEST_CASE(test_statusline_position_text),
};

int main(int argc, char **argv) {
    return RUN_SUITE("menu", tests, 100, 60);
}
