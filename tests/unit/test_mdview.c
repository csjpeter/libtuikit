/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-11 */
/* Unit tests of the Markdown renderer and of tui_mdview (TUI-117; the cases of IM-390 moved with it). */

/*
 * The text renderer is compared byte for byte; the view is drawn into a
 * screen whose bytes go to libptytest's emulator, and the cells (text,
 * colour, attributes) are compared there.
 */

#include "test_helpers.h"
#include "tuikit/tuikit.h"

#include <errno.h>

int g_tests_run = 0;
int g_tests_failed = 0;

/* The rendering of `md` as plain text into a static buffer. */
static const char *plain(const char *md, int width) {
    static tui_buf b;
    tui_buf_free(&b);
    tui_buf_init(&b);
    if (tui_md_render(md, width, 0, &b) != 0) return "(error)";
    return b.data ? b.data : "";
}

static const char *ansi(const char *md, int width) {
    static tui_buf b;
    tui_buf_free(&b);
    tui_buf_init(&b);
    if (tui_md_render(md, width, 1, &b) != 0) return "(error)";
    return b.data ? b.data : "";
}

/* ── the text renderer ───────────────────────────────────────────────── */

static void test_empty_and_args(void) {
    ASSERT_EQ_STR(plain(NULL, 40), "", "NULL is the empty text");
    ASSERT_EQ_STR(plain("", 40), "", "an empty text");
    ASSERT_EQ_STR(plain("\n\n  \n", 40), "", "only blank lines");
    tui_buf b;
    tui_buf_init(&b);
    errno = 0;
    ASSERT_EQ_INT(tui_md_render("x", 40, 0, NULL), -1, "no output buffer");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_md_render_marked("x", 40, NULL), -1, "marked: no output buffer");
    ASSERT_EQ_INT(tui_md_strip_marks(NULL, &b), -1, "strip: no input");
    ASSERT_EQ_INT(tui_md_strip_marks("x", NULL), -1, "strip: no output");
    tui_buf_free(&b);
}

static void test_headings(void) {
    ASSERT_EQ_STR(plain("# Title\n\ntext", 40), "Title\n=====\n\ntext", "h1 is underlined with =");
    ASSERT_EQ_STR(plain("para\n\n## Sub\n\ntext", 40), "para\n\nSub\n---\n\ntext",
                  "h2 is underlined with -, set apart by blank lines");
    ASSERT_EQ_STR(plain("para\n\n### Third ###\ntext", 40), "para\n\nThird\n\ntext",
                  "h3 has no underline, trailing # dropped");
    ASSERT_EQ_STR(plain("para\n\n####### seven\n", 40), "para\n\n####### seven",
                  "seven # is not a heading");
    ASSERT_EQ_STR(plain("para\n\n#nospace", 40), "para\n\n#nospace", "no space: not a heading");
    ASSERT_EQ_STR(plain("para\n\n## **Bold** head", 40), "para\n\nBold head\n---------",
                  "markers in a heading do not count in the underline");
}

static void test_summary(void) {
    ASSERT_EQ_STR(plain("First one.\n\nSecond.", 40), "First one.\n\nSecond.",
                  "the summary is set apart by a blank line");
    ASSERT_EQ_STR(ansi("First one.\n\nSecond.", 40), "\x1b[1mFirst one.\x1b[0m\n\nSecond.",
                  "the summary is bold in ansi");
    ASSERT_EQ_STR(ansi("# Head\n\nNot a summary.", 40),
                  "\x1b[1mHead\x1b[0m\n====\n\nNot a summary.",
                  "after a heading the paragraph is no summary; the heading is bold");
    ASSERT_EQ_STR(ansi("one two three four five six", 20),
                  "\x1b[1mone two three four\x1b[0m\n\x1b[1mfive six\x1b[0m",
                  "a wrapped summary: each line resets and opens its own");
}

static void test_inline(void) {
    ASSERT_EQ_STR(plain("x **bold** and *it* and `code` here", 60),
                  "x bold and it and code here", "markers dropped in plain");
    ASSERT_EQ_STR(plain("x __bold__ and _it_ end", 60), "x bold and it end", "underscore forms");
    ASSERT_EQ_STR(plain("a snake_case_name stays", 60), "a snake_case_name stays",
                  "an underscore inside a word is no emphasis");
    ASSERT_EQ_STR(plain("an unclosed *star and `tick", 60), "an unclosed *star and `tick",
                  "an unclosed marker is text");
    ASSERT_EQ_STR(plain("a \\*not\\* emphasis \\[x\\]", 60), "a *not* emphasis [x]",
                  "a backslash escapes");
    ASSERT_EQ_STR(plain("see [the docs](https://example.org/x) now", 60),
                  "see the docs (https://example.org/x) now", "a link reads text (url)");
    ASSERT_EQ_STR(plain("a [lone] bracket and [half](open", 60), "a [lone] bracket and [half](open",
                  "a malformed link is text");
    ASSERT_EQ_STR(plain("**a *b* c**", 60), "a b c", "nested emphasis");
    ASSERT_EQ_STR(ansi("S\n\nx **b** `c` *i*", 60),
                  "\x1b[1mS\x1b[0m\n\nx \x1b[1mb\x1b[0m \x1b[36mc\x1b[0m \x1b[3mi\x1b[0m", "SGR for each span");
    ASSERT_EQ_STR(ansi("S\n\n**a *b* c**", 60),
                  "\x1b[1mS\x1b[0m\n\n\x1b[1ma \x1b[3mb\x1b[0m\x1b[1m c\x1b[0m",
                  "a nested span restores the outer one");
}

static void test_lists_and_quotes(void) {
    ASSERT_EQ_STR(plain("intro\n\n- one\n- two\n* three\n+ four", 40),
                  "intro\n\n \xE2\x80\xA2 one\n \xE2\x80\xA2 two\n \xE2\x80\xA2 three\n \xE2\x80\xA2 four",
                  "bullets, one per line, no blank between");
    ASSERT_EQ_STR(plain("intro\n\n1. one\n2. two\n10. ten", 40),
                  "intro\n\n 1. one\n 2. two\n 10. ten", "numbered");
    ASSERT_EQ_STR(plain("intro\n\n- a\n  - nested\n", 40),
                  "intro\n\n \xE2\x80\xA2 a\n   \xE2\x80\xA2 nested", "a nested item is indented");
    ASSERT_EQ_STR(plain("intro\n\n- a long item that has to wrap around twice here", 24),
                  "intro\n\n \xE2\x80\xA2 a long item that has\n   to wrap around twice\n   here",
                  "a wrapped item hangs under its text");
    ASSERT_EQ_STR(plain("intro\n\n> quoted words\n> more words\n>\n> second", 40),
                  "intro\n\n\xE2\x94\x82 quoted words more words\n\n\xE2\x94\x82 second",
                  "quote lines reflow; an empty quote line ends the paragraph");
    ASSERT_EQ_STR(plain("intro\n\n>tight", 40), "intro\n\n\xE2\x94\x82 tight", "a quote without a space");
    ASSERT_EQ_STR(plain("intro\n\n- a\n\n- b", 40), "intro\n\n \xE2\x80\xA2 a\n\n \xE2\x80\xA2 b",
                  "a blank line between items stays");
    ASSERT_EQ_STR(plain("intro\n- a\ntext after", 40),
                  "intro\n\n \xE2\x80\xA2 a\n\ntext after", "a list is set apart from the text around it");
    ASSERT_EQ_STR(plain("-notalist and 12.x", 40), "-notalist and 12.x", "no space after the marker");
    ASSERT_EQ_STR(plain("x\n\n- a\n> q", 40), "x\n\n \xE2\x80\xA2 a\n\n\xE2\x94\x82 q",
                  "a quote after a list");
    ASSERT_EQ_STR(plain("x\n\n> q\n- a", 40), "x\n\n\xE2\x94\x82 q\n\n \xE2\x80\xA2 a",
                  "a list after a quote");
    ASSERT_EQ_STR(plain("x\n\n> q\ntext", 40), "x\n\n\xE2\x94\x82 q\n\ntext",
                  "text after a quote is a new paragraph");
}

static void test_reflow(void) {
    /* The lines of one paragraph are joined and wrapped again. */
    const char *soft = "alpha beta gamma\ndelta epsilon zeta\neta theta iota";
    ASSERT_EQ_STR(plain(soft, 40), "alpha beta gamma delta epsilon zeta eta\ntheta iota",
                  "soft-wrapped lines fill the width");
    ASSERT_EQ_STR(plain(soft, 20), "alpha beta gamma\ndelta epsilon zeta\neta theta iota",
                  "and wrap again at a narrower width");
    ASSERT_EQ_STR(plain("  alpha\n    beta\r\ngamma", 40), "alpha beta gamma", "indent and CR dropped");
    /* An empty line is a new paragraph. */
    ASSERT_EQ_STR(plain("one two\nthree\n\nfour\nfive", 40), "one two three\n\nfour five",
                  "an empty line starts a new paragraph");
    /* A list item is one line; an indented line continues it. */
    ASSERT_EQ_STR(plain("x\n\n- first item\n- second item\n  goes on here\n- third", 40),
                  "x\n\n \xE2\x80\xA2 first item\n \xE2\x80\xA2 second item goes on here\n \xE2\x80\xA2 third",
                  "list items stay separate, an indented line continues one");
    /* A code block keeps its lines. */
    ASSERT_EQ_STR(plain("x\n\n```\nline one\nline two\n```\nafter", 40),
                  "x\n\n    line one\n    line two\n\nafter", "code lines are not joined");
    /* Emphasis may span a source line break. */
    ASSERT_EQ_STR(plain("a **bold\nacross** b", 40), "a bold across b", "a span over a line break");
}

static void test_code(void) {
    ASSERT_EQ_STR(plain("x\n\n```c\nint a;\n  indented();\n```\n", 40),
                  "x\n\n    int a;\n      indented();", "fenced code is indented, keeps its own indent, the fence dropped");
    ASSERT_EQ_STR(plain("x\n\n~~~\ntilde\n~~~\n", 40), "x\n\n    tilde", "tilde fences");
    ASSERT_EQ_STR(plain("x\n\n```\nunclosed\nto the end", 40), "x\n\n    unclosed\n    to the end",
                  "an unclosed fence runs to the end");
    ASSERT_EQ_STR(plain("x\n\n```\n\n```", 40), "x\n\n    ", "an empty code line");
    ASSERT_EQ_STR(plain("x\n\n```\nabcdefghijklmnopqrstuvwxyz0123456789\n```", 24),
                  "x\n\n    abcdefghijklmnopqrst\n    uvwxyz0123456789",
                  "a code line wider than the screen is split, never cut");
    ASSERT_EQ_STR(ansi("x\n\n```\nab\n```", 40), "\x1b[1mx\x1b[0m\n\n    \x1b[36mab\x1b[0m",
                  "code is cyan in ansi");
}

static void test_tables(void) {
    ASSERT_EQ_STR(plain("x\n\n| a | bb |\n|---|---|\n| ccc | d |\n", 40),
                  "x\n\n  a   \xE2\x94\x82 bb\n  \xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xBC\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\n  ccc \xE2\x94\x82 d ",
                  "columns are padded, the header is ruled");
    ASSERT_EQ_STR(plain("x\n\n| h |\n| **b** |\n", 40), "x\n\n  h\n  b", "inline markup in a cell");
    ASSERT_EQ_STR(plain("x\n\n| aaaaaaaaaa | bbbbbbbbbb |\n", 20),
                  "x\n\n  aaaaaa\xE2\x80\xA6 \xE2\x94\x82 bbbbbb\xE2\x80\xA6",
                  "the widest column is cut with an ellipsis to fit");
    ASSERT_EQ_STR(plain("x\n\n| a |\ntext", 40), "x\n\n  a\n\ntext", "the table ends at the first other line");
    /* A cut inside a span does not leave the span open. */
    ASSERT_EQ_STR(ansi("x\n\n| `abcdefghijklmnopqrstuvwxyz` | k |\n", 20),
                  "\x1b[1mx\x1b[0m\n\n  \x1b[36mabcdefghijkl\x1b[0m\xE2\x80\xA6 \xE2\x94\x82 k",
                  "a cell cut inside code closes the span");
}

static void test_narrow_and_wide(void) {
    ASSERT_EQ_STR(plain("one two three four five six", 5), "one two three four\nfive six",
                  "the public renderer wraps at 20 at the least");
    tui_buf b;
    tui_buf_init(&b);
    ASSERT_EQ_INT(tui_md_render_marked("antidisestablishmentarianism", 20, &b), 0, "marked");
    tui_buf_free(&b);
    ASSERT_EQ_STR(plain("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa b", 20),
                  "aaaaaaaaaaaaaaaaaaaa\naaaaaaaaaaaaaaaaaaaa\naaaaaaaaaa b",
                  "a word wider than a line is split at the column");
    ASSERT_EQ_STR(plain("S\n\n**aaaaaaaaaaaaaaaaaaaaaaaaa**", 20), "S\n\naaaaaaaaaaaaaaaaaaaa\naaaaa",
                  "a long bold word is split, its markers kept in place");
    ASSERT_EQ_STR(ansi("S\n\n**aaaaaaaaaaaaaaaaaaaaaaaaa**", 20),
                  "\x1b[1mS\x1b[0m\n\n\x1b[1maaaaaaaaaaaaaaaaaaaa\x1b[0m\n\x1b[1maaaaa\x1b[0m",
                  "and each half carries the bold");
    ASSERT_EQ_STR(plain("- aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 20),
                  " \xE2\x80\xA2 aaaaaaaaaaaaaaaaa\n   aaaaaaaaaaa", "a long word in a list item");
    ASSERT_EQ_STR(plain("\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97 \xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97 "
                        "\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97 \xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97", 20),
                  "\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97 \xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97 "
                  "\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97\n\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97",
                  "wide characters count two columns");
    ASSERT_EQ_STR(plain("tab\there \x01 and \x1b[31m bell", 60), "tab here and [31m bell",
                  "controls are shown as spaces, no byte can pose as a marker");
}

static void test_marked(void) {
    tui_buf b, p;
    tui_buf_init(&b);
    tui_buf_init(&p);
    ASSERT_EQ_INT(tui_md_render_marked("S one\n\n# H\n\nx **b** `c` *i*", 40, &b), 0, "render");
    ASSERT_EQ_STR(b.data, "\x06S one\x04\n\n\x05H\x04\n=\n\nx \x01" "b\x04 \x03" "c\x04 \x02" "i\x04",
                  "markers: summary, heading, bold, code, italic");
    ASSERT_EQ_INT(tui_md_strip_marks(b.data, &p), 0, "strip");
    ASSERT_EQ_STR(p.data, plain("S one\n\n# H\n\nx **b** `c` *i*", 40), "stripped equals the plain rendering");
    tui_buf_free(&b);
    tui_buf_free(&p);
    /* Appends to what is there. */
    tui_buf_init(&b);
    tui_buf_puts(&b, "pre\n");
    tui_md_render("text", 40, 0, &b);
    ASSERT_EQ_STR(b.data, "pre\ntext", "appended; the newline of the caller stays");
    tui_buf_free(&b);
}

/* ── the view ────────────────────────────────────────────────────────── */

typedef struct fx {
    tui_screen *scr;
    tui_buf     buf;
    PtySession *s;
} fx;

static int fx_open(fx *f, int rows, int cols) {
    f->scr = tui_screen_new(rows, cols);
    tui_buf_init(&f->buf);
    f->s = pty_open(cols, rows);
    return (f->scr && f->s) ? 0 : -1;
}

static void fx_show(fx *f) {
    tui_buf_reset(&f->buf);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
}

static void fx_close(fx *f) {
    tui_screen_free(f->scr);
    tui_buf_free(&f->buf);
    pty_close(f->s);
}

static tui_event key(const char *bytes) {
    tui_decoder d;
    tui_decoder_init(&d);
    tui_event ev;
    memset(&ev, 0, sizeof ev);
    tui_decoder_feed(&d, bytes, strlen(bytes));
    if (!tui_decoder_next(&d, &ev)) tui_decoder_timeout(&d, &ev);
    tui_decoder_free(&d);
    return ev;
}

static const char *DOC =
    "Summary line of the doc.\n\n"
    "## Heading\n\n"
    "Text with **bold**, *italic* and `code`\nsoft wrapped here.\n\n"
    "- item\n";

static void test_view_draws_styles(void) {
    fx f;
    ASSERT(fx_open(&f, 10, 40) == 0, "fixtures");
    tui_mdview mv;
    tui_mdview_init(&mv);
    ASSERT_EQ_INT(tui_mdview_set_text(&mv, DOC), 0, "set_text");
    tui_mdview_render(&mv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    const char *const expect[] = {
        "Summary line of the doc.", "",
        "Heading", "-------", "",
        "Text with bold, italic and code soft", "wrapped here.", "",
        " \xE2\x80\xA2 item", "", NULL
    };
    ASSERT_SCREEN_EQ(f.s, expect);
    ASSERT_EQ_INT(tui_mdview_lines(&mv), 9, "nine lines");
    ASSERT_CELL_ATTR(f.s, 0, 0, PTY_ATTR_BOLD);                   /* the summary */
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 0), PTY_FG_DEFAULT, "the summary keeps the text colour");
    ASSERT_CELL_ATTR(f.s, 2, 0, PTY_ATTR_BOLD);                   /* the heading: the accent */
    ASSERT_EQ_INT(pty_cell_fg(f.s, 2, 0), PTY_FG_CYAN, "heading in the accent colour");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 2, 6), PTY_FG_CYAN, "to its last letter");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 5, 0) & PTY_ATTR_BOLD, 0, "plain text is not bold");
    ASSERT_CELL_ATTR(f.s, 5, 10, PTY_ATTR_BOLD);                  /* "bold" */
    ASSERT_CELL_TEXT(f.s, 5, 10, "b");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 5, 14) & PTY_ATTR_BOLD, 0, "bold ends");
    ASSERT_CELL_ATTR(f.s, 5, 16, PTY_ATTR_ITALIC);                /* "italic" */
    ASSERT_CELL_TEXT(f.s, 5, 16, "i");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 5, 27), PTY_FG_YELLOW, "code in the hint colour");
    ASSERT_CELL_TEXT(f.s, 5, 27, "c");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 5, 32), PTY_FG_DEFAULT, "code ends");
    tui_mdview_free(&mv);
    fx_close(&f);
}

static void test_view_plain_fallback(void) {
    fx f;
    ASSERT(fx_open(&f, 10, 40) == 0, "fixtures");
    tui_mdview mv;
    tui_mdview_init(&mv);
    tui_mdview_set_text(&mv, DOC);
    tui_mdview_set_plain(&mv, 1);
    tui_mdview_render(&mv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    const char *const expect[] = {
        "Summary line of the doc.", "",
        "Heading", "-------", "",
        "Text with bold, italic and code soft", "wrapped here.", "",
        " \xE2\x80\xA2 item", "", NULL
    };
    ASSERT_SCREEN_EQ(f.s, expect);
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 40; c++)
            if (pty_cell_attr(f.s, r, c) != 0 || pty_cell_fg(f.s, r, c) != PTY_FG_DEFAULT) {
                ASSERT(0, "plain: a cell carries a colour or an attribute");
            }
    tui_mdview_set_plain(&mv, 0);
    tui_mdview_render(&mv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_CELL_ATTR(f.s, 0, 0, PTY_ATTR_BOLD);
    tui_mdview_set_plain(NULL, 1);
    tui_mdview_free(&mv);
    fx_close(&f);
}

static void test_view_base_style(void) {
    fx f;
    ASSERT(fx_open(&f, 4, 30) == 0, "fixtures");
    tui_mdview mv;
    tui_mdview_init(&mv);
    tui_mdview_set_text(&mv, "Summary\n\n# Head");
    tui_style base = tui_style_make(TUI_GREEN, TUI_BLUE, 0);
    tui_mdview_render(&mv, tui_canvas_of(f.scr), base);
    fx_show(&f);
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 0), PTY_FG_GREEN, "the base colour is the text colour");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 0, 0), pty_cell_bg(f.s, 3, 29), "the background fills the area");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 2, 0), pty_cell_bg(f.s, 3, 29), "and the heading keeps it");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 2, 0), PTY_FG_CYAN, "the heading takes the accent colour");
    tui_mdview_free(&mv);
    fx_close(&f);
}

static void test_view_scroll_and_keys(void) {
    fx f;
    ASSERT(fx_open(&f, 4, 20) == 0, "fixtures");
    tui_mdview mv;
    tui_mdview_init(&mv);
    char md[400] = "";
    for (int i = 1; i <= 10; i++) {
        char l[32];
        snprintf(l, sizeof l, "- row %d\n", i);
        strcat(md, l);
    }
    tui_mdview_set_text(&mv, md);
    tui_canvas all = tui_canvas_of(f.scr);
    tui_mdview_render(&mv, all, TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_EQ_INT(tui_mdview_lines(&mv), 10, "ten lines");
    ASSERT_CELL_TEXT(f.s, 0, 3, "r");
    ASSERT_EQ_STR(pty_cell_text(f.s, 0, 19), "\xE2\x96\x88", "a scroll bar at the right edge");
    ASSERT_EQ_INT(tui_mdview_top(&mv), 0, "at the top");

    tui_event ev = key("j");
    ASSERT_EQ_INT(tui_mdview_handle(&mv, &ev), 1, "j is a scroll key");
    ASSERT_EQ_INT(tui_mdview_top(&mv), 1, "j moves down");
    ev = key("\x1b[B");
    tui_mdview_handle(&mv, &ev);
    ASSERT_EQ_INT(tui_mdview_top(&mv), 2, "Down moves down");
    ev = key("k");
    tui_mdview_handle(&mv, &ev);
    ev = key("\x1b[A");
    tui_mdview_handle(&mv, &ev);
    ASSERT_EQ_INT(tui_mdview_top(&mv), 0, "k and Up move up");
    ev = key("\x1b[A");
    tui_mdview_handle(&mv, &ev);
    ASSERT_EQ_INT(tui_mdview_top(&mv), 0, "clamped at the top");
    tui_mdview_render(&mv, all, TUI_STYLE_DEFAULT);
    ev = key("\x1b[6~");
    ASSERT_EQ_INT(tui_mdview_handle(&mv, &ev), 1, "PgDn");
    ASSERT_EQ_INT(tui_mdview_top(&mv), 3, "a page is the rows less one");
    ev = key("\x1b[5~");
    tui_mdview_handle(&mv, &ev);
    ASSERT_EQ_INT(tui_mdview_top(&mv), 0, "PgUp");
    ev = key("\x1b[F");
    ASSERT_EQ_INT(tui_mdview_handle(&mv, &ev), 1, "End");
    ASSERT_EQ_INT(tui_mdview_top(&mv), 6, "End shows the last row");
    tui_mdview_render(&mv, all, TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_CELL_TEXT(f.s, 3, 7, "1");
    ASSERT_CELL_TEXT(f.s, 3, 8, "0");
    ev = key("\x1b[H");
    ASSERT_EQ_INT(tui_mdview_handle(&mv, &ev), 1, "Home");
    ASSERT_EQ_INT(tui_mdview_top(&mv), 0, "Home goes to the top");
    tui_mdview_scroll_to(&mv, 4);
    ASSERT_EQ_INT(tui_mdview_top(&mv), 4, "scroll_to");
    tui_mdview_scroll(&mv, -2);
    ASSERT_EQ_INT(tui_mdview_top(&mv), 2, "scroll");
    tui_mdview_scroll_end(&mv);
    ASSERT_EQ_INT(tui_mdview_top(&mv), 6, "scroll_end");
    ev = key("q");
    ASSERT_EQ_INT(tui_mdview_handle(&mv, &ev), 0, "q is the program's (back)");
    ev = key("\x1b");
    ASSERT_EQ_INT(tui_mdview_handle(&mv, &ev), 0, "Esc is the program's (back)");
    ev = key("\x7f");
    ASSERT_EQ_INT(tui_mdview_handle(&mv, &ev), 0, "Backspace is the program's (back)");
    ASSERT_EQ_INT(tui_mdview_handle(&mv, NULL), 0, "no event");
    ASSERT_EQ_INT(tui_mdview_handle(NULL, &ev), 0, "no view");

    /* A new text starts at the top. */
    tui_mdview_set_text(&mv, md);
    ASSERT_EQ_INT(tui_mdview_top(&mv), 0, "set_text scrolls to the top");
    tui_mdview_free(&mv);
    fx_close(&f);
}

static void test_view_resize_and_reflow(void) {
    fx f;
    ASSERT(fx_open(&f, 6, 40) == 0, "fixtures");
    tui_mdview mv;
    tui_mdview_init(&mv);
    tui_mdview_set_text(&mv, "S\n\nalpha beta gamma\ndelta epsilon zeta\neta theta iota");
    tui_canvas all = tui_canvas_of(f.scr);
    tui_mdview_render(&mv, all, TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_EQ_INT(tui_mdview_lines(&mv), 4, "wide: the paragraph is two lines");
    ASSERT_CELL_TEXT(f.s, 2, 0, "a");
    /* The same text drawn in a narrower area wraps again. */
    tui_canvas narrow = tui_canvas_sub(all, tui_rect_make(0, 0, 6, 16));
    tui_mdview_render(&mv, narrow, TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(tui_mdview_lines(&mv), 6, "narrow: the paragraph is four lines");
    /* And back. */
    tui_mdview_render(&mv, all, TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(tui_mdview_lines(&mv), 4, "wide again");
    /* Very narrow: 3 columns, and a one-column canvas. */
    tui_mdview_render(&mv, tui_canvas_sub(all, tui_rect_make(0, 0, 6, 3)), TUI_STYLE_DEFAULT);
    ASSERT(tui_mdview_lines(&mv) > 6, "three columns: many lines, no crash");
    tui_mdview_render(&mv, tui_canvas_sub(all, tui_rect_make(0, 0, 6, 1)), TUI_STYLE_DEFAULT);
    ASSERT(tui_mdview_lines(&mv) > 6, "one column: no crash");
    /* Zero size draws nothing. */
    tui_mdview_render(&mv, tui_canvas_sub(all, tui_rect_make(0, 0, 0, 10)), TUI_STYLE_DEFAULT);
    tui_mdview_render(NULL, all, TUI_STYLE_DEFAULT);
    tui_mdview_free(&mv);
    fx_close(&f);
}

static void test_view_empty_and_misc(void) {
    fx f;
    ASSERT(fx_open(&f, 3, 20) == 0, "fixtures");
    tui_mdview mv;
    tui_mdview_init(&mv);
    tui_canvas all = tui_canvas_of(f.scr);
    tui_mdview_render(&mv, all, TUI_STYLE_DEFAULT);   /* never given a text */
    ASSERT_EQ_INT(tui_mdview_lines(&mv), 0, "no text, no lines");
    tui_mdview_set_text(&mv, NULL);
    tui_mdview_render(&mv, all, TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(tui_mdview_lines(&mv), 0, "the empty text");
    tui_mdview_scroll(&mv, 5);
    ASSERT_EQ_INT(tui_mdview_top(&mv), 0, "nothing to scroll");
    ASSERT_EQ_INT(tui_mdview_set_text(NULL, "x"), -1, "set_text: no view");
    ASSERT_EQ_INT(tui_mdview_lines(NULL), 0, "lines: no view");
    ASSERT_EQ_INT(tui_mdview_top(NULL), 0, "top: no view");
    tui_mdview_scroll(NULL, 1);
    tui_mdview_scroll_to(NULL, 1);
    tui_mdview_scroll_end(NULL);
    tui_mdview_init(NULL);
    tui_mdview_free(NULL);
    /* A long span wraps; each line carries it. */
    tui_mdview_set_text(&mv, "S\n\n**one two three four five six seven**");
    tui_mdview_render(&mv, tui_canvas_sub(all, tui_rect_make(0, 0, 3, 12)), TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_CELL_TEXT(f.s, 2, 0, "o");
    ASSERT_CELL_ATTR(f.s, 2, 0, PTY_ATTR_BOLD);
    tui_mdview_scroll_to(&mv, 3);
    tui_mdview_render(&mv, tui_canvas_sub(all, tui_rect_make(0, 0, 3, 12)), TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_CELL_TEXT(f.s, 0, 0, "t");
    ASSERT_CELL_ATTR(f.s, 0, 0, PTY_ATTR_BOLD);        /* the span opened on the line above */
    tui_mdview_free(&mv);
    fx_close(&f);
}

static const TestCase tests[] = {
    TEST_CASE(test_empty_and_args),
    TEST_CASE(test_headings),
    TEST_CASE(test_summary),
    TEST_CASE(test_inline),
    TEST_CASE(test_lists_and_quotes),
    TEST_CASE(test_reflow),
    TEST_CASE(test_code),
    TEST_CASE(test_tables),
    TEST_CASE(test_narrow_and_wide),
    TEST_CASE(test_marked),
    TEST_CASE(test_view_draws_styles),
    TEST_CASE(test_view_plain_fallback),
    TEST_CASE(test_view_base_style),
    TEST_CASE(test_view_scroll_and_keys),
    TEST_CASE(test_view_resize_and_reflow),
    TEST_CASE(test_view_empty_and_misc),
};

int main(int argc, char **argv) {
    return RUN_SUITE("mdview", tests, 135, 60);
}
