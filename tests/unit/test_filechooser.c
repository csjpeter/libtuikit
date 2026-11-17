/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* Unit tests of tui_filechooser. */

/*
 * Unit tests of tui_filechooser: the list and its order, the field and the
 * list following each other, going into and out of directories, the three
 * modes, completion, the hidden toggle and the filter, the background read
 * (made slow on purpose), the failure paths, and what is drawn -- rendered,
 * fed to libptytest's emulator, compared cell by cell.
 */

#include "fc_tree.h"
#include "faults.h"
#include "test_helpers.h"
#include "tuikit/filechooser.h"

#include <errno.h>
#include <poll.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static char g_root[256];

static void make_tree(void) {
    setenv("TZ", "UTC", 1);
    setenv("TERM", "xterm-256color", 1);
    unsetenv("COLORTERM");
    snprintf(g_root, sizeof g_root, "%s/fc-unit-%d", TUIKIT_TEST_TMPDIR, (int)getpid());
    fc_tree_make(g_root);
}

static void drop_tree(void) {
    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf '%s/fc-unit-%d'", TUIKIT_TEST_TMPDIR, (int)getpid());
    if (system(cmd) != 0) fprintf(stderr, "cannot remove the test tree\n");
}

static tui_fc_state send(tui_filechooser *fc, const char *keys) {
    tui_decoder dec;
    tui_decoder_init(&dec);
    tui_decoder_feed(&dec, keys, strlen(keys));
    tui_event ev;
    tui_fc_state st = fc->state;
    while (tui_decoder_next(&dec, &ev)) st = tui_filechooser_handle(fc, &ev);
    if (tui_decoder_timeout(&dec, &ev)) st = tui_filechooser_handle(fc, &ev);
    tui_decoder_free(&dec);
    return st;
}

/* Waits (at most 5 s) until the read in progress has been taken in. */
static int settle(tui_filechooser *fc) {
    for (int i = 0; i < 500; i++) {
        tui_filechooser_poll(fc);
        if (!tui_filechooser_loading(fc)) return 1;
        struct pollfd pfd = { fc->notify_fd, POLLIN, 0 };
        poll(&pfd, 1, 10);
    }
    return 0;
}

static int open_at(tui_filechooser *fc, tui_fc_mode mode, const char *sub, const char *filter) {
    char dir[300];
    snprintf(dir, sizeof dir, "%s%s%s", g_root, sub ? "/" : "", sub ? sub : "");
    if (tui_filechooser_open(fc, NULL, mode, dir, filter) != 0) return -1;
    return settle(fc) ? 0 : -1;
}

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

static void fx_draw(Fx *f, tui_filechooser *fc) {
    tui_canvas all = tui_canvas_of(f->scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_filechooser_render(fc, all);
    tui_buf_reset(&f->buf);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
}

static void fx_close(Fx *f) {
    tui_screen_free(f->scr);
    tui_buf_free(&f->buf);
    if (f->s) pty_close(f->s);
}

/* The text of screen row @p r. */
static const char *row(Fx *f, int r) {
    static char bufs[4][256];
    static int n;
    char *b = bufs[n++ & 3];
    pty_row_text(f->s, r, b, 256);
    return b;
}

/* The row of the list (screen row 5 is the first) that carries the '>' marker. */
static int marked_row(Fx *f, int rows) {
    for (int r = 5; r < rows; r++)
        if (strstr(row(f, r), "│ > ")) return r;
    return -1;
}

static int has(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

/* The field scrolls to keep the cursor in view, so a long path shows its end:
 * is the last 50 columns of @p path in @p hay? */
static int has_end(const char *hay, const char *path) {
    size_t n = strlen(path);
    return strstr(hay, n > 50 ? path + n - 50 : path) != NULL;
}

/* ── The list ────────────────────────────────────────────────────────── */

static void test_list_order_and_columns(void) {
    tui_filechooser fc;
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open and read");
    char want[300];
    snprintf(want, sizeof want, "%s/", g_root);
    ASSERT_EQ_STR(tui_filechooser_dir(&fc), want, "the directory, with a trailing slash");
    ASSERT_EQ_INT(fc.state, TUI_FC_ACTIVE, "active");
    ASSERT(fc.notify_fd >= 0, "a notify descriptor");
    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 2), "Open file"), "the default title");
    snprintf(want, sizeof want, "%s/", g_root);
    ASSERT(has_end(row(&f, 3), want), "the field shows the directory");
    ASSERT(has(row(&f, 4), "6 entries"), "the count (hidden ones not counted)");
    const char *const names[] = { "../", "alpha/", "beta/", "zlink/", "a.txt", "b.eml", "C.TXT" };
    for (int i = 0; i < 7; i++) {
        char msg[64];
        snprintf(msg, sizeof msg, "row %d is %s", i, names[i]);
        ASSERT(has(row(&f, 5 + i), names[i]), msg);
    }
    ASSERT(!has(row(&f, 5), "2026"), "the '..' row has no date");
    ASSERT(has(row(&f, 6), "2026-01-02 03:04"), "a directory has its date");
    ASSERT(!has(row(&f, 6), "B"), "and no size");
    ASSERT(has(row(&f, 9), "    3B 2026-01-02 03:04"), "bytes, right-aligned in six columns");
    ASSERT(has(row(&f, 10), "  2.0K 2026-01-02 03:04"), "kilobytes with a decimal");
    ASSERT(has(row(&f, 11), "  5.0M 2026-01-02 03:04"), "megabytes");
    ASSERT(!has(row(&f, 12), "alpha"), "nothing else is listed");
    ASSERT(!has(row(&f, 5), ".dots") && !has(row(&f, 6), ".dots"), "hidden names are not listed");
    ASSERT_EQ_INT(marked_row(&f, 24), 5, "the first row is selected");
    fx_close(&f);
    tui_filechooser_free(&fc);
    ASSERT(fc.priv == NULL, "freed");
    tui_filechooser_free(&fc);
    tui_filechooser_free(NULL);
}

/* The selection is a dark background with a '>' in the first column. */
static void test_selection_colours(void) {
    tui_filechooser fc;
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open");
    send(&fc, "\x1b[B");
    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    fx_draw(&f, &fc);
    ASSERT_EQ_STR(pty_cell_text(f.s, 6, 4), ">", "the marker");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 6, 8), PTY_COLOR_IDX(17), "the row has the selection's dark blue");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 7, 8), PTY_BG_DEFAULT, "another row has the terminal's own");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 2, 6) != 0, 1, "the title is in the frame's colour");
    fx_close(&f);
    tui_filechooser_free(&fc);
}

/* ── The field and the list follow each other ────────────────────────── */

static void test_field_follows_rows(void) {
    tui_filechooser fc;
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open");
    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    char want[320];
    send(&fc, "\x1b[B");                       /* alpha/ */
    fx_draw(&f, &fc);
    snprintf(want, sizeof want, "%s/alpha/", g_root);
    ASSERT(has_end(row(&f, 3), want), "Down: the field shows the path of the row");
    ASSERT_EQ_INT(marked_row(&f, 24), 6, "and the row is marked");
    send(&fc, "\x1b[B\x1b[B\x1b[B");           /* beta, zlink, a.txt */
    fx_draw(&f, &fc);
    snprintf(want, sizeof want, "%s/a.txt", g_root);
    ASSERT(has_end(row(&f, 3), want), "a file: no trailing slash");
    send(&fc, "\x1b[F");                       /* End */
    fx_draw(&f, &fc);
    snprintf(want, sizeof want, "%s/C.TXT", g_root);
    ASSERT(has_end(row(&f, 3), want), "End: the last row");
    send(&fc, "\x1b[A");
    fx_draw(&f, &fc);
    snprintf(want, sizeof want, "%s/b.eml", g_root);
    ASSERT(has_end(row(&f, 3), want), "Up");
    send(&fc, "\x1b[H");                       /* Home */
    fx_draw(&f, &fc);
    ASSERT(!has(row(&f, 3), "fc-unit"), "its path is the parent directory");
    send(&fc, "\x1b[6~\x1b[5~");               /* PgDn, PgUp */
    fx_draw(&f, &fc);
    ASSERT_EQ_INT(marked_row(&f, 24), 5, "PgDn then PgUp are back at the top");
    fx_close(&f);
    tui_filechooser_free(&fc);
}

static void test_typing_selects_and_changes_directory(void) {
    tui_filechooser fc;
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open");
    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    send(&fc, "b");
    fx_draw(&f, &fc);
    ASSERT_EQ_INT(marked_row(&f, 24), 7, "typing 'b' selects the first name that starts with it");
    ASSERT(has(row(&f, 7), "beta/"), "beta");
    send(&fc, "\x1b[3~");                      /* Delete at the end: nothing */
    send(&fc, "zzz");
    fx_draw(&f, &fc);
    ASSERT_EQ_INT(marked_row(&f, 24), 7, "no match: the selection stays");
    send(&fc, "\x7f\x7f\x7f\x7f");             /* Backspace x4: the leaf is gone, then 'b' */
    send(&fc, "alpha/");
    ASSERT_EQ_INT(settle(&fc), 1, "the typed directory is read");
    char want[320];
    snprintf(want, sizeof want, "%s/alpha/", g_root);
    ASSERT_EQ_STR(tui_filechooser_dir(&fc), want, "typing a slash shows that directory");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 6), "inner.txt"), "its entries");
    ASSERT(has(row(&f, 4), "1 entry"), "singular");
    fx_close(&f);
    tui_filechooser_free(&fc);
}

/* ── Directories ─────────────────────────────────────────────────────── */

static void test_enter_directory_and_backspace_up(void) {
    tui_filechooser fc;
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open");
    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    send(&fc, "\x1b[B");                       /* alpha/ */
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_ACTIVE, "Enter on a directory goes into it");
    settle(&fc);
    char want[320];
    snprintf(want, sizeof want, "%s/alpha/", g_root);
    ASSERT_EQ_STR(tui_filechooser_dir(&fc), want, "now in alpha");
    fx_draw(&f, &fc);
    ASSERT(has_end(row(&f, 3), want), "the field shows it");
    ASSERT(has(row(&f, 5), "../") && has(row(&f, 6), "inner.txt"), "its list");
    send(&fc, "\x7f");                         /* Backspace: up */
    settle(&fc);
    snprintf(want, sizeof want, "%s/", g_root);
    ASSERT_EQ_STR(tui_filechooser_dir(&fc), want, "Backspace goes up");
    fx_draw(&f, &fc);
    ASSERT_EQ_INT(marked_row(&f, 24), 6, "and stands on the directory it came out of");
    ASSERT(has(row(&f, 6), "alpha/"), "alpha");
    send(&fc, "x\x7f");                        /* a typed name: Backspace deletes */
    ASSERT_EQ_STR(tui_filechooser_dir(&fc), want, "Backspace after a typed name edits it");
    send(&fc, "\x1b[A\r");                     /* the '..' row, Enter */
    settle(&fc);
    ASSERT(strcmp(tui_filechooser_dir(&fc), want) != 0, "the '..' row goes up");
    snprintf(want, sizeof want, "%s/", g_root);
    fx_close(&f);
    tui_filechooser_free(&fc);

    /* At the root there is no '..' and Backspace stays. */
    ASSERT_EQ_INT(tui_filechooser_open(&fc, "Root", TUI_FC_OPEN, "/", NULL), 0, "open at /");
    settle(&fc);
    ASSERT_EQ_STR(tui_filechooser_dir(&fc), "/", "root");
    send(&fc, "\x7f");
    ASSERT_EQ_STR(tui_filechooser_dir(&fc), "/", "Backspace at the root stays");
    tui_filechooser_free(&fc);

    /* A symbolic link to a directory is a directory. */
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open");
    send(&fc, "zlink\r");
    settle(&fc);
    ASSERT(has(tui_filechooser_dir(&fc), "zlink/"), "a link to a directory is entered");
    tui_filechooser_free(&fc);
}

static void test_dotdot_typed(void) {
    tui_filechooser fc;
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, "alpha", NULL), 0, "open");
    send(&fc, "../beta");
    settle(&fc);
    send(&fc, "\r");
    settle(&fc);
    char want[320];
    snprintf(want, sizeof want, "%s/beta/", g_root);
    ASSERT_EQ_STR(tui_filechooser_dir(&fc), want, "a typed relative path with .. is resolved");
    tui_filechooser_free(&fc);
}

/* ── The three modes ─────────────────────────────────────────────────── */

static void test_open_mode(void) {
    tui_filechooser fc;
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open");
    send(&fc, "\x1b[F\x1b[A");                 /* b.eml */
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_OK, "Enter on a file chooses it");
    char want[320];
    snprintf(want, sizeof want, "%s/b.eml", g_root);
    ASSERT_EQ_STR(fc.path, want, "the path");
    ASSERT_EQ_INT(send(&fc, "x"), TUI_FC_OK, "closed stays closed");
    tui_filechooser_free(&fc);

    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open again");
    send(&fc, "a.txt");
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_OK, "a typed name");
    snprintf(want, sizeof want, "%s/a.txt", g_root);
    ASSERT_EQ_STR(fc.path, want, "the typed file");
    tui_filechooser_free(&fc);

    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, "beta", NULL), 0, "open in beta");
    send(&fc, "../a.txt");
    ASSERT_EQ_INT(settle(&fc), 1, "settled");
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_OK, "a typed path in another directory");
    ASSERT_EQ_STR(fc.path, want, "resolved to the same file");
    tui_filechooser_free(&fc);

    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open again");
    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    send(&fc, "nothing");
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_ACTIVE, "a name that does not exist is refused");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "no such file: nothing"), "the refusal is shown");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 4, 4) != 0, 1, "in the error colour");
    send(&fc, "x");
    fx_draw(&f, &fc);
    ASSERT(!has(row(&f, 4), "no such file"), "and clears on the next key");
    send(&fc, "\x15\r");                       /* Ctrl-U, Enter on an empty field */
    ASSERT_EQ_INT(fc.state, TUI_FC_ACTIVE, "an empty field chooses nothing");
    fx_close(&f);
    tui_filechooser_free(&fc);
}

static void test_dir_mode(void) {
    tui_filechooser fc;
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_DIR, NULL, NULL), 0, "open");
    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 2), "Choose directory"), "title");
    ASSERT(has(row(&f, 5), "./"), "the first row is this directory");
    ASSERT(has(row(&f, 6), "../") && has(row(&f, 7), "alpha/"), "then '..' and the directories");
    ASSERT(!has(row(&f, 10), "a.txt") && !has(row(&f, 9), "a.txt") && !has(row(&f, 8), "a.txt"),
           "no files");
    ASSERT(has(row(&f, 4), "3 entries"), "alpha, beta, zlink");
    send(&fc, "\x1b[B\x1b[B\x1b[B");           /* alpha: the field */
    send(&fc, "\x1b[A\x1b[A\x1b[A");           /* back to "." */
    char want[320];
    snprintf(want, sizeof want, "%s/", g_root);
    fx_draw(&f, &fc);
    ASSERT(has_end(row(&f, 3), want), "'.' shows the directory itself");
    send(&fc, "\x1b[B\x1b[B\r");               /* alpha: go in */
    settle(&fc);
    snprintf(want, sizeof want, "%s/alpha", g_root);
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_OK, "Enter on the directory shown chooses it");
    ASSERT_EQ_STR(fc.path, want, "without a trailing slash");
    tui_filechooser_free(&fc);

    ASSERT_EQ_INT(open_at(&fc, TUI_FC_DIR, NULL, NULL), 0, "open again");
    send(&fc, "a.txt\r");
    ASSERT_EQ_INT(fc.state, TUI_FC_ACTIVE, "a file is not a directory");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "not a directory") || has(row(&f, 4), "no such directory"), "refused");
    send(&fc, "\x15nope\r");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "no such directory: "), "a missing directory");
    fx_close(&f);
    tui_filechooser_free(&fc);

    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_DIR, "/", NULL), 0, "root");
    settle(&fc);
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_OK, "the root can be chosen");
    ASSERT_EQ_STR(fc.path, "/", "as /");
    tui_filechooser_free(&fc);
}

static void test_save_mode(void) {
    tui_filechooser fc;
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_SAVE, NULL, NULL), 0, "open");
    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 2), "Save as"), "title");
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_ACTIVE, "the directory alone is not a name");
    send(&fc, "new.txt");
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_OK, "a new name is chosen at once");
    char want[320];
    snprintf(want, sizeof want, "%s/new.txt", g_root);
    ASSERT_EQ_STR(fc.path, want, "path");
    tui_filechooser_free(&fc);

    ASSERT_EQ_INT(open_at(&fc, TUI_FC_SAVE, NULL, NULL), 0, "open again");
    send(&fc, "a.txt");
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_ACTIVE, "an existing file asks first");
    fx_draw(&f, &fc);
    ASSERT_SCREEN_CONTAINS(f.s, "Replace file");
    ASSERT_SCREEN_CONTAINS(f.s, "a.txt exists.");
    ASSERT_SCREEN_CONTAINS(f.s, "[ Yes ]");
    ASSERT_EQ_INT(send(&fc, "x"), TUI_FC_ACTIVE, "other keys leave the question open");
    ASSERT_EQ_INT(send(&fc, "n"), TUI_FC_ACTIVE, "No goes back to the chooser");
    fx_draw(&f, &fc);
    ASSERT_SCREEN_NOT_CONTAINS(f.s, "Replace file");
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_ACTIVE, "asked again");
    ASSERT_EQ_INT(send(&fc, "\x1b"), TUI_FC_ACTIVE, "Esc on the question is No, not a cancel");
    send(&fc, "\r");
    ASSERT_EQ_INT(send(&fc, "y"), TUI_FC_OK, "Yes replaces");
    snprintf(want, sizeof want, "%s/a.txt", g_root);
    ASSERT_EQ_STR(fc.path, want, "the existing file");
    tui_filechooser_free(&fc);

    ASSERT_EQ_INT(open_at(&fc, TUI_FC_SAVE, NULL, NULL), 0, "open again");
    send(&fc, "nodir/x.txt");
    settle(&fc);
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_ACTIVE, "a directory that cannot be read is refused");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "cannot read: No such file or directory"), "with the reason");
    fx_close(&f);
    tui_filechooser_free(&fc);
}

static void test_cancel(void) {
    tui_filechooser fc;
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open");
    ASSERT_EQ_INT(send(&fc, "\x1b"), TUI_FC_CANCELLED, "Esc cancels");
    ASSERT(fc.path == NULL, "no path");
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_CANCELLED, "stays cancelled");
    tui_filechooser_free(&fc);
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open");
    ASSERT_EQ_INT(send(&fc, "\x03"), TUI_FC_CANCELLED, "Ctrl-C cancels");
    tui_filechooser_free(&fc);
}

/* ── Completion, hidden names, filter ────────────────────────────────── */

static void test_tab_completion(void) {
    tui_filechooser fc;
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open");
    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    char want[320];
    send(&fc, "a\t");
    fx_draw(&f, &fc);
    snprintf(want, sizeof want, "%s/a ", g_root);
    ASSERT(has(row(&f, 3), "/a") && !has(row(&f, 3), "a.txt") && !has(row(&f, 3), "alpha"),
           "'a' is ambiguous (alpha, a.txt): nothing is added");
    send(&fc, "l\t");
    fx_draw(&f, &fc);
    snprintf(want, sizeof want, "%s/alpha/", g_root);
    ASSERT(has_end(row(&f, 3), want), "one directory: completed, with a slash");
    settle(&fc);
    ASSERT_EQ_STR(tui_filechooser_dir(&fc), want, "and shown");
    send(&fc, "\x1b[B\x1b[A\x15");
    send(&fc, "..\x7f\x7f\x7f");
    send(&fc, "\x15");
    snprintf(want, sizeof want, "%s/", g_root);
    tui_filechooser_free(&fc);

    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open again");
    send(&fc, "b.\t");
    fx_draw(&f, &fc);
    snprintf(want, sizeof want, "%s/b.eml", g_root);
    ASSERT(has_end(row(&f, 3), want), "one file: completed whole");
    send(&fc, "\x15x\t");                      /* no match: unchanged */
    tui_filechooser_free(&fc);

    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open again");
    send(&fc, "\x15/nonexistent-dir/q\t");     /* the listing is not of this directory */
    ASSERT_EQ_INT(fc.state, TUI_FC_ACTIVE, "no completion for a directory not shown");
    tui_filechooser_free(&fc);

    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open once more");
    send(&fc, "\xc3\xa9\t");                   /* a multibyte letter that matches nothing */
    tui_filechooser_free(&fc);
    fx_close(&f);
}

static void test_hidden_toggle_and_filter(void) {
    tui_filechooser fc;
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open");
    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    send(&fc, "\x1b[B\x1b[B");                 /* beta */
    send(&fc, "\x1b" "h");                     /* Alt-H */
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "7 entries, hidden shown"), "the status says so");
    ASSERT(has(row(&f, 6), ".dots/"), "the hidden directory is in the list");
    ASSERT(has(row(&f, 7), "alpha/") && has(row(&f, 8), "beta/"), "in order");
    ASSERT_EQ_INT(marked_row(&f, 24), 8, "the selection stays on beta");
    send(&fc, "\x1b" "h");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "6 entries") && !has(row(&f, 4), "hidden"), "toggled off");
    tui_filechooser_set_hidden(&fc, 1);
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "hidden shown"), "set_hidden(1)");
    tui_filechooser_set_hidden(&fc, 0);
    tui_filechooser_free(&fc);
    tui_filechooser_set_hidden(&fc, 1);        /* after free: nothing */
    tui_filechooser_set_hidden(NULL, 1);

    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, "*.eml"), 0, "open with a filter");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "4 entries, filter *.eml"), "the status names the filter");
    ASSERT(has(row(&f, 5), "../") && has(row(&f, 9), "b.eml"), "directories and the match");
    ASSERT(!has(row(&f, 10), "a.txt") && !has(row(&f, 9), "a.txt"), "other files are not listed");
    tui_filechooser_free(&fc);
    fx_close(&f);
}

/* ── The background read ─────────────────────────────────────────────── */

static void test_slow_read_does_not_stop_the_keys(void) {
    fault_opendir_delay(400);
    tui_filechooser fc;
    char dir[300];
    snprintf(dir, sizeof dir, "%s", g_root);
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, dir, NULL), 0, "open returns at once");
    ASSERT_EQ_INT(tui_filechooser_loading(&fc), 1, "still reading");
    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "Reading..."), "the status says so");
    ASSERT(!has(row(&f, 6), "alpha"), "no entries yet");
    send(&fc, "a.txt");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 3), "a.txt"), "the typing field works meanwhile");
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_ACTIVE, "Enter waits for the list");
    ASSERT_EQ_INT(tui_filechooser_poll(&fc), 0, "nothing arrived yet");
    ASSERT_EQ_INT(settle(&fc), 1, "the read ends");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "6 entries"), "the list is there");
    ASSERT_EQ_INT(marked_row(&f, 24), 9, "the typed name is selected in it");
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_OK, "and now Enter chooses it");
    tui_filechooser_free(&fc);

    /* Moving on from a read that has not ended, and closing with one running. */
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, dir, NULL), 0, "open");
    send(&fc, "alpha/");
    ASSERT_EQ_INT(settle(&fc), 1, "the second read is the one taken in");
    ASSERT(has(tui_filechooser_dir(&fc), "alpha/"), "alpha");
    tui_filechooser_free(&fc);
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, dir, NULL), 0, "open");
    tui_filechooser_free(&fc);                 /* the thread ends on its own */
    usleep(900 * 1000);
    fault_reset();
    fx_close(&f);
}

/* ── Failures ────────────────────────────────────────────────────────── */

static void test_unreadable_and_arguments(void) {
    tui_filechooser fc;
    ASSERT_EQ_INT(tui_filechooser_open(&fc, "Missing", TUI_FC_OPEN, "/nonexistent-dir-xyz", NULL), 0, "open");
    ASSERT_EQ_INT(settle(&fc), 1, "settled");
    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 2), "Missing"), "the given title");
    ASSERT(has(row(&f, 4), "cannot read: No such file or directory"), "the reason is shown");
    send(&fc, "x\r");
    ASSERT_EQ_INT(fc.state, TUI_FC_ACTIVE, "still open");
    fx_close(&f);
    tui_filechooser_free(&fc);

    ASSERT_EQ_INT(tui_filechooser_open(NULL, NULL, TUI_FC_OPEN, NULL, NULL), -1, "NULL");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, (tui_fc_mode)7, NULL, NULL), -1, "bad mode");
    ASSERT_EQ_INT(tui_filechooser_poll(NULL), 0, "poll(NULL)");
    ASSERT_EQ_INT(tui_filechooser_loading(NULL), 0, "loading(NULL)");
    ASSERT(tui_filechooser_dir(NULL) == NULL, "dir(NULL)");
    ASSERT_EQ_INT(tui_filechooser_handle(NULL, NULL), TUI_FC_CANCELLED, "handle(NULL)");

    /* The start directory and the filter are optional. */
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, NULL, NULL), 0, "start NULL: the current directory");
    char cwd[300];
    ASSERT(getcwd(cwd, sizeof cwd) != NULL, "cwd");
    ASSERT(has(tui_filechooser_dir(&fc), cwd), "it is the current directory");
    settle(&fc);
    tui_event none = { .type = TUI_EV_NONE };
    ASSERT_EQ_INT(tui_filechooser_handle(&fc, &none), TUI_FC_ACTIVE, "other events");
    ASSERT_EQ_INT(tui_filechooser_handle(&fc, NULL), TUI_FC_ACTIVE, "no event");
    tui_filechooser_free(&fc);
    ASSERT_EQ_INT(tui_filechooser_handle(&fc, &none), TUI_FC_ACTIVE, "after free: nothing");
}

/* A link to nothing is listed as itself; a typed path that is too long is refused. */
static void test_dangling_link_and_long_typing(void) {
    char dir[300], link[400];
    snprintf(dir, sizeof dir, "%s/fc-dangling-%d", TUIKIT_TEST_TMPDIR, (int)getpid());
    snprintf(link, sizeof link, "%s/nowhere", dir);
    ASSERT(mkdir(dir, 0755) == 0, "the directory");
    ASSERT(symlink("/nonexistent-target-xyz", link) == 0, "the dangling link");
    tui_filechooser fc;
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, dir, NULL), 0, "open");
    ASSERT_EQ_INT(settle(&fc), 1, "settled");
    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    fx_draw(&f, &fc);
    int shown = 0;
    for (int r = 4; r < 24; r++) shown += has(row(&f, r), "nowhere");
    ASSERT_EQ_INT(shown, 1, "the link is in the list");
    tui_filechooser_free(&fc);
    ASSERT_EQ_INT(unlink(link), 0, "the link is removed");
    ASSERT_EQ_INT(rmdir(dir), 0, "the directory is removed");

    /* A start directory of 3000 characters (it does not exist: the list says so), and a relative path of 3000 typed. */
    char start[3100];
    start[0] = '/';
    memset(start + 1, 'a', 3000);
    start[3001] = '\0';
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, start, NULL), 0, "open at a long directory");
    settle(&fc);
    send(&fc, "\x15");                                   /* Ctrl-U clears the field */
    char typed[101];
    memset(typed, 'b', 100);
    typed[100] = '\0';
    for (int i = 0; i < 30; i++) send(&fc, typed);       /* the decoder takes a short run at a time */
    ASSERT_EQ_INT(send(&fc, "\r"), TUI_FC_ACTIVE, "still open");
    fx_draw(&f, &fc);
    int refused = 0;
    for (int r = 0; r < 24; r++) refused += has(row(&f, r), "path too long");
    ASSERT(refused >= 1, "the path is refused as too long");
    tui_filechooser_free(&fc);
    fx_close(&f);
}

static void test_faults(void) {
    tui_filechooser fc;
    fault_arm(F_CALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, g_root, NULL), -1, "calloc fails");
    ASSERT_EQ_INT(errno, ENOMEM, "ENOMEM");
    fault_reset();
    fault_arm(F_SOCKETPAIR, 0, 1, EMFILE);
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, g_root, NULL), -1, "socketpair fails");
    ASSERT_EQ_INT(errno, EMFILE, "its errno");
    fault_reset();
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, g_root, NULL), -1, "the title cannot be copied");
    fault_reset();
    char longpath[5000];
    memset(longpath, 'x', sizeof longpath - 1);
    longpath[0] = '/';
    longpath[sizeof longpath - 1] = '\0';
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, longpath, NULL), -1, "a path too long");
    char longrel[8300];                        /* relative: the working directory and this are over the limit together */
    memset(longrel, 'y', sizeof longrel - 1);
    longrel[sizeof longrel - 1] = '\0';
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, longrel, NULL), -1, "a relative path too long");

    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    fault_arm(F_CALLOC, 1, 1, ENOMEM);         /* the second calloc: the read's job */
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, g_root, NULL), 0, "open");
    ASSERT_EQ_INT(tui_filechooser_loading(&fc), 0, "no read was started");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "cannot read: Cannot allocate memory"), "and it says why");
    tui_filechooser_free(&fc);
    fault_reset();

    fault_arm(F_PTHREAD_CREATE, 0, 1, EAGAIN);
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, g_root, NULL), 0, "open");
    ASSERT_EQ_INT(tui_filechooser_loading(&fc), 0, "no thread");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "cannot read: Resource temporarily unavailable"), "the reason");
    tui_filechooser_free(&fc);
    fault_reset();

    fault_arm(F_FCNTL, 0, 1, EMFILE);          /* the descriptor handed to the thread */
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, g_root, NULL), 0, "open");
    ASSERT_EQ_INT(tui_filechooser_loading(&fc), 0, "no read");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "cannot read: Too many open files"), "the reason");
    tui_filechooser_free(&fc);
    fault_reset();

    /* The read itself: its list cannot grow, or an entry's name cannot be copied. */
    fault_reset();
    fault_opendir_delay(100);                  /* the read thread waits: the fault is armed after the open */
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, g_root, NULL), 0, "open");
    fault_arm(F_REALLOC, 0, 1, ENOMEM);
    settle(&fc);
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "cannot read: Cannot allocate memory"), "the list could not grow");
    tui_filechooser_free(&fc);
    fault_reset();
    int name_failed = 0;
    for (int skip = 0; skip < 4; skip++) {
        fault_opendir_delay(100);
        ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, g_root, NULL), 0, "open");
        fault_arm(F_MALLOC, skip, 1, ENOMEM);
        settle(&fc);
        fx_draw(&f, &fc);
        if (has(row(&f, 4), "cannot read: Cannot allocate memory")) name_failed++;
        tui_filechooser_free(&fc);
        fault_reset();
    }
    ASSERT(name_failed >= 1, "an entry's name could not be copied");

    /* Every allocation of an open in turn: it either fails or opens without a read. */
    fault_opendir_delay(100);                  /* the thread allocates only after the count is read */
    fault_reset();
    fault_opendir_delay(100);
    ASSERT_EQ_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, g_root, NULL), 0, "a clean open");
    int mallocs = fault_calls(F_MALLOC);
    tui_filechooser_free(&fc);
    usleep(300 * 1000);
    int no_read = 0, refused = 0;
    for (int skip = 0; skip < mallocs; skip++) {
        fault_reset();
        fault_opendir_delay(100);
        fault_arm(F_MALLOC, skip, 1, ENOMEM);
        int rc = tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, g_root, NULL);
        if (rc != 0) {
            refused++;
        } else {
            if (!tui_filechooser_loading(&fc)) no_read++;
            tui_filechooser_free(&fc);
        }
        usleep(300 * 1000);
    }
    ASSERT(refused >= 1, "an allocation of the open itself can fail");
    ASSERT(no_read >= 1, "and so can the one of the read: the dialog opens, without a list");
    fault_reset();
    fx_close(&f);
}

/* ── Small terminals and long lists ──────────────────────────────────── */

static void test_small_and_scrolling(void) {
    tui_filechooser fc;
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, NULL, NULL), 0, "open");
    Fx f;
    ASSERT(fx_open(&f, 9, 30) == 0, "fixtures");
    fx_draw(&f, &fc);
    ASSERT_SCREEN_CONTAINS(f.s, "Open file");
    ASSERT_SCREEN_CONTAINS(f.s, "alpha/");
    ASSERT(!has(row(&f, 4), "2026"), "a narrow list drops the size and the date");
    send(&fc, "\x1b[F");                       /* End: scrolls */
    fx_draw(&f, &fc);
    ASSERT_SCREEN_CONTAINS(f.s, "C.TXT");
    fx_close(&f);
    ASSERT(fx_open(&f, 4, 12) == 0, "a tiny screen");
    fx_draw(&f, &fc);                          /* must not crash */
    fx_close(&f);
    tui_filechooser_free(&fc);
}

static void test_many_entries(void) {
    char sub[300];
    snprintf(sub, sizeof sub, "%s/many", g_root);
    mkdir(sub, 0755);
    for (int i = 0; i < 150; i++) {
        char name[32];
        snprintf(name, sizeof name, "many/f%03d", i);
        fc_tree_file(g_root, name, 1);
    }
    tui_filechooser fc;
    ASSERT_EQ_INT(open_at(&fc, TUI_FC_OPEN, "many", NULL), 0, "open");
    Fx f;
    ASSERT(fx_open(&f, 24, 80) == 0, "fixtures");
    fx_draw(&f, &fc);
    ASSERT(has(row(&f, 4), "150 entries"), "count");
    send(&fc, "\x1b[6~");
    fx_draw(&f, &fc);
    int sel = marked_row(&f, 24);
    ASSERT(sel > 5, "PgDn moves the selection a page down");
    send(&fc, "\x1b[6~");
    fx_draw(&f, &fc);
    ASSERT(marked_row(&f, 24) >= 5, "the marked row stays on the screen");
    send(&fc, "\x1b[5~");
    fx_draw(&f, &fc);
    ASSERT(marked_row(&f, 24) >= 5, "PgUp keeps it on the screen as well");
    send(&fc, "\x1b[F");
    fx_draw(&f, &fc);
    ASSERT_SCREEN_CONTAINS(f.s, "f149");
    ASSERT_EQ_INT(pty_cell_text(f.s, 8, 78)[0] != 0, 1, "a scroll bar in the padding column");
    fx_close(&f);
    tui_filechooser_free(&fc);
}

static const TestCase tests[] = {
    TEST_CASE(test_list_order_and_columns),
    TEST_CASE(test_selection_colours),
    TEST_CASE(test_field_follows_rows),
    TEST_CASE(test_typing_selects_and_changes_directory),
    TEST_CASE(test_enter_directory_and_backspace_up),
    TEST_CASE(test_dotdot_typed),
    TEST_CASE(test_open_mode),
    TEST_CASE(test_dir_mode),
    TEST_CASE(test_save_mode),
    TEST_CASE(test_cancel),
    TEST_CASE(test_tab_completion),
    TEST_CASE(test_hidden_toggle_and_filter),
    TEST_CASE(test_slow_read_does_not_stop_the_keys),
    TEST_CASE(test_unreadable_and_arguments),
    TEST_CASE(test_dangling_link_and_long_typing),
    TEST_CASE(test_faults),
    TEST_CASE(test_small_and_scrolling),
    TEST_CASE(test_many_entries),
};

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    make_tree();
    int rc = RUN_SUITE("filechooser", tests, 150, 120);
    drop_tree();
    return rc;
}
