/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-22 */
/* Unit tests of the colour schemes: file format, built-in defaults, search order, selector, failures. */

#include "test_helpers.h"
#include "faults.h"
#include "../../src/internal.h"
#include "tuikit/scheme.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static char g_root[512];

static void mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            (void)mkdir(tmp, 0755);
            *p = '/';
        }
    }
    (void)mkdir(tmp, 0755);
}

/* Writes @p text to <root>/<rel>, making the directories. */
static void put_file(const char *rel, const char *text) {
    char path[PATH_MAX + 600], dir[PATH_MAX + 600];
    snprintf(path, sizeof path, "%s/%s", g_root, rel);
    snprintf(dir, sizeof dir, "%s", path);
    *strrchr(dir, '/') = 0;
    mkdir_p(dir);
    FILE *f = fopen(path, "w");
    ASSERT(f != NULL, "test file can be written");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static const char *at(const char *rel) {
    static char path[4][PATH_MAX + 600];
    static int k;
    k = (k + 1) % 4;
    snprintf(path[k], sizeof path[k], "%s/%s", g_root, rel);
    return path[k];
}

static void setup(void) {
    snprintf(g_root, sizeof g_root, "%s/scheme", TUIKIT_TEST_TMPDIR);
    mkdir_p(g_root);
}

static int same(tui_style a, tui_style b) { return tui_style_eq(a, b); }

static void poison(tui_scheme *sc) {
    for (int i = 0; i < TUI_ROLE_COUNT; i++)
        sc->styles[i] = tui_style_make(TUI_COLOR_RGB(1, 2, 3), TUI_COLOR_RGB(4, 5, 6), 0x7F);
}

/* ── Roles and the built-in palette ─────────────────────────────────── */

static void test_roles(void) {
    ASSERT_EQ_INT(TUI_ROLE_COUNT, 15, "fifteen roles");
    for (int i = 0; i < TUI_ROLE_COUNT; i++) {
        const char *n = tui_role_name((tui_role)i);
        ASSERT(n != NULL, "every role has a key");
        ASSERT_EQ_INT(tui_role_from_name(n), i, "key round trip");
    }
    ASSERT_EQ_STR(tui_role_name(TUI_ROLE_INPUT_FOCUS), "input.focus", "input.focus key");
    ASSERT_EQ_STR(tui_role_name(TUI_ROLE_TAB_INACTIVE), "tab.inactive", "tab.inactive key");
    ASSERT(tui_role_name((tui_role)-1) == NULL, "negative role");
    ASSERT(tui_role_name(TUI_ROLE_COUNT) == NULL, "role past the end");
    ASSERT_EQ_INT(tui_role_from_name("nope"), -1, "unknown key");
    ASSERT_EQ_INT(tui_role_from_name(NULL), -1, "NULL key");
}

static void test_builtin_matches_palette(void) {
    ASSERT(same(tui_scheme_builtin(TUI_ROLE_HEADER), TUI_STYLE_HEADER), "header is the palette's");
    ASSERT(same(tui_scheme_builtin(TUI_ROLE_ACCENT), TUI_STYLE_ACCENT), "accent");
    ASSERT(same(tui_scheme_builtin(TUI_ROLE_OK), TUI_STYLE_OK), "ok");
    ASSERT(same(tui_scheme_builtin(TUI_ROLE_ERROR), TUI_STYLE_ERROR), "error");
    ASSERT(same(tui_scheme_builtin(TUI_ROLE_HINT), TUI_STYLE_HINT), "hint");
    ASSERT(same(tui_scheme_builtin(TUI_ROLE_PROGRESS), TUI_STYLE_PROGRESS), "progress");
    ASSERT(same(tui_scheme_builtin(TUI_ROLE_TEXT), TUI_STYLE_DEFAULT), "text is the terminal's");
    /* the selection is the dark blue of the library's table at 256 colours */
    tui_style sel = tui_scheme_builtin(TUI_ROLE_SELECTION);
    ASSERT_EQ_INT(sel.fg, TUI_COLOR_DEFAULT, "selection keeps the foreground");
    ASSERT_EQ_INT(tui_color_reduce(sel.bg, 256), tui__selection_base(256).bg, "selection at 256");
    ASSERT_EQ_INT(tui_color_reduce(sel.bg, 16), tui__selection_base(16).bg, "selection at 16");
    tui_scheme sc;
    tui_scheme_defaults(&sc);
    for (int i = 0; i < TUI_ROLE_COUNT; i++)
        ASSERT(same(sc.styles[i], tui_scheme_builtin((tui_role)i)), "defaults are the built-ins");
}

static void test_default_scheme_file_equals_builtin(void) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/data/schemes/default.scheme", TUIKIT_SOURCE_DIR);
    FILE *f = fopen(path, "r");
    ASSERT(f != NULL, "default.scheme is in the source tree");
    if (!f) return;
    static char text[8192];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    tui_scheme sc;
    tui_scheme_report rep;
    poison(&sc);   /* a key the file lacks stays poisoned and fails below */
    ASSERT_EQ_INT(tui_scheme_parse(&sc, text, "default.scheme", &rep), 0, "default.scheme parses clean");
    for (int i = 0; i < TUI_ROLE_COUNT; i++) {
        tui_style b = tui_scheme_builtin((tui_role)i);
        char what[96];
        snprintf(what, sizeof what, "default.scheme key %s equals the built-in", tui_role_name((tui_role)i));
        ASSERT(same(sc.styles[i], b), what);
    }
}

/* ── The file format ────────────────────────────────────────────────── */

static void test_every_key_colour_and_attribute(void) {
    static const char text[] =
        "# a comment\n"
        "\n"
        "   text = red blue bold\n"
        "header=white   blue   bold,underline   # trailing comment\n"
        "status = bright-white bright-black dim\n"
        "selection = - #000080 -\n"
        "cursor = 200 15 italic,strike\r\n"
        "accent = default default blink\n"
        "frame = bright-cyan - reverse\n"
        "input = #FFaa00 #0a0B0c -\n"
        "input.focus = 0 255 bold,dim,italic,underline,strike,reverse,blink\n"
        "tab.active = black - -\n"
        "tab.inactive = green\n"
        "ok = magenta yellow\n"
        "error = bright-red - bold\n"
        "hint = 16 231 -\n"
        "progress = bright-magenta bright-yellow bright-green\n";
    tui_scheme sc;
    tui_scheme_report rep;
    tui_scheme_defaults(&sc);
    ASSERT_EQ_INT(tui_scheme_parse(&sc, text, "t", &rep), 1, "one problem: the last line's attribute");
    ASSERT(strstr(rep.first, "t:17: progress: bad attribute") != NULL, "that one is named");
    ASSERT(same(sc.styles[TUI_ROLE_PROGRESS], tui_scheme_builtin(TUI_ROLE_PROGRESS)), "and falls back");

    ASSERT(same(sc.styles[TUI_ROLE_TEXT], tui_style_make(TUI_RED, TUI_BLUE, TUI_ATTR_BOLD)), "text");
    ASSERT(same(sc.styles[TUI_ROLE_HEADER],
                tui_style_make(TUI_WHITE, TUI_BLUE, TUI_ATTR_BOLD | TUI_ATTR_UNDERLINE)), "header, comment cut");
    ASSERT(same(sc.styles[TUI_ROLE_STATUS],
                tui_style_make(TUI_BRIGHT_WHITE, TUI_BRIGHT_BLACK, TUI_ATTR_DIM)), "status, bright names");
    ASSERT(same(sc.styles[TUI_ROLE_SELECTION],
                tui_style_make(TUI_COLOR_DEFAULT, TUI_COLOR_RGB(0, 0, 0x80), 0)), "selection, #rrggbb");
    ASSERT(same(sc.styles[TUI_ROLE_CURSOR],
                tui_style_make(TUI_COLOR_256(200), TUI_COLOR_16(15), TUI_ATTR_ITALIC | TUI_ATTR_STRIKE)),
           "cursor, indexes and CRLF");
    ASSERT(same(sc.styles[TUI_ROLE_ACCENT], tui_style_attrs(TUI_ATTR_BLINK)), "accent, default");
    ASSERT(same(sc.styles[TUI_ROLE_FRAME], tui_style_make(TUI_BRIGHT_CYAN, TUI_COLOR_DEFAULT, TUI_ATTR_REVERSE)),
           "frame");
    ASSERT(same(sc.styles[TUI_ROLE_INPUT],
                tui_style_make(TUI_COLOR_RGB(0xFF, 0xAA, 0), TUI_COLOR_RGB(0x0A, 0x0B, 0x0C), 0)),
           "input, hex case");
    ASSERT(same(sc.styles[TUI_ROLE_INPUT_FOCUS],
                tui_style_make(TUI_BLACK, TUI_COLOR_256(255),
                               TUI_ATTR_BOLD | TUI_ATTR_DIM | TUI_ATTR_ITALIC | TUI_ATTR_UNDERLINE |
                               TUI_ATTR_STRIKE | TUI_ATTR_REVERSE | TUI_ATTR_BLINK)),
           "input.focus, every attribute");
    ASSERT(same(sc.styles[TUI_ROLE_TAB_ACTIVE], tui_style_make(TUI_BLACK, TUI_COLOR_DEFAULT, 0)), "tab.active");
    ASSERT(same(sc.styles[TUI_ROLE_TAB_INACTIVE], tui_style_make(TUI_GREEN, TUI_COLOR_DEFAULT, 0)),
           "a missing part is '-'");
    ASSERT(same(sc.styles[TUI_ROLE_OK], tui_style_make(TUI_MAGENTA, TUI_YELLOW, 0)), "ok");
    ASSERT(same(sc.styles[TUI_ROLE_ERROR], tui_style_make(TUI_BRIGHT_RED, TUI_COLOR_DEFAULT, TUI_ATTR_BOLD)),
           "error");
    ASSERT(same(sc.styles[TUI_ROLE_HINT], tui_style_make(TUI_COLOR_256(16), TUI_COLOR_256(231), 0)), "hint");

    /* a NULL report is allowed */
    ASSERT_EQ_INT(tui_scheme_parse(&sc, "bogus\n", "t", NULL), 1, "NULL report");
}

static void expect_bad(const char *line, const char *msg) {
    tui_scheme sc;
    tui_scheme_report rep;
    tui_scheme_defaults(&sc);
    ASSERT_EQ_INT(tui_scheme_parse(&sc, line, "s", &rep), 1, line);
    ASSERT(strstr(rep.first, msg) != NULL, msg);
    for (int i = 0; i < TUI_ROLE_COUNT; i++)
        ASSERT(same(sc.styles[i], tui_scheme_builtin((tui_role)i)), "a malformed line changes nothing");
}

static void test_malformed_lines_fall_back(void) {
    expect_bad("error red\n", "no '='");
    expect_bad("colour = red\n", "s:1: unknown key 'colour'");
    expect_bad("error =\n", "error: empty value");
    expect_bad("error = red - bold extra\n", "too many parts");
    expect_bad("error = chartreuse\n", "bad foreground colour");
    expect_bad("error = red #fff\n", "bad background colour");
    expect_bad("error = red - loud\n", "bad attribute");
    expect_bad("error = red - bold,\n", "bad attribute");
    expect_bad("error = red - bold,,dim\n", "bad attribute");
    expect_bad("error = 256\n", "bad foreground colour");
    expect_bad("error = 1x\n", "bad foreground colour");
    expect_bad("error = 0001\n", "bad foreground colour");
    expect_bad("error = #12345\n", "bad foreground colour");
    expect_bad("error = #12345g\n", "bad foreground colour");
    expect_bad("error = bright-nothing\n", "bad foreground colour");
    expect_bad("error = Red\n", "bad foreground colour");
    char longpart[200];
    memset(longpart, 'a', sizeof longpart - 1);
    longpart[sizeof longpart - 1] = 0;
    char line[700];
    snprintf(line, sizeof line, "error = %s\n", longpart);
    expect_bad(line, "a part is too long");
    snprintf(line, sizeof line, "error = red - bold # %s %s\n", longpart, longpart);   /* too long a line */
    expect_bad(line, "line too long");

    /* the good lines around a bad one still count, and only the first is kept */
    tui_scheme sc;
    tui_scheme_report rep;
    tui_scheme_defaults(&sc);
    ASSERT_EQ_INT(tui_scheme_parse(&sc, "ok = blue\nzzz = 1\nerror\nhint = cyan\n", "o", &rep), 2, "two problems");
    ASSERT_EQ_INT(rep.problems, 2, "report counts them");
    ASSERT(strstr(rep.first, "o:2: unknown key 'zzz'") != NULL, "the first is kept");
    ASSERT(same(sc.styles[TUI_ROLE_OK], tui_style_make(TUI_BLUE, TUI_COLOR_DEFAULT, 0)), "line before");
    ASSERT(same(sc.styles[TUI_ROLE_HINT], tui_style_make(TUI_CYAN, TUI_COLOR_DEFAULT, 0)), "line after");
}

/* ── Search order and the selector ──────────────────────────────────── */

static void test_user_scheme_wins_over_system(void) {
    put_file("so/user/schemes/pair.scheme", "error = blue - -\n");
    put_file("so/share/pair.scheme", "error = green - -\nhint = red - -\n");
    put_file("so/share/only.scheme", "hint = magenta - -\n");
    tui_scheme sc;
    tui_scheme_report rep;
    ASSERT_EQ_INT(tui__scheme_load_in(at("so/user"), at("so/etc"), at("so/share"), "pair", &sc, &rep), 0,
                  "the user's scheme loads");
    ASSERT_EQ_INT(sc.styles[TUI_ROLE_ERROR].fg, TUI_BLUE, "the user's file wins");
    ASSERT(same(sc.styles[TUI_ROLE_HINT], tui_scheme_builtin(TUI_ROLE_HINT)), "the system one is not mixed in");
    ASSERT_EQ_INT(tui__scheme_load_in(at("so/user"), at("so/etc"), at("so/share"), "only", &sc, &rep), 0,
                  "a system scheme loads");
    ASSERT_EQ_INT(sc.styles[TUI_ROLE_HINT].fg, TUI_MAGENTA, "from the system directory");
    ASSERT_EQ_INT(tui__scheme_load_in(NULL, at("so/etc"), at("so/share"), "pair", &sc, &rep), 0, "no user dir");
    ASSERT_EQ_INT(sc.styles[TUI_ROLE_ERROR].fg, TUI_GREEN, "then the system one");
}

static void test_selector_search_order(void) {
    put_file("sel/user/schemes/mine.scheme", "ok = cyan - -\n");
    put_file("sel/share/mine.scheme", "ok = red - -\n");
    put_file("sel/share/sys.scheme", "ok = yellow - -\n");
    put_file("sel/share/etcs.scheme", "ok = magenta - -\n");
    tui_scheme sc;
    tui_scheme_report rep;

    /* nothing set: the built-in palette */
    ASSERT_EQ_INT(tui__scheme_load_in(at("sel/none"), at("sel/none"), at("sel/share"), NULL, &sc, &rep), 0,
                  "no tuikit.conf: no problem");
    ASSERT(same(sc.styles[TUI_ROLE_OK], tui_scheme_builtin(TUI_ROLE_OK)), "the built-in palette");

    /* /etc only */
    put_file("sel/etc/tuikit.conf", "# system\nother = 1\nscheme = etcs\n");
    ASSERT_EQ_INT(tui__scheme_load_in(at("sel/user"), at("sel/etc"), at("sel/share"), NULL, &sc, &rep), 0,
                  "the /etc selector");
    ASSERT_EQ_INT(sc.styles[TUI_ROLE_OK].fg, TUI_MAGENTA, "/etc/tuikit/tuikit.conf names it");

    /* the user's conf wins over /etc */
    put_file("sel/user/tuikit.conf", "scheme = sys\n");
    ASSERT_EQ_INT(tui__scheme_load_in(at("sel/user"), at("sel/etc"), at("sel/share"), NULL, &sc, &rep), 0,
                  "the user selector");
    ASSERT_EQ_INT(sc.styles[TUI_ROLE_OK].fg, TUI_YELLOW, "the user's tuikit.conf wins");

    /* and the user's scheme of that name wins over the system's */
    put_file("sel/user/tuikit.conf", "scheme = mine\n");
    ASSERT_EQ_INT(tui__scheme_load_in(at("sel/user"), at("sel/etc"), at("sel/share"), NULL, &sc, &rep), 0, "mine");
    ASSERT_EQ_INT(sc.styles[TUI_ROLE_OK].fg, TUI_CYAN, "user scheme, selected by the conf");

    /* a user conf without the key leaves it to /etc */
    put_file("sel/user/tuikit.conf", "other = 2\n");
    ASSERT_EQ_INT(tui__scheme_load_in(at("sel/user"), at("sel/etc"), at("sel/share"), NULL, &sc, &rep), 0, "fall");
    ASSERT_EQ_INT(sc.styles[TUI_ROLE_OK].fg, TUI_MAGENTA, "falls to /etc");

    /* an explicit name beats the selector */
    ASSERT_EQ_INT(tui__scheme_load_in(at("sel/user"), at("sel/etc"), at("sel/share"), "sys", &sc, &rep), 0, "named");
    ASSERT_EQ_INT(sc.styles[TUI_ROLE_OK].fg, TUI_YELLOW, "the name given");
}

static void test_selector_problems(void) {
    tui_scheme sc;
    tui_scheme_report rep;
    put_file("selp/user/tuikit.conf", "scheme = ../evil\nnoequals\n");
    put_file("selp/share/x.scheme", "ok = red - -\n");
    ASSERT_EQ_INT(tui__scheme_load_in(at("selp/user"), at("selp/etc"), at("selp/share"), NULL, &sc, &rep), 2,
                  "a bad name and a bad line in tuikit.conf");
    ASSERT(strstr(rep.first, "tuikit.conf:1: bad scheme name '../evil'") != NULL, "named in the report");
    ASSERT(same(sc.styles[TUI_ROLE_OK], tui_scheme_builtin(TUI_ROLE_OK)), "built-in");

    char big[LINE_MAX + 400];
    memset(big, 'x', sizeof big - 2);
    big[sizeof big - 2] = '\n';
    big[sizeof big - 1] = 0;
    put_file("selp/user/tuikit.conf", big);
    ASSERT_EQ_INT(tui__scheme_load_in(at("selp/user"), at("selp/etc"), at("selp/share"), NULL, &sc, &rep), 1,
                  "a long line in tuikit.conf");
    ASSERT(strstr(rep.first, "line too long") != NULL, "reported");

    ASSERT_EQ_INT(tui__scheme_load_in(at("selp/user"), at("selp/etc"), at("selp/share"), "a/b", &sc, &rep), 1,
                  "a name with a slash");
    ASSERT(strstr(rep.first, "bad scheme name 'a/b'") != NULL, "no path in a name");
    ASSERT_EQ_INT(tui__scheme_load_in(at("selp/user"), at("selp/etc"), at("selp/share"), ".hid", &sc, &rep), 1,
                  "a dot name");
    ASSERT_EQ_INT(tui__scheme_load_in(at("selp/user"), at("selp/etc"), at("selp/share"), "", &sc, &rep), 1,
                  "an empty name");
    char name[80];
    memset(name, 'n', sizeof name - 1);
    name[sizeof name - 1] = 0;
    ASSERT_EQ_INT(tui__scheme_load_in(at("selp/user"), at("selp/etc"), at("selp/share"), name, &sc, &rep), 1,
                  "a long name");
}

static void test_missing_and_unreadable_schemes(void) {
    tui_scheme sc;
    tui_scheme_report rep;
    ASSERT_EQ_INT(tui__scheme_load_in(at("mu/user"), at("mu/etc"), at("mu/share"), "absent", &sc, &rep), 1,
                  "a missing scheme is one problem");
    ASSERT(strstr(rep.first, "scheme 'absent' not found") != NULL, "named");
    for (int i = 0; i < TUI_ROLE_COUNT; i++)
        ASSERT(same(sc.styles[i], tui_scheme_builtin((tui_role)i)), "built-in for every key");
    ASSERT_EQ_INT(tui__scheme_load_in(at("mu/user"), at("mu/etc"), at("mu/share"), "default", &sc, &rep), 0,
                  "default, not installed, is the built-in palette");

    /* a directory where the file should be: unreadable */
    mkdir_p(at("mu/user/schemes/dirlike.scheme"));
    ASSERT_EQ_INT(tui__scheme_load_in(at("mu/user"), at("mu/etc"), at("mu/share"), "dirlike", &sc, &rep), 1,
                  "unreadable");
    ASSERT(strstr(rep.first, "cannot read") != NULL, "says so");

    /* a path through a file: not ENOENT */
    put_file("mu/afile", "x\n");
    ASSERT_EQ_INT(tui__scheme_load_in(at("mu/afile"), at("mu/etc"), at("mu/share"), "any", &sc, &rep), 1,
                  "cannot open; the missing one after it is not counted again");
    ASSERT(strstr(rep.first, "cannot open") != NULL, "says so");

    /* too long a file */
    static char big[65537 + 10];
    memset(big, '#', sizeof big - 1);
    for (size_t i = 99; i < sizeof big - 1; i += 100) big[i] = '\n';
    big[sizeof big - 1] = 0;
    put_file("mu/user/schemes/big.scheme", big);
    ASSERT_EQ_INT(tui__scheme_load_in(at("mu/user"), at("mu/etc"), at("mu/share"), "big", &sc, &rep), 1,
                  "file too long");
    ASSERT(strstr(rep.first, "file too long") != NULL, "says so");
    big[65535] = 0;
    put_file("mu/user/schemes/exact.scheme", big);
    ASSERT_EQ_INT(tui__scheme_load_in(at("mu/user"), at("mu/etc"), at("mu/share"), "exact", &sc, &rep), 0,
                  "a file of exactly the limit is read");

    /* paths too long for the buffers are skipped */
    static char longdir[PATH_MAX + 50];
    memset(longdir, 'd', sizeof longdir - 1);
    longdir[0] = '/';
    longdir[sizeof longdir - 1] = 0;
    ASSERT_EQ_INT(tui__scheme_load_in(longdir, longdir, at("mu/share"), "absent", &sc, &rep), 1,
                  "too long user and etc directories");

    /* memory */
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui__scheme_load_in(at("mu/user"), at("mu/etc"), at("mu/share"), "exact", &sc, &rep), 1,
                  "no memory is one problem");
    fault_reset();
    ASSERT(strstr(rep.first, "out of memory") != NULL, "says so");
    ASSERT(same(sc.styles[TUI_ROLE_ERROR], tui_scheme_builtin(TUI_ROLE_ERROR)), "built-in");
}

/* ── The public loader and the active scheme, through the environment ── */

static void test_public_load_and_active(void) {
    char xdg[PATH_MAX + 600];
    snprintf(xdg, sizeof xdg, "%s/pub/xdg", g_root);
    put_file("pub/xdg/tuikit/schemes/blue.scheme", "error = blue - underline\n");
    put_file("pub/xdg/tuikit/tuikit.conf", "scheme = blue\n");
    put_file("pub/home/.config/tuikit/schemes/home.scheme", "hint = red - -\n");
    put_file("pub/bad/tuikit/schemes/bad.scheme", "error = nope\n");
    put_file("pub/bad/tuikit/tuikit.conf", "scheme = bad\n");
    char home[PATH_MAX + 600];
    snprintf(home, sizeof home, "%s/pub/home", g_root);

    tui_scheme sc;
    tui_scheme_report rep;
    setenv("XDG_CONFIG_HOME", xdg, 1);
    ASSERT_EQ_INT(tui_scheme_load(&sc, "blue", &rep), 0, "XDG_CONFIG_HOME is the user directory");
    ASSERT_EQ_INT(sc.styles[TUI_ROLE_ERROR].fg, TUI_BLUE, "its scheme");
    ASSERT_EQ_INT(tui_scheme_load(&sc, "blue", NULL), 0, "NULL report");

    ASSERT_EQ_INT(tui_scheme_init(), 0, "init reads the selector");
    ASSERT_EQ_INT(tui_scheme_style(TUI_ROLE_ERROR).fg, TUI_BLUE, "the active scheme's style");
    ASSERT_EQ_INT(tui_scheme_style(TUI_ROLE_ERROR).attrs, TUI_ATTR_UNDERLINE, "its attributes");
    ASSERT(same(tui_scheme_style(TUI_ROLE_OK), tui_scheme_builtin(TUI_ROLE_OK)), "a key it lacks is built-in");
    ASSERT(tui_scheme_diagnostic() == NULL, "no diagnostic");
    ASSERT(same(tui_scheme_style((tui_role)-1), TUI_STYLE_DEFAULT), "a bad role");
    ASSERT(same(tui_scheme_style(TUI_ROLE_COUNT), TUI_STYLE_DEFAULT), "a role past the end");

    snprintf(xdg, sizeof xdg, "%s/pub/bad", g_root);
    setenv("XDG_CONFIG_HOME", xdg, 1);
    ASSERT_EQ_INT(tui_scheme_init(), 1, "reload sees the bad scheme");
    ASSERT(tui_scheme_diagnostic() != NULL, "a diagnostic to show once");
    ASSERT(strstr(tui_scheme_diagnostic(), "bad.scheme:1: error: bad foreground colour") != NULL, "which names it");
    ASSERT(same(tui_scheme_style(TUI_ROLE_ERROR), TUI_STYLE_ERROR), "and the role is built-in");

    /* without XDG_CONFIG_HOME: $HOME/.config */
    unsetenv("XDG_CONFIG_HOME");
    setenv("HOME", home, 1);
    ASSERT_EQ_INT(tui_scheme_load(&sc, "home", &rep), 0, "HOME/.config/tuikit");
    ASSERT_EQ_INT(sc.styles[TUI_ROLE_HINT].fg, TUI_RED, "its scheme");
    setenv("XDG_CONFIG_HOME", "relative/dir", 1);
    ASSERT_EQ_INT(tui_scheme_load(&sc, "home", &rep), 0, "a relative XDG_CONFIG_HOME is ignored");

    /* neither: no user directory; the system directories remain */
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("HOME");
    ASSERT_EQ_INT(tui_scheme_load(&sc, "home", &rep), 1, "no user directory");

    /* a user directory that does not fit */
    static char longx[PATH_MAX + 20];
    memset(longx, 'x', sizeof longx - 1);
    longx[0] = '/';
    longx[sizeof longx - 1] = 0;
    setenv("XDG_CONFIG_HOME", longx, 1);
    ASSERT_EQ_INT(tui_scheme_load(&sc, "home", &rep), 1, "too long XDG_CONFIG_HOME");
    unsetenv("XDG_CONFIG_HOME");
    setenv("HOME", longx, 1);
    ASSERT_EQ_INT(tui_scheme_load(&sc, "home", &rep), 1, "too long HOME");
    setenv("HOME", home, 1);
    setenv("XDG_CONFIG_HOME", xdg, 1);
    tui_scheme_init();
}

/* ── Colour depth ───────────────────────────────────────────────────── */

static void test_color_reduce(void) {
    ASSERT_EQ_INT(tui_color_reduce(TUI_COLOR_DEFAULT, 8), TUI_COLOR_DEFAULT, "default stays");
    ASSERT_EQ_INT(tui_color_reduce(0x09000000u, 8), 0x09000000u, "an unknown kind stays");
    ASSERT_EQ_INT(tui_color_reduce(TUI_RED, 256), TUI_RED, "16 stays at 256");
    ASSERT_EQ_INT(tui_color_reduce(TUI_COLOR_256(99), 256), TUI_COLOR_256(99), "256 stays at 256");
    ASSERT_EQ_INT(tui_color_reduce(TUI_BRIGHT_RED, 16), TUI_BRIGHT_RED, "16 stays at 16");
    ASSERT_EQ_INT(tui_color_reduce(TUI_BLUE, 8), TUI_BLUE, "a normal colour stays at 8");
    ASSERT_EQ_INT(tui_color_reduce(TUI_COLOR_RGB(255, 255, 255), 16), TUI_BRIGHT_WHITE, "white at 16");
    ASSERT_EQ_INT(tui_color_reduce(TUI_COLOR_RGB(30, 30, 30), 16), TUI_BLACK, "near black at 16");
    ASSERT_EQ_INT(tui_color_reduce(TUI_COLOR_RGB(0, 0, 0x5f), 256), TUI_COLOR_256(17), "dark blue at 256");
    ASSERT_EQ_INT(tui_color_reduce(TUI_COLOR_RGB(255, 255, 255), 256), TUI_COLOR_256(231), "white at 256");
    ASSERT_EQ_INT(tui_color_reduce(TUI_COLOR_RGB(128, 128, 128), 256), TUI_COLOR_256(244), "grey at 256");
    ASSERT_EQ_INT(tui_color_reduce(TUI_COLOR_RGB(0, 0, 0x5f), 16), TUI_BLUE, "dark blue at 16");
    ASSERT_EQ_INT(tui_color_reduce(TUI_COLOR_RGB(255, 0, 0), 16), TUI_BRIGHT_RED, "red at 16");
    ASSERT_EQ_INT(tui_color_reduce(TUI_COLOR_RGB(0, 0, 0x5f), 8), TUI_BLUE, "dark blue at 8");
    ASSERT_EQ_INT(tui_color_reduce(TUI_COLOR_RGB(255, 0, 0), 8), TUI_RED, "red at 8");
    ASSERT_EQ_INT(tui_color_reduce(TUI_COLOR_256(196), 8), TUI_RED, "256 red at 8");
    ASSERT_EQ_INT(tui_color_reduce(TUI_COLOR_256(234), 16), TUI_BLACK, "256 dark grey at 16");
    ASSERT_EQ_INT(tui_color_reduce(TUI_BRIGHT_GREEN, 8), TUI_GREEN, "bright green at 8");
    ASSERT_EQ_INT(tui_color_reduce(TUI_COLOR_RGB(0, 0, 0), 8), TUI_BLACK, "black at 8");
}

/* ── The attributes the scheme adds reach the terminal ──────────────── */

static void test_strike_and_blink_are_rendered(void) {
    tui_screen *s = tui_screen_new(2, 10);
    tui_buf b;
    tui_buf_init(&b);
    tui_screen_put(s, 0, 0, 0, "x", tui_style_attrs(TUI_ATTR_STRIKE));
    tui_screen_put(s, 0, 1, 0, "y", tui_style_attrs(TUI_ATTR_BLINK));
    tui_screen_put(s, 0, 2, 0, "z", tui_style_attrs(TUI_ATTR_BLINK | TUI_ATTR_STRIKE | TUI_ATTR_BOLD));
    tui_screen_render(s, &b);
    ASSERT(strstr(b.data, "\033[0;9mx") != NULL, "strike is SGR 9");
    ASSERT(strstr(b.data, "\033[0;5my") != NULL, "blink is SGR 5");
    ASSERT(strstr(b.data, "\033[0;1;5;9mz") != NULL, "they combine");
    tui_buf_free(&b);
    tui_screen_free(s);
}

static const TestCase tests[] = {
    TEST_CASE(test_roles),
    TEST_CASE(test_builtin_matches_palette),
    TEST_CASE(test_default_scheme_file_equals_builtin),
    TEST_CASE(test_every_key_colour_and_attribute),
    TEST_CASE(test_malformed_lines_fall_back),
    TEST_CASE(test_user_scheme_wins_over_system),
    TEST_CASE(test_selector_search_order),
    TEST_CASE(test_selector_problems),
    TEST_CASE(test_missing_and_unreadable_schemes),
    TEST_CASE(test_public_load_and_active),
    TEST_CASE(test_color_reduce),
    TEST_CASE(test_strike_and_blink_are_rendered),
};

int main(int argc, char **argv) {
    setup();
    return RUN_SUITE("scheme", tests, 250, 60);
}
