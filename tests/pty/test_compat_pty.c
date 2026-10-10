/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* The PTY scenarios of email-cli and tg-cli, carried over. */

/*
 * The PTY scenarios of email-cli and tg-cli, carried over: the same
 * keystrokes against the same harness behaviour (tests/pty/compat_harness.c),
 * through a real pseudo-terminal.
 *
 *   email-cli tests/pty/test_input_line_pty.c       -> input_line scenarios
 *   tg-cli tests/functional/pty/test_readline.c     -> readline scenarios
 *   tg-cli tests/functional/pty/test_password_prompt.c -> password scenarios
 *   tg-cli tests/functional/pty/test_terminal_coverage.c -> keys, wait_key
 *
 * The originals mostly look for text anywhere on a row; where the layout is
 * known, the full row is compared here as well.
 */

#include "test_helpers.h"

#include <fcntl.h>
#include <sys/stat.h>

int g_tests_run = 0;
int g_tests_failed = 0;

#ifndef HARNESS
#error HARNESS must be defined by the build
#endif

#define WAIT_MS 5000

static char g_root[512];

static PtySession *start(const char *mode, const char *arg) {
    /* Wide enough for a fixture path on one row, as the originals' short
     * /tmp paths were in 80 columns. */
    PtySession *s = pty_open(200, 24);
    if (!s) return NULL;
    const char *argv[] = { HARNESS, mode, arg, NULL };
    if (pty_run(s, argv) != 0) {
        pty_close(s);
        return NULL;
    }
    return s;
}

/* The completion fixtures: a directory under the build tree, per run. */
static int fs_setup(void) {
    snprintf(g_root, sizeof(g_root), "%s/compat-%d", TUIKIT_TEST_TMPDIR, (int)getpid());
    return mkdir(g_root, 0700);
}

static void fs_teardown(void) {
    char cmd[700];
    /* A literal prefix with the pid: never a bare variable. */
    snprintf(cmd, sizeof(cmd), "rm -rf '%s/compat-%d'", TUIKIT_TEST_TMPDIR, (int)getpid());
    if (system(cmd) != 0) printf("  warning: could not remove %s\n", g_root);
}

/* Each completion test gets a directory of its own, so no test sees
 * another's files. */
static char g_dir[540];

static int use_dir(const char *name) {
    snprintf(g_dir, sizeof(g_dir), "%s/%s", g_root, name);
    return mkdir(g_dir, 0700);
}

static int make_file(const char *rel) {
    char p[1024];
    snprintf(p, sizeof(p), "%s/%s", g_dir, rel);
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    close(fd);
    return 0;
}

static int make_dir(const char *rel) {
    char p[1024];
    snprintf(p, sizeof(p), "%s/%s", g_dir, rel);
    return mkdir(p, 0700);
}

/* Waits until @p text is on the screen at least @p n times: the same line
 * accepted twice, which a plain wait would see already after the first. */
typedef struct { const char *text; int n; } count_ctx;

static int count_on_screen(PtySession *s, void *ctx) {
    count_ctx *c = ctx;
    char screen[8192];
    pty_screen_text(s, screen, sizeof(screen));
    int found = 0;
    for (const char *p = screen; (p = strstr(p, c->text)) != NULL; p++) found++;
    return found >= c->n;
}

#define ASSERT_WAIT_COUNT(s, text, count) do { \
    g_tests_run++; \
    count_ctx _c = { text, count }; \
    if (pty_wait_until(s, count_on_screen, &_c, WAIT_MS) != 0) { \
        PTY_ASSERT_FAIL_S_(s, (const char *)NULL, "\"%s\" not on the screen %d times", text, count); \
        pty_screen_dump(s, PTY_ASSERT_OUT); \
        return; \
    } \
} while (0)

/* Waits for the harness to exit after its result line. */
#define FINISH(s) do { ASSERT_EXIT_STATUS(s, 0, WAIT_MS); pty_close(s); } while (0)

/* ── email-cli: input_line ───────────────────────────────────────────── */

static void test_utf8_accented_input(void) {
    PtySession *s = start("input_line", "");
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "Path:", WAIT_MS);
    pty_send_str(s, "h\xC5\x91" "cser\xC3\xA9l\xC5\x91-sz\xC3\xA1mla.pdf");
    ASSERT_WAIT_FOR(s, "h\xC5\x91" "cser\xC3\xA9l\xC5\x91-sz\xC3\xA1mla.pdf", WAIT_MS);
    const char *const row[] = { "  Path: h\xC5\x91" "cser\xC3\xA9l\xC5\x91-sz\xC3\xA1mla.pdf", NULL };
    ASSERT_REGION_EQ(s, PTY_REGION(0, 0, 1, 200), row);
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "RESULT: h\xC5\x91" "cser\xC3\xA9l\xC5\x91-sz\xC3\xA1mla.pdf", WAIT_MS);
    FINISH(s);
}

static void test_utf8_backspace_deletes_code_point(void) {
    PtySession *s = start("input_line", "");
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "Path:", WAIT_MS);
    pty_send_str(s, "\xC3\xA1rv\xC3\xADz");
    ASSERT_WAIT_FOR(s, "\xC3\xA1rv\xC3\xADz", WAIT_MS);
    pty_send_key(s, PTY_KEY_BACK);
    pty_send_key(s, PTY_KEY_BACK);
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "RESULT: \xC3\xA1rv", WAIT_MS);
    ASSERT_SCREEN_NOT_CONTAINS(s, "RESULT: \xC3\xA1rv\xC3");
    FINISH(s);
}

static void test_utf8_insert_in_middle(void) {
    PtySession *s = start("input_line", "");
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "Path:", WAIT_MS);
    pty_send_str(s, "\xC5\x91z");
    ASSERT_WAIT_FOR(s, "\xC5\x91z", WAIT_MS);
    pty_send_key(s, PTY_KEY_LEFT);
    pty_send_str(s, "\xC3\xA9");
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "RESULT: \xC5\x91\xC3\xA9z", WAIT_MS);
    FINISH(s);
}

static void test_prompt_and_typing(void) {
    PtySession *s = start("input_line", "");
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "Path:", WAIT_MS);
    ASSERT_ROW_CONTAINS(s, 0, "Path:");
    pty_send_str(s, "hello");
    const char *const row[] = { "  Path: hello", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(0, 0, 1, 200), row, WAIT_MS);
    /* The cursor is put back on the row in a write of its own, after
     * render_below: wait for it rather than look in between. */
    ASSERT_WAIT_CURSOR(s, 0, 13, WAIT_MS, "cursor after hello (email-cli: 3 + prompt + text)");
    pty_send_key(s, PTY_KEY_BACK);
    const char *const row2[] = { "  Path: hell", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(0, 0, 1, 200), row2, WAIT_MS);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_FOR(s, "CANCELLED", WAIT_MS);
    ASSERT_SCREEN_NOT_CONTAINS(s, "RESULT:");
    FINISH(s);
}

static void test_initial_text_confirmed(void) {
    PtySession *s = start("input_line", "/some/path.txt");
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "/some/path.txt", WAIT_MS);
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "RESULT: /some/path.txt", WAIT_MS);
    FINISH(s);
}

static void test_typed_text_in_result(void) {
    PtySession *s = start("input_line", "");
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "Path:", WAIT_MS);
    pty_send_str(s, "/tmp/myfile.txt");
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "RESULT: /tmp/myfile.txt", WAIT_MS);
    FINISH(s);
}

static void test_arrows_home_end(void) {
    PtySession *s = start("input_line", "abc");
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "abc", WAIT_MS);
    pty_send_key(s, PTY_KEY_LEFT);
    pty_send_key(s, PTY_KEY_LEFT);
    pty_send_str(s, "X");
    ASSERT_WAIT_FOR(s, "aXbc", WAIT_MS);
    pty_send_key(s, PTY_KEY_HOME);
    pty_send_str(s, "Z");
    ASSERT_WAIT_FOR(s, "ZaXbc", WAIT_MS);
    pty_send_key(s, PTY_KEY_END);
    pty_send_str(s, "Y");
    const char *const row[] = { "  Path: ZaXbcY", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(0, 0, 1, 200), row, WAIT_MS);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_FOR(s, "CANCELLED", WAIT_MS);
    FINISH(s);
}

/* Tab completion, through the tab_fn / shift_tab_fn / render_below hooks. */
static void test_tab_single_match(void) {
    ASSERT(use_dir("test_tab_single_match") == 0, "fixture directory");
    ASSERT(make_file("report.pdf") == 0, "fixture");
    char prefix[600];
    snprintf(prefix, sizeof(prefix), "%s/rep", g_dir);
    PtySession *s = start("input_line", prefix);
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "rep", WAIT_MS);
    pty_send_key(s, PTY_KEY_TAB);
    ASSERT_WAIT_FOR(s, "report.pdf", WAIT_MS);
    pty_send_key(s, PTY_KEY_ENTER);
    char want[700];
    snprintf(want, sizeof(want), "RESULT: %s/report.pdf", g_dir);
    ASSERT_WAIT_FOR(s, want, WAIT_MS);
    FINISH(s);
}

static void test_tab_bar_and_cycling(void) {
    ASSERT(use_dir("test_tab_bar_and_cycling") == 0, "fixture directory");
    ASSERT(make_file("zz_alpha.txt") == 0 && make_file("zz_beta.txt") == 0 &&
           make_file("zz_gamma.txt") == 0, "fixtures");
    char prefix[600];
    snprintf(prefix, sizeof(prefix), "%s/zz_", g_dir);
    PtySession *s = start("input_line", prefix);
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "zz_", WAIT_MS);
    pty_send_key(s, PTY_KEY_TAB);
    ASSERT_WAIT_FOR(s, "zz_alpha.txt", WAIT_MS);
    const char *const bar[] = { "  zz_alpha.txt  zz_beta.txt  zz_gamma.txt", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(1, 0, 1, 200), bar, WAIT_MS);
    char want[700];
    const char *const row[] = { want, NULL };
    pty_send_key(s, PTY_KEY_TAB);
    snprintf(want, sizeof(want), "  Path: %s/zz_beta.txt", g_dir);
    ASSERT_WAIT_REGION(s, PTY_REGION(0, 0, 1, 200), row, WAIT_MS);
    pty_send_key(s, PTY_KEY_TAB);
    snprintf(want, sizeof(want), "  Path: %s/zz_gamma.txt", g_dir);
    ASSERT_WAIT_REGION(s, PTY_REGION(0, 0, 1, 200), row, WAIT_MS);
    pty_send_key(s, PTY_KEY_TAB);                /* 4th Tab wraps to alpha */
    snprintf(want, sizeof(want), "  Path: %s/zz_alpha.txt", g_dir);
    ASSERT_WAIT_REGION(s, PTY_REGION(0, 0, 1, 200), row, WAIT_MS);
    pty_send(s, "\033[Z", 3);                    /* Shift-Tab: back to gamma */
    snprintf(want, sizeof(want), "  Path: %s/zz_gamma.txt", g_dir);
    ASSERT_WAIT_REGION(s, PTY_REGION(0, 0, 1, 200), row, WAIT_MS);
    pty_send(s, "\033[Z", 3);                    /* and beta */
    snprintf(want, sizeof(want), "  Path: %s/zz_beta.txt", g_dir);
    ASSERT_WAIT_REGION(s, PTY_REGION(0, 0, 1, 200), row, WAIT_MS);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_FOR(s, "CANCELLED", WAIT_MS);
    FINISH(s);
}

static void test_shift_tab_starts_backward(void) {
    ASSERT(use_dir("test_shift_tab_starts_backward") == 0, "fixture directory");
    ASSERT(make_file("xx_one.txt") == 0 && make_file("xx_three.txt") == 0 &&
           make_file("xx_two.txt") == 0, "fixtures");
    char prefix[600];
    snprintf(prefix, sizeof(prefix), "%s/xx_", g_dir);
    PtySession *s = start("input_line", prefix);
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "xx_", WAIT_MS);
    pty_send_key(s, PTY_KEY_TAB);
    ASSERT_WAIT_FOR(s, "xx_one.txt", WAIT_MS);
    pty_send(s, "\033[Z", 3);
    ASSERT_WAIT_FOR(s, "/xx_two.txt", WAIT_MS);
    ASSERT_ROW_CONTAINS(s, 0, "xx_two.txt");
    pty_send(s, "\033[Z", 3);
    ASSERT_WAIT_FOR(s, "/xx_three.txt", WAIT_MS);
    ASSERT_ROW_CONTAINS(s, 0, "xx_three.txt");
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_FOR(s, "CANCELLED", WAIT_MS);
    FINISH(s);
}

static void test_slash_enters_subdirectory(void) {
    ASSERT(use_dir("test_slash_enters_subdirectory") == 0, "fixture directory");
    ASSERT(make_dir("docs") == 0 && make_file("docs/guide.pdf") == 0, "fixtures");
    char prefix[600];
    snprintf(prefix, sizeof(prefix), "%s/do", g_dir);
    PtySession *s = start("input_line", prefix);
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "/do", WAIT_MS);
    pty_send_key(s, PTY_KEY_TAB);
    ASSERT_WAIT_FOR(s, "/docs", WAIT_MS);
    pty_send_str(s, "/");
    ASSERT_WAIT_FOR(s, "/docs/", WAIT_MS);
    pty_send_key(s, PTY_KEY_TAB);
    ASSERT_WAIT_FOR(s, "guide.pdf", WAIT_MS);
    pty_send_key(s, PTY_KEY_ENTER);
    char want[700];
    snprintf(want, sizeof(want), "RESULT: %s/docs/guide.pdf", g_dir);
    ASSERT_WAIT_FOR(s, want, WAIT_MS);
    FINISH(s);
}

static void test_suffix_preserved(void) {
    ASSERT(use_dir("test_suffix_preserved") == 0, "fixture directory");
    ASSERT(make_file("readme.txt") == 0, "fixture");
    char initial[600];
    snprintf(initial, sizeof(initial), "%s/filename.pdf", g_dir);
    PtySession *s = start("input_line", initial);
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "filename.pdf", WAIT_MS);
    pty_send_key(s, PTY_KEY_HOME);
    for (size_t i = 0; i < strlen(g_dir) + 1; i++) pty_send_key(s, PTY_KEY_RIGHT);
    pty_send_key(s, PTY_KEY_TAB);
    ASSERT_WAIT_FOR(s, "readme.txt", WAIT_MS);
    ASSERT_ROW_CONTAINS(s, 0, "readme.txtfilename.pdf");
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_FOR(s, "CANCELLED", WAIT_MS);
    FINISH(s);
}

static void test_hidden_files_excluded(void) {
    ASSERT(use_dir("test_hidden_files_excluded") == 0, "fixture directory");
    ASSERT(make_dir("h") == 0 && make_file("h/.hidden_secret") == 0 &&
           make_file("h/visible.cfg") == 0, "fixtures");
    char prefix[600];
    snprintf(prefix, sizeof(prefix), "%s/h/", g_dir);
    PtySession *s = start("input_line", prefix);
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "/h/", WAIT_MS);
    pty_send_key(s, PTY_KEY_TAB);
    ASSERT_WAIT_FOR(s, "visible.cfg", WAIT_MS);
    ASSERT_SCREEN_NOT_CONTAINS(s, ".hidden_secret");
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_FOR(s, "CANCELLED", WAIT_MS);
    FINISH(s);
}

/* ── tg-cli: readline ────────────────────────────────────────────────── */

static PtySession *start_rl(void) {
    PtySession *s = start("readline", NULL);
    if (s && pty_wait_for(s, "rl>", WAIT_MS) != 0) {
        pty_close(s);
        return NULL;
    }
    return s;
}

static void rl_line(PtySession *s, const char *expect) {
    char want[128];
    snprintf(want, sizeof(want), "ACCEPTED:%s", expect);
    ASSERT_WAIT_FOR(s, want, WAIT_MS);
}

static void test_rl_editing(void) {
    PtySession *s = start_rl();
    ASSERT(s != NULL, "rl harness ready");
    ASSERT_ROW_CONTAINS(s, 0, "rl>");
    pty_send_str(s, "hello");
    pty_send_key(s, PTY_KEY_ENTER);
    rl_line(s, "hello");
    pty_send_str(s, "helo");
    pty_send_key(s, PTY_KEY_LEFT);
    pty_send_str(s, "l");
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_COUNT(s, "ACCEPTED:hello", 2);
    pty_send_str(s, "world");
    pty_send_key(s, PTY_KEY_HOME);
    pty_send_str(s, "hello ");
    pty_send_key(s, PTY_KEY_END);
    pty_send_key(s, PTY_KEY_ENTER);
    rl_line(s, "hello world");
    pty_send_str(s, "helpx");
    pty_send_key(s, PTY_KEY_BACK);
    pty_send_key(s, PTY_KEY_ENTER);
    rl_line(s, "help");
    ASSERT_SCREEN_NOT_CONTAINS(s, "ACCEPTED:helpx");
    pty_send_key(s, PTY_KEY_CTRL_D);
    ASSERT_EXIT_STATUS(s, 0, WAIT_MS);
    pty_close(s);
}

static void test_rl_kill_keys(void) {
    PtySession *s = start_rl();
    ASSERT(s != NULL, "rl harness ready");
    pty_send_str(s, "helloXXX");
    pty_send_key(s, PTY_KEY_LEFT);
    pty_send_key(s, PTY_KEY_LEFT);
    pty_send_key(s, PTY_KEY_LEFT);
    pty_send(s, "\x0B", 1);                    /* Ctrl-K */
    pty_send_key(s, PTY_KEY_ENTER);
    rl_line(s, "hello");
    pty_send_str(s, "foo bar");
    pty_send(s, "\x17", 1);                    /* Ctrl-W */
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_COUNT(s, "ACCEPTED:foo", 1);          /* "foo " (a trailing blank) */
    ASSERT_SCREEN_NOT_CONTAINS(s, "ACCEPTED:foo bar");
    pty_send_key(s, PTY_KEY_CTRL_D);
    ASSERT_EXIT_STATUS(s, 0, WAIT_MS);
    pty_close(s);
}

static void test_rl_history(void) {
    PtySession *s = start_rl();
    ASSERT(s != NULL, "rl harness ready");
    pty_send_str(s, "first");
    pty_send_key(s, PTY_KEY_ENTER);
    rl_line(s, "first");
    pty_send_key(s, PTY_KEY_UP);
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_COUNT(s, "ACCEPTED:first", 2);
    /* Up then Down brings back the line being edited. */
    pty_send_str(s, "alpha");
    pty_send_key(s, PTY_KEY_UP);
    pty_send_key(s, PTY_KEY_DOWN);
    pty_send_key(s, PTY_KEY_ENTER);
    rl_line(s, "alpha");
    pty_send_str(s, "beta");
    pty_send_key(s, PTY_KEY_ENTER);
    rl_line(s, "beta");
    pty_send_key(s, PTY_KEY_CTRL_D);
    ASSERT_EXIT_STATUS(s, 0, WAIT_MS);
    pty_close(s);
}

/* ── tg-cli: the password prompt ─────────────────────────────────────── */

static void test_password_echo_off(void) {
    PtySession *s = start("password", NULL);
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "Password:", WAIT_MS);
    pty_send_str(s, "hunter2");
    ASSERT_EQ_INT(pty_settle(s, 150), 0, "settled");
    ASSERT_SCREEN_NOT_CONTAINS(s, "hunter2");
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "ACCEPTED:hunter2", WAIT_MS);
    FINISH(s);
}

static void test_password_echo_restored(void) {
    PtySession *s = start("password_then_echo", NULL);
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "Password:", WAIT_MS);
    pty_send_str(s, "secret");
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "ACCEPTED:secret", WAIT_MS);
    pty_send_str(s, "CONFIRM");
    ASSERT_WAIT_FOR(s, "CONFIRM", WAIT_MS);       /* echoed: echo is back */
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "ECHO:CONFIRM", WAIT_MS);
    FINISH(s);
}

static void test_password_backspace_and_long(void) {
    PtySession *s = start("password", NULL);
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "Password:", WAIT_MS);
    pty_send_str(s, "bad");
    pty_send_key(s, PTY_KEY_BACK);
    pty_send_key(s, PTY_KEY_BACK);
    pty_send_key(s, PTY_KEY_BACK);
    pty_send_str(s, "hunter2");
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "ACCEPTED:hunter2", WAIT_MS);
    ASSERT_SCREEN_NOT_CONTAINS(s, "badhunter2");
    FINISH(s);

    s = start("password", "big");
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "Password:", WAIT_MS);
    char pw[257];
    memset(pw, 'p', 256);
    pw[256] = '\0';
    pty_send(s, pw, 256);
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "ACCEPTED:pppppppppp", WAIT_MS);
    FINISH(s);
}

static void test_password_ctrl_d_on_empty(void) {
    PtySession *s = start("password", NULL);
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "Password:", WAIT_MS);
    pty_send_key(s, PTY_KEY_CTRL_D);
    ASSERT_WAIT_FOR(s, "ERROR", WAIT_MS);
    ASSERT_EXIT_STATUS(s, 1, WAIT_MS);
    pty_close(s);
}

/* Ctrl-C at the prompt: the process dies of SIGINT as in tg-cli -- and,
 * an addition, the same terminal has its echo back afterwards. */
static void test_password_ctrl_c_restores_echo(void) {
    PtySession *s = pty_open(80, 24);
    ASSERT(s != NULL, "pty_open");
    const char *script =
        /* A trap keeps the shell alive through Ctrl-C (sent to the whole
         * foreground group); the child gets SIGINT with its default action. */
        "trap : INT; \"$0\" password; echo \"status=$?\"; "
        "stty -a | tr ' ;' '\\n\\n' | grep -x -e echo -e -echo -e icanon -e -icanon "
        "| LC_ALL=C sort | tr '\\n' ' '; echo; echo END";
    const char *argv[] = { "/bin/sh", "-c", script, HARNESS, NULL };
    ASSERT(pty_run(s, argv) == 0, "start under sh");
    ASSERT_WAIT_FOR(s, "Password:", WAIT_MS);
    pty_send_str(s, "hun");
    pty_send_key(s, PTY_KEY_CTRL_C);
    ASSERT_WAIT_FOR(s, "END", WAIT_MS);
    ASSERT_SCREEN_CONTAINS(s, "status=130");
    ASSERT_SCREEN_CONTAINS(s, "echo icanon");
    ASSERT_SCREEN_NOT_CONTAINS(s, "-echo");
    ASSERT_EXIT_STATUS(s, 0, WAIT_MS);
    pty_close(s);
}

/* ── tg-cli: keys and wait_key ───────────────────────────────────────── */

static void test_keys(void) {
    /* Tall enough that the key lines never scroll the screen. */
    PtySession *s = pty_open(80, 40);
    ASSERT(s != NULL, "pty_open");
    const char *argv[] = { HARNESS, "read_key", NULL };
    ASSERT(pty_run(s, argv) == 0, "harness launched");
    ASSERT_WAIT_FOR(s, "READY", WAIT_MS);
    static const struct { const char *bytes; const char *name; } keys[] = {
        { "a", "KEY:CHAR:a" }, { "\r", "KEY:ENTER" }, { "\x7f", "KEY:BACK" },
        { "\x01", "KEY:CTRL_A" }, { "\x05", "KEY:CTRL_E" }, { "\x0b", "KEY:CTRL_K" },
        { "\x17", "KEY:CTRL_W" }, { "\033[A", "KEY:UP" }, { "\033[B", "KEY:DOWN" },
        { "\033[C", "KEY:RIGHT" }, { "\033[D", "KEY:LEFT" }, { "\033[H", "KEY:HOME" },
        { "\033[F", "KEY:END" }, { "\033[5~", "KEY:PGUP" }, { "\033[6~", "KEY:PGDN" },
        { "\033[3~", "KEY:DELETE" }, { "\033[1~", "KEY:HOME" }, { "\033[4~", "KEY:END" },
        { "\033[7~", "KEY:HOME" }, { "\033[8~", "KEY:END" }, { "\033OH", "KEY:HOME" },
        { "\033OF", "KEY:END" }, { "\033", "KEY:ESC" }, { "\xC3\xA1", "KEY:CHAR:\xC3\xA1" },
    };
    int row = 1;
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++, row++) {
        pty_send(s, keys[i].bytes, strlen(keys[i].bytes));
        const char *const line[] = { keys[i].name, NULL };
        ASSERT_WAIT_REGION(s, PTY_REGION(row, 0, 1, 40), line, WAIT_MS);
    }
    /* Unknown sequences are consumed whole: nothing reaches the program,
     * not even a "KEY:IGNORE" (tg-cli printed one); the next key is next. */
    pty_send(s, "\033[20m\033OZ", 8);
    pty_send_key(s, PTY_KEY_CTRL_D);
    const char *const last[] = { "KEY:CTRL_D", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(row, 0, 1, 40), last, WAIT_MS);
    ASSERT_EXIT_STATUS(s, 0, WAIT_MS);
    pty_close(s);
}

static void test_wait_key(void) {
    PtySession *s = start("wait_key", NULL);
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "READY", WAIT_MS);
    pty_send_str(s, "x\n");
    ASSERT_WAIT_FOR(s, "WAIT_KEY:READY", WAIT_MS);
    FINISH(s);
    s = start("wait_key", NULL);
    ASSERT(s != NULL, "harness launched");
    ASSERT_WAIT_FOR(s, "WAIT_KEY:TIMEOUT", 8000);
    FINISH(s);
}

static const TestCase tests[] = {
    TEST_CASE(test_utf8_accented_input),
    TEST_CASE(test_utf8_backspace_deletes_code_point),
    TEST_CASE(test_utf8_insert_in_middle),
    TEST_CASE(test_prompt_and_typing),
    TEST_CASE(test_initial_text_confirmed),
    TEST_CASE(test_typed_text_in_result),
    TEST_CASE(test_arrows_home_end),
    TEST_CASE(test_tab_single_match),
    TEST_CASE(test_tab_bar_and_cycling),
    TEST_CASE(test_shift_tab_starts_backward),
    TEST_CASE(test_slash_enters_subdirectory),
    TEST_CASE(test_suffix_preserved),
    TEST_CASE(test_hidden_files_excluded),
    TEST_CASE(test_rl_editing),
    TEST_CASE(test_rl_kill_keys),
    TEST_CASE(test_rl_history),
    TEST_CASE(test_password_echo_off),
    TEST_CASE(test_password_echo_restored),
    TEST_CASE(test_password_backspace_and_long),
    TEST_CASE(test_password_ctrl_d_on_empty),
    TEST_CASE(test_password_ctrl_c_restores_echo),
    TEST_CASE(test_keys),
    TEST_CASE(test_wait_key),
};

int main(int argc, char **argv) {
    if (fs_setup() != 0) {
        printf("FAIL: cannot create the fixture directory %s\n", g_root);
        return 1;
    }
    int rc = RUN_SUITE("compat-pty", tests, 150, 120);
    fs_teardown();
    return rc;
}
