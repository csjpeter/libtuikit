/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Unit tests of tui_decoder. */

/*
 * Unit tests of tui_decoder.  The expected sequences are written out from
 * xterm's "Control Sequences" document, independently of the decoder.
 *
 * Every case is decoded three ways -- all bytes at once, one byte at a
 * time, and in two halves -- since a sequence split across two reads is
 * the case decoders get wrong.
 */

#include "test_helpers.h"
#include "tuikit/event.h"

int g_tests_run = 0;
int g_tests_failed = 0;

/* Feeds @p in in pieces of @p chunk bytes (0: all at once), calling
 * tui_decoder_timeout() at the end when @p timeout, and returns the event
 * names joined by single spaces. */
static void decode(const char *in, size_t n, size_t chunk, int timeout,
                   char *out, size_t outsz) {
    tui_decoder d;
    tui_decoder_init(&d);
    out[0] = '\0';
    size_t o = 0;
    size_t pos = 0;
    tui_event ev;
    char name[64];
    while (pos < n) {
        size_t step = chunk ? chunk : n - pos;
        if (step > n - pos) step = n - pos;
        size_t took = tui_decoder_feed(&d, in + pos, step);
        pos += took;
        while (tui_decoder_next(&d, &ev)) {
            o += (size_t)snprintf(out + o, outsz - o, "%s%s", o ? " " : "",
                                  tui_event_name(&ev, name, sizeof(name)));
        }
    }
    if (timeout) {
        while (tui_decoder_timeout(&d, &ev)) {
            o += (size_t)snprintf(out + o, outsz - o, "%s%s", o ? " " : "",
                                  tui_event_name(&ev, name, sizeof(name)));
        }
    }
    tui_decoder_free(&d);
}

typedef struct {
    const char *in;
    const char *expect;   /* event names, space separated */
    int         timeout;  /* resolve the end with tui_decoder_timeout() */
} Case;

static const Case cases[] = {
    /* plain keys */
    { "a", "a", 0 },
    { "Ab1 ", "A b 1 Space", 0 },
    { "\r", "Enter", 0 },
    { "\n", "Enter", 0 },
    { "\t", "Tab", 0 },
    { "\x7f", "Backspace", 0 },
    { "\x08", "Backspace", 0 },
    { "\x03", "Ctrl+C", 0 },
    { "\x01\x05\x0b\x17\x04", "Ctrl+A Ctrl+E Ctrl+K Ctrl+W Ctrl+D", 0 },
    { "\x13\x11", "Ctrl+S Ctrl+Q", 0 },
    { "\x1c\x1d\x1f", "Ctrl+\\ Ctrl+] Ctrl+_", 0 },
    { "\x00", "Ctrl+Space", 0 },          /* see the explicit length below */
    /* UTF-8 */
    { "\xC3\xA1\xC5\x91", "\xC3\xA1 \xC5\x91", 0 },              /* á ő */
    { "\xE4\xB8\xAD", "\xE4\xB8\xAD", 0 },                       /* 中 */
    { "\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80", 0 },               /* 😀 */
    { "\xFF" "a", "\xEF\xBF\xBD a", 0 },                         /* bad byte */
    { "\xC2\x9B" "a", "a", 0 },                                  /* C1 dropped */
    { "\xE4\xB8", "\xEF\xBF\xBD", 1 },                           /* half, then timeout */
    /* ESC */
    { "\x1b", "Esc", 1 },
    { "\x1b\x1b", "Esc Esc", 1 },
    { "\x1bx", "Alt+x", 0 },
    { "\x1b\xC3\xA1", "Alt+\xC3\xA1", 0 },
    { "\x1b\x03", "Ctrl+Alt+C", 0 },
    { "\x1b\r", "Alt+Enter", 0 },
    { "\x1b[", "Alt+[", 1 },
    { "\x1bO", "Alt+O", 1 },
    { "\x1b[1;5", "", 1 },                 /* unfinished: dropped */
    /* CSI cursor keys and modifiers (modifyCursorKeys) */
    { "\x1b[A\x1b[B\x1b[C\x1b[D", "Up Down Right Left", 0 },
    { "\x1b[H\x1b[F", "Home End", 0 },
    { "\x1b[1;2A", "Shift+Up", 0 },
    { "\x1b[1;3B", "Alt+Down", 0 },
    { "\x1b[1;5C", "Ctrl+Right", 0 },
    { "\x1b[1;5A" "x", "Ctrl+Up x", 0 },   /* survey defect 2: no "5A" leak */
    { "\x1b[1;6D", "Ctrl+Shift+Left", 0 },
    { "\x1b[1;8H", "Ctrl+Alt+Shift+Home", 0 },
    { "\x1b[1;9A", "Alt+Up", 0 },           /* meta counts as Alt */
    /* SS3 (application cursor mode, F1-F4) -- survey defect 4 */
    { "\x1bOA\x1bOB\x1bOC\x1bOD", "Up Down Right Left", 0 },
    { "\x1bOH\x1bOF", "Home End", 0 },
    { "\x1bOP\x1bOQ\x1bOR\x1bOS", "F1 F2 F3 F4", 0 },
    { "\x1bOM", "Enter", 0 },
    { "\x1bO5A", "Ctrl+Up", 0 },
    { "\x1bOx" "a", "a", 0 },               /* unknown SS3 dropped whole */
    /* tilde keys */
    { "\x1b[1~\x1b[2~\x1b[3~\x1b[4~", "Home Insert Delete End", 0 },
    { "\x1b[5~\x1b[6~\x1b[7~\x1b[8~", "PgUp PgDn Home End", 0 },
    { "\x1b[11~\x1b[12~\x1b[13~\x1b[14~\x1b[15~", "F1 F2 F3 F4 F5", 0 },
    { "\x1b[17~\x1b[18~\x1b[19~\x1b[20~\x1b[21~", "F6 F7 F8 F9 F10", 0 },
    { "\x1b[23~\x1b[24~", "F11 F12", 0 },
    { "\x1b[3;5~", "Ctrl+Delete", 0 },
    { "\x1b[24;2~", "Shift+F12", 0 },
    { "\x1b[99~" "a", "a", 0 },             /* unknown number dropped */
    { "\x1b[1;2P\x1b[1;5S", "Shift+F1 Ctrl+F4", 0 },
    { "\x1b[Z", "Shift+Tab", 0 },
    /* CSI u */
    { "\x1b[97;5u", "Ctrl+A", 0 },
    { "\x1b[13u\x1b[9u\x1b[27u\x1b[127u", "Enter Tab Esc Backspace", 0 },
    { "\x1b[13;2u", "Shift+Enter", 0 },
    { "\x1b[225u", "\xC3\xA1", 0 },
    { "\x1b[27;5u", "Ctrl+Esc", 0 },
    { "\x1b[u\x1b[7u\x1b[155u" "z", "z", 0 },  /* invalid ones dropped */
    /* not keys: consumed whole, nothing leaks */
    { "\x1b[12;40R" "a", "a", 0 },          /* cursor position report */
    { "\x1b[?1;2c" "a", "a", 0 },           /* device attributes */
    { "\x1b[<0;10;5M" "a", "a", 0 },        /* SGR mouse */
    { "\x1b[I\x1b[O" "a", "a", 0 },         /* focus in/out */
    { "\x1b[201~" "a", "a", 0 },            /* stray paste end */
    { "\x1b[1 q" "a", "a", 0 },             /* intermediate byte */
    { "\x1b[1:2A", "Up", 0 },               /* sub-parameter ignored */
    { "\x1b[1?A" "a", "a", 0 },             /* a private marker inside: not a key */
    { "\x1b\xC3", "Alt+\xEF\xBF\xBD", 1 },    /* Alt + half a character, then silence */
    { "\x1b[\x03" "a", "Ctrl+C a", 0 },     /* broken CSI keeps the odd byte */
    { "\x1bO\x03", "Ctrl+C", 0 },
};

static void check_case(const Case *c, size_t idx) {
    size_t n = strlen(c->in);
    if (n == 0) n = 1;                     /* the NUL case */
    char got[512];
    static const size_t chunks[] = { 0, 1, 2 };
    for (size_t k = 0; k < 3; k++) {
        decode(c->in, n, chunks[k], c->timeout, got, sizeof(got));
        char what[128];
        snprintf(what, sizeof(what), "case %zu fed %s", idx,
                 chunks[k] == 0 ? "at once" : chunks[k] == 1 ? "byte by byte" : "in pairs");
        ASSERT_EQ_STR(got, c->expect, what);
    }
}

static void test_table(void) {
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int before = g_tests_failed;
        check_case(&cases[i], i);
        if (g_tests_failed != before) return;
    }
}

static void test_event_fields(void) {
    tui_decoder d;
    tui_decoder_init(&d);
    tui_event ev;
    tui_decoder_feed(&d, "\xC3\xA1\x03\x1bx\x1b[1;5A", 11);
    ASSERT_EQ_INT(tui_decoder_next(&d, &ev), 1, "á decoded");
    ASSERT_EQ_INT(ev.type, TUI_EV_KEY, "type");
    ASSERT_EQ_INT(ev.key, TUI_KEY_CHAR, "key");
    ASSERT_EQ_INT(ev.cp, 0xE1, "code point");
    ASSERT_EQ_STR(ev.utf8, "\xC3\xA1", "utf8 of á (email-cli's terminal_last_utf8)");
    ASSERT_EQ_INT(ev.mods, 0, "no modifier");
    ASSERT_EQ_INT(tui_decoder_next(&d, &ev), 1, "Ctrl-C decoded");
    ASSERT(TUI_EV_IS_CTRL(&ev, 'c'), "TUI_EV_IS_CTRL(ev, 'c')");
    ASSERT_EQ_STR(ev.utf8, "", "a Ctrl chord carries no text");
    ASSERT_EQ_INT(tui_decoder_next(&d, &ev), 1, "Alt+x decoded");
    ASSERT(!TUI_EV_IS_CHAR(&ev, 'x'), "Alt+x is not the plain x");
    ASSERT_EQ_INT(ev.mods, TUI_MOD_ALT, "Alt");
    ASSERT_EQ_STR(ev.utf8, "", "an Alt chord carries no text");
    ASSERT_EQ_INT(tui_decoder_next(&d, &ev), 1, "Ctrl+Up decoded");
    ASSERT_EQ_INT(ev.key, TUI_KEY_UP, "Up");
    ASSERT_EQ_INT(ev.mods, TUI_MOD_CTRL, "Ctrl");
    ASSERT_EQ_INT(tui_decoder_next(&d, &ev), 0, "nothing more");
    ASSERT_EQ_INT(tui_decoder_pending(&d), 0, "nothing pending");
    tui_decoder_feed(&d, "q", 1);
    ASSERT_EQ_INT(tui_decoder_next(&d, &ev), 1, "q decoded");
    ASSERT(TUI_EV_IS_CHAR(&ev, 'q'), "TUI_EV_IS_CHAR(ev, 'q')");
    tui_decoder_free(&d);
}

static void test_pending(void) {
    tui_decoder d;
    tui_decoder_init(&d);
    tui_event ev;
    tui_decoder_feed(&d, "\x1b", 1);
    ASSERT_EQ_INT(tui_decoder_next(&d, &ev), 0, "lone ESC is not yet a key");
    ASSERT_EQ_INT(tui_decoder_pending(&d), 1, "lone ESC is pending");
    tui_decoder_feed(&d, "[B", 2);
    ASSERT_EQ_INT(tui_decoder_next(&d, &ev), 1, "completed by more input");
    ASSERT_EQ_INT(ev.key, TUI_KEY_DOWN, "Down, not Esc");
    ASSERT_EQ_INT(tui_decoder_pending(&d), 0, "nothing pending");
    ASSERT_EQ_INT(tui_decoder_timeout(&d, &ev), 0, "timeout with nothing pending");
    tui_decoder_free(&d);
}

static void test_paste(void) {
    char got[256];
    const char in[] = "\x1b[200~hello \x1b[A world\x1b[201~x";
    for (size_t chunk = 0; chunk <= 3; chunk++) {
        decode(in, sizeof(in) - 1, chunk, 0, got, sizeof(got));
        ASSERT_EQ_STR(got, "Paste 15 bytes x", "paste event then x");
    }
    tui_decoder d;
    tui_decoder_init(&d);
    tui_event ev;
    tui_decoder_feed(&d, in, sizeof(in) - 1);
    ASSERT_EQ_INT(tui_decoder_next(&d, &ev), 1, "paste decoded");
    ASSERT_EQ_INT(ev.type, TUI_EV_PASTE, "type");
    ASSERT_EQ_INT(ev.paste_len, 15, "length");
    ASSERT(memcmp(ev.paste, "hello \x1b[A world", 15) == 0, "escape sequences inside are text");
    tui_decoder_free(&d);

    /* An empty paste, and one that never ends: delivered on timeout. */
    decode("\x1b[200~\x1b[201~", 12, 0, 0, got, sizeof(got));
    ASSERT_EQ_STR(got, "Paste 0 bytes", "empty paste");
    const char *open_end = "\x1b[200~abc\x1b[20";
    decode(open_end, strlen(open_end), 0, 1, got, sizeof(got));
    ASSERT_EQ_STR(got, "Paste 7 bytes", "unterminated paste on timeout, held bytes included");
}

static void test_large_paste_and_queue(void) {
    size_t n = TUI_DECODER_PASTE_MAX + 5000;
    char *in = malloc(n + 12);
    ASSERT(in != NULL, "allocation");
    memcpy(in, "\x1b[200~", 6);
    memset(in + 6, 'z', n);
    memcpy(in + 6 + n, "\x1b[201~", 6);
    tui_decoder d;
    tui_decoder_init(&d);
    tui_event ev;
    size_t pos = 0, total = n + 12;
    int events = 0;
    size_t paste_len = 0;
    while (pos < total) {
        size_t took = tui_decoder_feed(&d, in + pos, total - pos);
        pos += took;
        while (tui_decoder_next(&d, &ev)) {
            events++;
            paste_len = ev.paste_len;
        }
    }
    free(in);
    ASSERT_EQ_INT(events, 1, "one paste event");
    ASSERT_EQ_INT(paste_len, TUI_DECODER_PASTE_MAX, "paste capped at the maximum");
    ASSERT_EQ_INT(d.paste_truncated, 1, "truncation noted");
    tui_decoder_free(&d);

    /* The queue accepts what fits and the rest after draining. */
    tui_decoder_init(&d);
    char many[600];
    memset(many, 'k', sizeof(many));
    size_t took = tui_decoder_feed(&d, many, sizeof(many));
    ASSERT_EQ_INT(took, sizeof(d.in), "first feed fills the queue");
    int keys = 0;
    while (tui_decoder_next(&d, &ev)) keys++;
    took += tui_decoder_feed(&d, many + took, sizeof(many) - took);
    while (tui_decoder_next(&d, &ev)) keys++;
    took += tui_decoder_feed(&d, many + took, sizeof(many) - took);
    while (tui_decoder_next(&d, &ev)) keys++;
    ASSERT_EQ_INT(keys, 600, "every byte became a key");
    tui_decoder_free(&d);
}

static void test_overlong_sequences(void) {
    /* A CSI longer than any key: skipped to its final byte, nothing leaks. */
    char in[200];
    size_t n = 0;
    memcpy(in, "\x1b[", 2);
    n = 2;
    for (int i = 0; i < 100; i++) in[n++] = (i % 3) ? '1' : ';';
    in[n++] = '~';
    in[n++] = 'x';
    char got[128];
    decode(in, n, 0, 0, got, sizeof(got));
    ASSERT_EQ_STR(got, "x", "over-long CSI skipped, fed at once");
    decode(in, n, 1, 0, got, sizeof(got));
    ASSERT_EQ_STR(got, "x", "over-long CSI skipped, byte by byte");
    decode(in, n - 2, 0, 1, got, sizeof(got));
    ASSERT_EQ_STR(got, "", "over-long unfinished CSI dropped on timeout");

    /* A full queue of one incomplete UTF-8 lead after another. */
    tui_decoder d;
    tui_decoder_init(&d);
    tui_event ev;
    tui_decoder_feed(&d, "\x1b[", 2);
    memset(in, '1', sizeof(in));
    size_t fed = 2;
    while (fed < 256) {
        size_t want = 256 - fed;
        if (want > sizeof(in)) want = sizeof(in);
        fed += tui_decoder_feed(&d, in, want);
    }
    while (tui_decoder_next(&d, &ev)) {}
    ASSERT_EQ_INT(d.skip_csi, 1, "skipping a CSI that filled the queue");
    tui_decoder_feed(&d, "Ab", 2);
    ASSERT_EQ_INT(tui_decoder_next(&d, &ev), 1, "after its final byte");
    ASSERT(TUI_EV_IS_CHAR(&ev, 'b'), "the next key is b");
    tui_decoder_free(&d);
}

static void test_names(void) {
    char buf[64];
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ASSERT_EQ_STR(tui_event_name(&ev, buf, sizeof(buf)), "None", "none");
    ev.type = TUI_EV_RESIZE; ev.rows = 24; ev.cols = 80;
    ASSERT_EQ_STR(tui_event_name(&ev, buf, sizeof(buf)), "Resize 24x80", "resize");
    ev.type = TUI_EV_FD; ev.fd_index = 2;
    ASSERT_EQ_STR(tui_event_name(&ev, buf, sizeof(buf)), "Fd 2", "fd");
    ev.type = TUI_EV_HANGUP;
    ASSERT_EQ_STR(tui_event_name(&ev, buf, sizeof(buf)), "Hangup", "hangup");
    ev.type = TUI_EV_KEY; ev.key = TUI_KEY_CHAR; ev.cp = 0x1B; ev.mods = 0;
    ASSERT_EQ_STR(tui_event_name(&ev, buf, sizeof(buf)), "\xC2\xB7", "a control is never printed raw");
    ev.key = (tui_key)0x200000;
    ASSERT_EQ_STR(tui_event_name(&ev, buf, sizeof(buf)), "?", "unknown key");
    ASSERT_EQ_STR(tui_event_name(NULL, buf, sizeof(buf)), "None", "NULL event");
    ASSERT_EQ_STR(tui_event_name(&ev, NULL, 0), "", "no buffer");
}

static void test_null_arguments(void) {
    tui_event ev;
    tui_decoder d;
    tui_decoder_init(&d);
    ASSERT_EQ_INT(tui_decoder_feed(NULL, "a", 1), 0, "feed NULL decoder");
    ASSERT_EQ_INT(tui_decoder_feed(&d, NULL, 1), 0, "feed NULL bytes");
    ASSERT_EQ_INT(tui_decoder_next(NULL, &ev), 0, "next NULL decoder");
    ASSERT_EQ_INT(tui_decoder_next(&d, NULL), 0, "next NULL event");
    ASSERT_EQ_INT(tui_decoder_pending(NULL), 0, "pending NULL");
    ASSERT_EQ_INT(tui_decoder_timeout(NULL, &ev), 0, "timeout NULL");
    tui_decoder_init(NULL);
    tui_decoder_free(NULL);
    tui_decoder_free(&d);
}

static const TestCase tests[] = {
    TEST_CASE(test_table),
    TEST_CASE(test_event_fields),
    TEST_CASE(test_pending),
    TEST_CASE(test_paste),
    TEST_CASE(test_large_paste_and_queue),
    TEST_CASE(test_overlong_sequences),
    TEST_CASE(test_names),
    TEST_CASE(test_null_arguments),
};

int main(int argc, char **argv) {
    return RUN_SUITE("decoder", tests, 250, 60);
}
