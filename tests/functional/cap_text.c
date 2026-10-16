/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-16 */
/* Capability tests: version, tui_buf, UTF-8 and widths, the input decoder. */

#include "cap_common.h"

#include <fcntl.h>
#include <poll.h>

static void cap_version(void) {
    const char *v = tui_version();
    CHECK(v && v[0] >= '0' && v[0] <= '9' && strchr(v, '.'), "tui_version is dotted");
    CHECK_STR(v, TUIKIT_EXPECTED_VERSION, "tui_version is the built version");
}

static void cap_buf(void) {
    tui_buf b;
    tui_buf_init(&b);
    CHECK_INT(b.len, 0, "init empty");
    CHECK_INT(tui_buf_append(&b, "ab", 2), 0, "append");
    CHECK_INT(tui_buf_puts(&b, "cd"), 0, "puts");
    CHECK_INT(tui_buf_printf(&b, "%d-%s", 42, "x"), 0, "printf");
    CHECK_STR(b.data, "abcd42-x", "content");
    CHECK_INT(b.len, 8, "len");

    int p[2];
    CHECK_INT(pipe(p), 0, "pipe");
    CHECK_INT(tui_buf_write_fd(&b, p[1]), 0, "write_fd");
    CHECK_INT(b.len, 0, "write_fd empties the buffer");
    char rd[16] = {0};
    CHECK_INT(read(p[0], rd, sizeof rd), 8, "bytes reached the pipe");
    CHECK_STR(rd, "abcd42-x", "bytes written");
    CHECK_INT(tui_buf_puts(&b, "zz"), 0, "puts again");
    close(p[0]);
    close(p[1]);
    int bad = tui_buf_write_fd(&b, -1);
    CHECK_INT(bad, -1, "write to a bad fd fails");
    CHECK_INT(errno, EBADF, "with EBADF");

    tui_buf_reset(&b);
    CHECK_INT(b.len, 0, "reset empties");
    CHECK_INT(b.failed, 0, "reset clears failed");
    tui_buf_free(&b);
    CHECK(b.data == NULL || b.len == 0, "free re-initialises");
}

static void cap_utf8(void) {
    uint32_t cp = 0;
    CHECK_INT(tui_utf8_decode("A", 1, &cp), 1, "ascii");
    CHECK_INT(cp, 'A', "ascii cp");
    CHECK_INT(tui_utf8_decode("\xc3\xa9", 2, &cp), 2, "two bytes");
    CHECK_INT(cp, 0xE9, "e acute");
    CHECK_INT(tui_utf8_decode("\xe2\x82\xac", 3, &cp), 3, "three bytes");
    CHECK_INT(cp, 0x20AC, "euro");
    CHECK_INT(tui_utf8_decode("\xf0\x9f\x98\x80", 4, &cp), 4, "four bytes");
    CHECK_INT(cp, 0x1F600, "emoji");
    CHECK_INT(tui_utf8_decode("\xff", 1, &cp), 1, "malformed consumes one byte");
    CHECK_INT(cp, TUI_CP_REPLACEMENT, "malformed is U+FFFD");
    CHECK_INT(tui_utf8_decode("\xc0\x80", 2, &cp), 1, "overlong consumes one");
    CHECK_INT(cp, TUI_CP_REPLACEMENT, "overlong is U+FFFD");
    CHECK_INT(tui_utf8_decode("\xe2\x82", 2, &cp), 2, "cut short consumes what is there");
    CHECK_INT(cp, TUI_CP_REPLACEMENT, "cut short is U+FFFD");
    CHECK_INT(tui_utf8_decode("x", 0, &cp), 0, "n == 0");

    char out[4];
    CHECK_INT(tui_utf8_encode(0x41, out), 1, "encode 1");
    CHECK_INT(tui_utf8_encode(0xE9, out), 2, "encode 2");
    CHECK(memcmp(out, "\xc3\xa9", 2) == 0, "encode e acute");
    CHECK_INT(tui_utf8_encode(0x20AC, out), 3, "encode 3");
    CHECK_INT(tui_utf8_encode(0x1F600, out), 4, "encode 4");
    CHECK_INT(tui_utf8_encode(0x110000, out), 3, "invalid becomes U+FFFD");
    CHECK(memcmp(out, "\xef\xbf\xbd", 3) == 0, "U+FFFD bytes");

    CHECK_INT(tui_utf8_seq_len('a'), 1, "seq 1");
    CHECK_INT(tui_utf8_seq_len(0xC3), 2, "seq 2");
    CHECK_INT(tui_utf8_seq_len(0xE2), 3, "seq 3");
    CHECK_INT(tui_utf8_seq_len(0xF0), 4, "seq 4");
    CHECK_INT(tui_utf8_seq_len(0x80), 1, "bad lead is 1");
}

static void cap_width(void) {
    CHECK_INT(tui_cp_width('a'), 1, "ascii is 1");
    CHECK_INT(tui_cp_width(0x4E2D), 2, "CJK is 2");
    CHECK_INT(tui_cp_width(0x0301), 0, "combining is 0");
    CHECK_INT(tui_cp_width(0x01), -1, "C0 is -1");
    CHECK_INT(tui_cp_width(0x7F), -1, "DEL is -1");
    CHECK_INT(tui_cp_width(0x85), -1, "C1 is -1");
    CHECK_INT(tui_cp_width(0x1F600), 2, "emoji is 2");

    CHECK_INT(tui_cp_sanitize(0x1B), TUI_CP_SANITIZED, "ESC is sanitised");
    CHECK_INT(tui_cp_sanitize(0x9B), TUI_CP_SANITIZED, "8-bit CSI is sanitised");
    CHECK_INT(tui_cp_sanitize(0x7F), TUI_CP_SANITIZED, "DEL is sanitised");
    CHECK_INT(tui_cp_sanitize('a'), 'a', "printable unchanged");
    CHECK_INT(tui_cp_sanitize(0x4E2D), 0x4E2D, "wide unchanged");

    CHECK_INT(tui_str_width("abc"), 3, "str width");
    CHECK_INT(tui_str_width("a\xe4\xb8\xad"), 3, "wide char counts 2");
    CHECK_INT(tui_str_width("a\x1b" "b"), 3, "control counts as its replacement");
    CHECK_INT(tui_str_width(""), 0, "empty");
    CHECK_INT(tui_strn_width("abcdef", 3), 3, "strn width");

    int used = -1;
    CHECK_INT(tui_str_fit("abcdef", 4, &used), 4, "fit bytes");
    CHECK_INT(used, 4, "fit columns");
    CHECK_INT(tui_str_fit("a\xe4\xb8\xad" "b", 2, &used), 1, "wide char is not cut");
    CHECK_INT(used, 1, "columns of the fit");
    CHECK_INT(tui_str_fit("abc", 10, NULL), 3, "all fits, used may be NULL");

    char out[16];
    CHECK_INT(tui_str_fit_ellipsis("hello", 10, out, sizeof out), 5, "fits as is");
    CHECK_STR(out, "hello", "unchanged");
    CHECK_INT(tui_str_fit_ellipsis("hello world", 6, out, sizeof out), 6, "shortened");
    CHECK_STR(out, "hello\xe2\x80\xa6", "ends with an ellipsis");
    char tiny[3];
    CHECK_INT(tui_str_fit_ellipsis("hello world", 6, tiny, sizeof tiny), -1, "output too small");
}

static void cap_decoder(void) {
    tui_decoder d;
    tui_event ev;
    tui_decoder_init(&d);
    CHECK_INT(tui_decoder_pending(&d), 0, "nothing pending");
    CHECK_INT(tui_decoder_feed(&d, "a", 1), 1, "feed accepts");
    CHECK_INT(tui_decoder_next(&d, &ev), 1, "a char");
    CHECK(TUI_EV_IS_CHAR(&ev, 'a'), "plain a");
    CHECK_STR(ev.utf8, "a", "utf8 of the event");
    CHECK_INT(tui_decoder_next(&d, &ev), 0, "no more");

    tui_decoder_feed(&d, "\x1b[A\x1b[1;5C\x03\r\t\x1b[Z", 15);
    CHECK_INT(tui_decoder_next(&d, &ev), 1, "up");
    CHECK_INT(ev.key, TUI_KEY_UP, "up key");
    CHECK_INT(tui_decoder_next(&d, &ev), 1, "ctrl right");
    CHECK_INT(ev.key, TUI_KEY_RIGHT, "right key");
    CHECK_INT(ev.mods, TUI_MOD_CTRL, "ctrl mod");
    CHECK_INT(tui_decoder_next(&d, &ev), 1, "ctrl-c");
    CHECK(TUI_EV_IS_CTRL(&ev, 'c'), "ctrl-c");
    CHECK_INT(tui_decoder_next(&d, &ev), 1, "enter");
    CHECK_INT(ev.key, TUI_KEY_ENTER, "enter key");
    CHECK_INT(tui_decoder_next(&d, &ev), 1, "tab");
    CHECK_INT(ev.key, TUI_KEY_TAB, "tab key");
    CHECK_INT(tui_decoder_next(&d, &ev), 1, "shift-tab");
    CHECK_INT(ev.mods, TUI_MOD_SHIFT, "shift mod");

    tui_decoder_feed(&d, "\x1b", 1);
    CHECK_INT(tui_decoder_next(&d, &ev), 0, "lone ESC is not complete");
    CHECK_INT(tui_decoder_pending(&d), 1, "but pending");
    CHECK_INT(tui_decoder_timeout(&d, &ev), 1, "timeout resolves it");
    CHECK_INT(ev.key, TUI_KEY_ESC, "to Esc");
    CHECK_INT(tui_decoder_pending(&d), 0, "nothing pending after");
    CHECK_INT(tui_decoder_timeout(&d, &ev), 0, "nothing to resolve");

    tui_decoder_feed(&d, "\x1b[200~hi there\x1b[201~", 20);
    CHECK_INT(tui_decoder_next(&d, &ev), 1, "paste");
    CHECK_INT(ev.type, TUI_EV_PASTE, "paste type");
    CHECK_INT(ev.paste_len, 8, "paste length");
    CHECK(memcmp(ev.paste, "hi there", 8) == 0, "paste text");

    tui_decoder_feed(&d, "\xc3", 1);
    CHECK_INT(tui_decoder_timeout(&d, &ev), 1, "half a character at timeout");
    CHECK_INT(ev.cp, TUI_CP_REPLACEMENT, "becomes U+FFFD");

    char big[400];
    memset(big, 'x', sizeof big);
    size_t took = tui_decoder_feed(&d, big, sizeof big);
    CHECK(took > 0 && took < sizeof big, "a full queue accepts fewer bytes");
    tui_decoder_free(&d);
}

static void cap_event_name(void) {
    char nb[64];
    tui_event ev = cap_ev("\x1b[1;5A");
    CHECK_STR(tui_event_name(&ev, nb, sizeof nb), "Ctrl+Up", "Ctrl+Up");
    ev = cap_ev("a");
    CHECK_STR(tui_event_name(&ev, nb, sizeof nb), "a", "plain char");
    ev = cap_ev("\r");
    CHECK_STR(tui_event_name(&ev, nb, sizeof nb), "Enter", "Enter");
    memset(&ev, 0, sizeof ev);
    ev.type = TUI_EV_RESIZE;
    ev.rows = 24; ev.cols = 80;
    CHECK_STR(tui_event_name(&ev, nb, sizeof nb), "Resize 24x80", "Resize");
}

int main(void) {
    cap_version();
    cap_buf();
    cap_utf8();
    cap_width();
    cap_decoder();
    cap_event_name();
    return cap_finish("cap-text");
}
