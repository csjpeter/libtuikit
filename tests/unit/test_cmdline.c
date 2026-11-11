/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-11 */
/* Unit tests of tui_cmdline: opening, editing, history, completion, cancel, the message. */

/*
 * The command line is driven with the keys a user types (through the
 * decoder), the commands are stubs recording what they were given, and the
 * drawn row is fed to libptytest's emulator to compare the text and the
 * colour of the message.
 */

#include "test_helpers.h"
#include "tuikit/tuikit.h"

#include <errno.h>

int g_tests_run = 0;
int g_tests_failed = 0;

typedef struct stub {
    int  calls;
    char args[64];
    int  rc;
} stub;

static int run_stub(void *user, const char *args, char *msg, size_t size) {
    stub *st = user;
    st->calls++;
    snprintf(st->args, sizeof st->args, "%s", args);
    snprintf(msg, size, "ran with [%s]", args);
    return st->rc;
}

/* Types @p keys; returns the result of the last event. */
static tui_cmdline_result type(tui_cmdline *cl, const char *keys) {
    tui_decoder d;
    tui_decoder_init(&d);
    tui_decoder_feed(&d, keys, strlen(keys));
    tui_event ev;
    tui_cmdline_result r = TUI_CMDLINE_IGNORED;
    while (tui_decoder_next(&d, &ev)) r = tui_cmdline_handle(cl, &ev);
    if (tui_decoder_timeout(&d, &ev)) r = tui_cmdline_handle(cl, &ev);
    tui_decoder_free(&d);
    return r;
}

static void setup(tui_cmdline *cl, stub *a, stub *b) {
    tui_cmdline_init(cl);
    ASSERT_EQ_INT(tui_cmdline_register(cl, "reload", "fetch again", run_stub, a), 0, "register");
    ASSERT_EQ_INT(tui_cmdline_register(cl, "restart", "restart the service", run_stub, b), 0, "register");
    ASSERT_EQ_INT(tui_cmdline_register(cl, "quit", NULL, run_stub, a), 0, "register without help");
}

static void test_unbound_until_registered(void) {
    tui_cmdline cl;
    tui_cmdline_init(&cl);
    ASSERT_EQ_INT(tui_cmdline_count(&cl), 0, "no command");
    ASSERT_EQ_INT(type(&cl, ":"), TUI_CMDLINE_IGNORED, ": is unbound without commands");
    ASSERT_EQ_INT(tui_cmdline_is_open(&cl), 0, "closed");
    stub st = { 0, "", 0 };
    tui_cmdline_register(&cl, "go", "go", run_stub, &st);
    ASSERT_EQ_INT(type(&cl, "x"), TUI_CMDLINE_IGNORED, "another key is not ours");
    ASSERT_EQ_INT(type(&cl, "-"), TUI_CMDLINE_IGNORED, "- is the action menu, not ours");
    ASSERT_EQ_INT(type(&cl, ":"), TUI_CMDLINE_EDITING, ": opens it");
    ASSERT_EQ_INT(tui_cmdline_is_open(&cl), 1, "open");
    tui_cmdline_free(&cl);
    ASSERT_EQ_INT(tui_cmdline_count(&cl), 0, "freed");
    ASSERT_EQ_INT(tui_cmdline_handle(NULL, NULL), TUI_CMDLINE_IGNORED, "NULL line");
    ASSERT_EQ_INT(tui_cmdline_is_open(NULL), 0, "NULL is closed");
}

static void test_register_validation(void) {
    tui_cmdline cl;
    tui_cmdline_init(&cl);
    stub st = { 0, "", 0 };
    ASSERT_EQ_INT(tui_cmdline_register(NULL, "a", NULL, run_stub, &st), -1, "no line");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_cmdline_register(&cl, "", NULL, run_stub, &st), -1, "empty name");
    ASSERT_EQ_INT(tui_cmdline_register(&cl, "a b", NULL, run_stub, &st), -1, "blank in the name");
    ASSERT_EQ_INT(tui_cmdline_register(&cl, "a", NULL, NULL, &st), -1, "no handler");
    ASSERT_EQ_INT(tui_cmdline_count(&cl), 0, "none registered");
    tui_cmdline_register(&cl, "a", "first", run_stub, &st);
    tui_cmdline_register(&cl, "a", "second", run_stub, &st);
    ASSERT_EQ_INT(tui_cmdline_count(&cl), 1, "a name again replaces it");
    ASSERT_EQ_STR(tui_cmdline_help(&cl, 0), "second", "the new help");
    ASSERT_EQ_STR(tui_cmdline_name(&cl, 0), "a", "the name");
    ASSERT(tui_cmdline_name(&cl, 1) == NULL && tui_cmdline_name(&cl, -1) == NULL, "out of range");
    ASSERT(tui_cmdline_help(&cl, 1) == NULL && tui_cmdline_help(NULL, 0) == NULL, "no help there");
    ASSERT_EQ_INT(tui_cmdline_count(NULL), 0, "count of NULL");
    tui_cmdline_register(&cl, "nohelp", NULL, run_stub, &st);
    ASSERT_EQ_STR(tui_cmdline_help(&cl, 1), "", "a missing help is empty");
    tui_cmdline_free(&cl);
}

static void test_run_edit_and_cancel(void) {
    tui_cmdline cl;
    stub a = { 0, "", 0 }, b = { 0, "", 0 };
    setup(&cl, &a, &b);
    ASSERT_EQ_INT(type(&cl, ":reload now"), TUI_CMDLINE_EDITING, "typing");
    ASSERT_EQ_INT(type(&cl, "\r"), TUI_CMDLINE_RAN, "Enter runs");
    ASSERT_EQ_INT(tui_cmdline_is_open(&cl), 0, "closed after running");
    ASSERT_EQ_INT(a.calls, 1, "called once");
    ASSERT_EQ_STR(a.args, "now", "the arguments");
    int ok = 0;
    ASSERT_EQ_STR(tui_cmdline_message(&cl, &ok), "ran with [now]", "the handler's message");
    ASSERT_EQ_INT(ok, 1, "success");

    /* Editing: Left, a character inserted, Backspace; leading blanks are skipped. */
    type(&cl, ":");
    type(&cl, "  restrt");
    type(&cl, "\x1b[D\x1b[D" "a");   /* Left Left, a: "restart" */
    type(&cl, "\x1b[F" " x  y");      /* End */
    ASSERT_EQ_INT(type(&cl, "\r"), TUI_CMDLINE_RAN, "run");
    ASSERT_EQ_INT(b.calls, 1, "restart ran");
    ASSERT_EQ_STR(b.args, "x  y", "the rest of the line");
    ASSERT_EQ_STR(tui_cmdline_message(&cl, NULL), "ran with [x  y]", "message, ok pointer optional");

    /* A failing handler is an error message. */
    a.rc = 1;
    type(&cl, ":quit\r");
    ASSERT_EQ_INT(a.calls, 2, "quit ran");
    tui_cmdline_message(&cl, &ok);
    ASSERT_EQ_INT(ok, 0, "failure");

    /* Unknown names, an empty line. */
    type(&cl, ":nosuch 1\r");
    ASSERT_EQ_STR(tui_cmdline_message(&cl, &ok), "unknown command: nosuch", "unknown");
    ASSERT_EQ_INT(ok, 0, "an error");
    ASSERT_EQ_INT(a.calls + b.calls, 3, "nothing ran");
    type(&cl, ":   \r");
    ASSERT_EQ_STR(tui_cmdline_message(&cl, NULL), "", "an empty line leaves no message");

    /* Esc, Ctrl-C, Ctrl-D on an empty line and Backspace on an empty line cancel. */
    static const char *const cancel[] = { "\x1b", "\x03", "\x04", "\x7f" };
    for (int i = 0; i < 4; i++) {
        type(&cl, ":");
        ASSERT_EQ_INT(tui_cmdline_is_open(&cl), 1, "open");
        ASSERT_EQ_INT(type(&cl, cancel[i]), TUI_CMDLINE_CANCELLED, "cancelled");
        ASSERT_EQ_INT(tui_cmdline_is_open(&cl), 0, "closed");
    }
    ASSERT_EQ_INT(a.calls + b.calls, 3, "a cancel runs nothing");
    type(&cl, ":reload");
    ASSERT_EQ_INT(type(&cl, "\x7f"), TUI_CMDLINE_EDITING, "Backspace on text only deletes");
    ASSERT_EQ_INT(type(&cl, "\x1b"), TUI_CMDLINE_CANCELLED, "Esc with text cancels");
    type(&cl, ":");
    ASSERT_EQ_STR(tui_cmdline_message(&cl, NULL), "", "opening clears the message");
    ASSERT_EQ_INT(type(&cl, "\x1b[11~"), TUI_CMDLINE_EDITING, "an unused key is taken, not leaked");
    type(&cl, "\x1b");
    tui_cmdline_free(&cl);
}

static void test_history(void) {
    tui_cmdline cl;
    stub a = { 0, "", 0 }, b = { 0, "", 0 };
    setup(&cl, &a, &b);
    type(&cl, ":reload one\r");
    type(&cl, ":restart two\r");
    type(&cl, ":nosuch\r");
    type(&cl, ":");
    type(&cl, "\x1b[A");   /* Up: the newest */
    ASSERT_EQ_STR(cl.buf, "nosuch", "Up: the last line, an unknown one too");
    type(&cl, "\x1b[A");
    ASSERT_EQ_STR(cl.buf, "restart two", "Up again");
    type(&cl, "\x1b[A");
    ASSERT_EQ_STR(cl.buf, "reload one", "and again");
    type(&cl, "\x1b[B");
    ASSERT_EQ_STR(cl.buf, "restart two", "Down");
    ASSERT_EQ_INT(type(&cl, "\r"), TUI_CMDLINE_RAN, "run it again");
    ASSERT_EQ_INT(b.calls, 2, "restart ran twice");
    ASSERT_EQ_STR(b.args, "two", "with the same arguments");
    type(&cl, ":\x1b[A");
    ASSERT_EQ_STR(cl.buf, "restart two", "it is the newest now");
    type(&cl, "\x1b");
    tui_cmdline_free(&cl);
}

static void test_completion(void) {
    tui_cmdline cl;
    stub a = { 0, "", 0 }, b = { 0, "", 0 };
    setup(&cl, &a, &b);
    type(&cl, ":q\t");
    ASSERT_EQ_STR(cl.buf, "quit ", "a lone match is completed, a blank after it");
    ASSERT_EQ_STR(cl.hint, "", "no candidates to list");
    type(&cl, "\x15" "re\t");   /* Ctrl-U clears */
    ASSERT_EQ_STR(cl.buf, "re", "several matches: the common prefix, no more");
    ASSERT_EQ_STR(cl.hint, "reload restart", "the candidates");
    type(&cl, "s\t");
    ASSERT_EQ_STR(cl.buf, "restart ", "typing narrows it down");
    ASSERT_EQ_STR(cl.hint, "", "the hint is gone");
    type(&cl, "\x15" "zz\t");
    ASSERT_EQ_STR(cl.buf, "zz", "no match: unchanged");
    type(&cl, "\x15" "restart a\t");
    ASSERT_EQ_STR(cl.buf, "restart a", "after the name nothing is completed");
    type(&cl, "\x15\t");
    ASSERT_EQ_STR(cl.buf, "", "an empty line: the commands share no prefix");
    ASSERT_EQ_STR(cl.hint, "reload restart quit", "every command");
    type(&cl, "\x1b");
    tui_cmdline_free(&cl);
}

static void test_render(void) {
    tui_cmdline cl;
    stub a = { 0, "", 0 }, b = { 0, "", 0 };
    setup(&cl, &a, &b);
    tui_screen *scr = tui_screen_new(3, 30);
    tui_buf buf;
    tui_buf_init(&buf);
    PtySession *s = pty_open(30, 3);
    ASSERT(scr && s, "fixtures");
    tui_canvas all = tui_canvas_of(scr);

    tui_cmdline_render(&cl, all);   /* closed, no message: nothing */
    tui_screen_render(scr, &buf);
    pty_feed(s, buf.data, buf.len);
    const char *const empty[] = { "", "", "", NULL };
    ASSERT_SCREEN_EQ(s, empty);

    type(&cl, ":re\t");
    tui_screen_clear(scr, TUI_STYLE_DEFAULT);
    tui_cmdline_render(&cl, all);
    tui_buf_reset(&buf);
    tui_screen_render(scr, &buf);
    pty_feed(s, buf.data, buf.len);
    const char *const open[] = { "", "reload restart", ":re", NULL };
    ASSERT_SCREEN_EQ(s, open);
    int r, c;
    pty_cursor(s, &r, &c);
    ASSERT_EQ_INT(r, 2, "the cursor is on the last row");
    ASSERT_EQ_INT(c, 3, "after the text");

    type(&cl, "s\t\r");   /* restart, no argument */
    tui_screen_clear(scr, TUI_STYLE_DEFAULT);
    tui_cmdline_render(&cl, all);
    tui_buf_reset(&buf);
    tui_screen_render(scr, &buf);
    pty_feed(s, buf.data, buf.len);
    const char *const okrow[] = { "", "", "ran with []", NULL };
    ASSERT_SCREEN_EQ(s, okrow);
    ASSERT_EQ_INT(pty_cell_fg(s, 2, 0), PTY_FG_GREEN, "a success is in the ok colour");

    b.rc = 1;
    type(&cl, ":restart\r");
    tui_screen_clear(scr, TUI_STYLE_DEFAULT);
    tui_cmdline_render(&cl, all);
    tui_buf_reset(&buf);
    tui_screen_render(scr, &buf);
    pty_feed(s, buf.data, buf.len);
    ASSERT_EQ_INT(pty_cell_fg(s, 2, 0), PTY_FG_RED, "a failure is in the error colour");

    /* A one-row canvas has no room for the candidates; a zero-row one draws nothing. */
    type(&cl, ":re\t");
    tui_canvas one = tui_canvas_sub(all, tui_rect_make(2, 0, 1, 30));
    tui_cmdline_render(&cl, one);
    tui_cmdline_render(&cl, tui_canvas_sub(all, tui_rect_make(0, 0, 0, 30)));
    tui_cmdline_render(NULL, all);
    type(&cl, "\x1b");

    tui_cmdline_free(&cl);
    pty_close(s);
    tui_buf_free(&buf);
    tui_screen_free(scr);
}

static const TestCase tests[] = {
    TEST_CASE(test_unbound_until_registered),
    TEST_CASE(test_register_validation),
    TEST_CASE(test_run_edit_and_cancel),
    TEST_CASE(test_history),
    TEST_CASE(test_completion),
    TEST_CASE(test_render),
};

int main(int argc, char **argv) {
    return RUN_SUITE("cmdline", tests, 80, 60);
}
