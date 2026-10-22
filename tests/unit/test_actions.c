/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-18 */
/* Unit tests of the key table, the action menu and the help made from the table. */

/*
 * Unit tests of tuikit/actions.h (policies/tui-interface-model.md, "Actions
 * that change state: the action menu"): the key codes and their names, the
 * way-out keys, the table and every rule it is refused for (one test each),
 * the menu and the confirmation of a serious action -- keys and the picture,
 * colours included -- and the help overlay made from the same table.
 */

#include "test_helpers.h"
#include "faults.h"
#include "tuikit/actions.h"

#include <errno.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static tui_event key_ev(const char *bytes) {
    tui_decoder dec;
    tui_decoder_init(&dec);
    tui_decoder_feed(&dec, bytes, strlen(bytes));
    tui_event ev;
    memset(&ev, 0, sizeof ev);
    if (!tui_decoder_next(&dec, &ev)) tui_decoder_timeout(&dec, &ev);
    tui_decoder_free(&dec);
    return ev;
}

static tui_action_state menu_send(tui_actionmenu *m, const char *keys) {
    tui_decoder dec;
    tui_decoder_init(&dec);
    tui_decoder_feed(&dec, keys, strlen(keys));
    tui_event ev;
    tui_action_state st = m->state;
    while (tui_decoder_next(&dec, &ev)) st = tui_actionmenu_handle(m, &ev);
    if (tui_decoder_timeout(&dec, &ev)) st = tui_actionmenu_handle(m, &ev);
    tui_decoder_free(&dec);
    return st;
}

#define E(view, key, verb, name, desc, cls, irrev, confirm, id) \
    { view, key, verb, name, desc, cls, irrev, confirm, id }

static const tui_keyentry g_table[] = {
    E("list", TUI_KC_STAR, TUI_VERB_STAR, "Star", NULL, TUI_ACT_ROUTINE, 0, NULL, 1),
    E("list", TUI_KC_DELETE_D, TUI_VERB_DELETE, "Delete", NULL, TUI_ACT_SERIOUS, 1,
      "Delete %s? It cannot be undone.", 2),
    E("list", TUI_KC_NONE, TUI_VERB_NONE, "Restart service", "starts a process", TUI_ACT_SERIOUS, 0,
      "Restart the service of %s?", 3),
    E("list", TUI_KC_NONE, TUI_VERB_NONE, "Rotate secret", NULL, TUI_ACT_RARE, 0, NULL, 4),
    E("list", TUI_KC_RELOAD, TUI_VERB_NONE, "Reload", NULL, TUI_ACT_HARMLESS, 0, NULL, 5),
    E("detail", TUI_KC_READ, TUI_VERB_READ, "Mark read", NULL, TUI_ACT_ROUTINE, 0, NULL, 6),
    E(NULL, TUI_KC_HELP, TUI_VERB_NONE, "Help", NULL, TUI_ACT_HARMLESS, 0, NULL, 7),
};
#define N_TABLE ((int)(sizeof g_table / sizeof g_table[0]))

/* ── Keys ────────────────────────────────────────────────────────────── */

static void test_keycodes_and_names(void) {
    char b[32];
    tui_event ev = key_ev("d");
    ASSERT_EQ_INT(tui_keycode_of(&ev), TUI_KC_DELETE_D, "d");
    ev = key_ev("\x1b[3~");
    ASSERT_EQ_INT(tui_keycode_of(&ev), TUI_KC_DELETE, "Delete");
    ev = key_ev("\x03");
    ASSERT_EQ_INT(tui_keycode_of(&ev), TUI_KC_QUIT, "ctrl-c");
    ev = key_ev("G");
    ASSERT_EQ_INT(tui_keycode_of(&ev), TUI_KC_LAST, "G");
    ev = key_ev("\x1b" "d");
    ASSERT_EQ_INT(tui_keycode_of(&ev), TUI_KC_NONE, "Alt+d is no key of the table");
    ev = key_ev("\x1b[1;5A");
    ASSERT_EQ_INT(tui_keycode_of(&ev), TUI_KC_NONE, "ctrl-Up is no key of the table");
    ev = key_ev("\x1b[A");
    ASSERT_EQ_INT(tui_keycode_of(&ev), TUI_KEY_UP, "Up");
    ASSERT_EQ_INT(tui_keycode_of(NULL), TUI_KC_NONE, "no event");
    tui_event rs = { .type = TUI_EV_RESIZE };
    ASSERT_EQ_INT(tui_keycode_of(&rs), TUI_KC_NONE, "not a key event");

    ASSERT_EQ_INT(tui_keycode_name(TUI_KC_DELETE_D, b, sizeof b), 1, "length of d");
    ASSERT_EQ_STR(b, "d", "d");
    tui_keycode_name(TUI_KC_DELETE, b, sizeof b);
    ASSERT_EQ_STR(b, "Delete", "Delete");
    tui_keycode_name(TUI_KC_QUIT, b, sizeof b);
    ASSERT_EQ_STR(b, "ctrl-c", "ctrl-c");
    tui_keycode_name(TUI_KC_MARK, b, sizeof b);
    ASSERT_EQ_STR(b, "Space", "Space");
    tui_keycode_name(TUI_KC_ESC, b, sizeof b);
    ASSERT_EQ_STR(b, "Esc", "Esc");
    tui_keycode_name(TUI_KC_ENTER, b, sizeof b);
    ASSERT_EQ_STR(b, "Enter", "Enter");
    tui_keycode_name(TUI_KEY_F5, b, sizeof b);
    ASSERT_EQ_STR(b, "F5", "F5");
    tui_keycode_name(TUI_KC_STAR, b, sizeof b);
    ASSERT_EQ_STR(b, "*", "star");
    tui_keycode_name(0xE9, b, sizeof b);                       /* 2-byte UTF-8 */
    ASSERT_EQ_STR(b, "\xC3\xA9", "e acute");
    tui_keycode_name(0x4E2D, b, sizeof b);                     /* 3 */
    ASSERT_EQ_STR(b, "\xE4\xB8\xAD", "CJK");
    tui_keycode_name(0x1F600, b, sizeof b);                    /* 4 */
    ASSERT_EQ_STR(b, "\xF0\x9F\x98\x80", "emoji");
    tui_keycode_name(TUI_KC_NONE, b, sizeof b);
    ASSERT_EQ_STR(b, "", "no key has no name");
    tui_keycode_name(0x7FFFFFFF & ~TUI_KC_CTRL, b, sizeof b);
    ASSERT_EQ_STR(b, "", "an unknown code has no name");
    ASSERT_EQ_INT(tui_keycode_name(TUI_KC_DELETE, NULL, 0), 6, "length without a buffer");
}

static void test_registry_and_reserved(void) {
    ASSERT_EQ_INT(tui_registry_verb('*'), TUI_VERB_STAR, "star");
    ASSERT_EQ_INT(tui_registry_verb('!'), TUI_VERB_JUNK, "junk");
    ASSERT_EQ_INT(tui_registry_verb('+'), TUI_VERB_READ, "read");
    ASSERT_EQ_INT(tui_registry_verb('-'), TUI_VERB_UNREAD, "unread");
    ASSERT_EQ_INT(tui_registry_verb('d'), TUI_VERB_DELETE, "d");
    ASSERT_EQ_INT(tui_registry_verb(TUI_KEY_DELETE), TUI_VERB_DELETE, "Delete");
    ASSERT_EQ_INT(tui_registry_verb('n'), TUI_VERB_NEW, "new");
    ASSERT_EQ_INT(tui_registry_verb('e'), TUI_VERB_EDIT, "edit");
    ASSERT_EQ_INT(tui_registry_verb('t'), TUI_VERB_MOVE, "move");
    ASSERT_EQ_INT(tui_registry_verb('m'), TUI_VERB_MESSAGE, "message");
    ASSERT_EQ_INT(tui_registry_verb('r'), TUI_VERB_NONE, "r is not in the registry");
    ASSERT_EQ_INT(tui_registry_verb('x'), TUI_VERB_NONE, "x is not in the registry");

    const char *reserved = "rsfpocauilhx/jkqgGU?:";
    for (const char *p = reserved; *p; p++) {
        ASSERT_EQ_INT(tui_key_reserved((unsigned char)*p), 1, "reserved");
        ASSERT_EQ_INT(tui_registry_verb((unsigned char)*p), TUI_VERB_NONE, "reserved is not state-changing");
    }
    ASSERT_EQ_INT(tui_key_reserved('d'), 0, "d is not reserved");
    ASSERT_EQ_INT(tui_key_reserved('*'), 0, "star is not reserved");
    ASSERT_EQ_INT(tui_key_reserved(0), 0, "no key");
    ASSERT_EQ_INT(tui_key_reserved(TUI_KEY_DELETE), 0, "Delete is not reserved");
}

static void test_way_out(void) {
    tui_event bs = key_ev("\x7f"), q = key_ev("q"), esc = key_ev("\x1b"), cc = key_ev("\x03"), x = key_ev("x");
    ASSERT_EQ_INT(tui_nav_key(&q, 0), TUI_NAV_NONE, "q on the top level does not quit");
    ASSERT_EQ_INT(tui_nav_key(&q, 2), TUI_NAV_BACK, "q goes back");
    ASSERT_EQ_INT(tui_nav_key(&bs, 0), TUI_NAV_NONE, "Backspace on the top level does not quit");
    ASSERT_EQ_INT(tui_nav_key(&bs, 2), TUI_NAV_BACK, "Backspace goes back");
    ASSERT_EQ_INT(TUI_KC_BACKSPACE, (int)TUI_KEY_BACKSPACE, "the named constant");
    ASSERT_EQ_INT(tui_nav_key(&esc, 0), TUI_NAV_QUIT, "Esc on the top level quits");
    ASSERT_EQ_INT(tui_nav_key(&esc, 1), TUI_NAV_BACK, "Esc goes back");
    ASSERT_EQ_INT(tui_nav_key(&cc, 0), TUI_NAV_QUIT, "ctrl-c quits on the top level");
    ASSERT_EQ_INT(tui_nav_key(&cc, 3), TUI_NAV_QUIT, "ctrl-c quits from anywhere");
    ASSERT_EQ_INT(tui_nav_key(&x, 0), TUI_NAV_NONE, "other keys are not the way out");
    ASSERT_EQ_INT(tui_nav_key(NULL, 0), TUI_NAV_NONE, "no event");
}

/* ── The table ───────────────────────────────────────────────────────── */

static void test_table_accepts_the_policy(void) {
    tui_keytable t;
    char why[128];
    ASSERT_EQ_INT(tui_keytable_init(&t, g_table, N_TABLE, why, sizeof why), 0, "a good table");
    ASSERT_EQ_STR(why, "", "no reason");
    ASSERT_EQ_INT(t.n, N_TABLE, "taken over");
    ASSERT_EQ_INT(tui_keytable_init(&t, g_table, N_TABLE, NULL, 0), 0, "no reason buffer needed");
    ASSERT_EQ_INT(tui_keytable_init(&t, NULL, 0, NULL, 0), 0, "an empty table");
    errno = 0;
    ASSERT_EQ_INT(tui_keytable_init(NULL, g_table, 1, NULL, 0), -1, "no table");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_keytable_init(&t, NULL, 1, NULL, 0), -1, "no entries");
    ASSERT_EQ_INT(tui_keytable_init(&t, g_table, -1, NULL, 0), -1, "negative count");

    /* Every registry key is accepted for its own verb, a direct Delete too. */
    static const tui_keyentry reg[] = {
        E("l", TUI_KC_STAR, TUI_VERB_STAR, "Star", NULL, TUI_ACT_ROUTINE, 0, NULL, 1),
        E("l", TUI_KC_JUNK, TUI_VERB_JUNK, "Junk", NULL, TUI_ACT_ROUTINE, 0, NULL, 2),
        E("l", TUI_KC_READ, TUI_VERB_READ, "Read", NULL, TUI_ACT_ROUTINE, 0, NULL, 3),
        E("l", TUI_KC_UNREAD, TUI_VERB_UNREAD, "Unread", NULL, TUI_ACT_ROUTINE, 0, NULL, 4),
        E("l", TUI_KC_DELETE, TUI_VERB_DELETE, "Delete", NULL, TUI_ACT_SERIOUS, 1, "Delete %s?", 5),
        E("l", TUI_KC_NEW, TUI_VERB_NEW, "New", NULL, TUI_ACT_ROUTINE, 0, NULL, 6),
        E("l", TUI_KC_EDIT, TUI_VERB_EDIT, "Edit", NULL, TUI_ACT_ROUTINE, 0, NULL, 7),
        E("l", TUI_KC_MOVE, TUI_VERB_MOVE, "Move", NULL, TUI_ACT_ROUTINE, 0, NULL, 8),
        E("l", TUI_KC_MESSAGE, TUI_VERB_MESSAGE, "Message", NULL, TUI_ACT_ROUTINE, 0, NULL, 9),
    };
    ASSERT_EQ_INT(tui_keytable_init(&t, reg, 9, why, sizeof why), 0, "the registry keys");
    /* The same key in two different views is not a clash. */
    static const tui_keyentry two[] = {
        E("a", TUI_KC_STAR, TUI_VERB_STAR, "Star", NULL, TUI_ACT_ROUTINE, 0, NULL, 1),
        E("b", TUI_KC_STAR, TUI_VERB_STAR, "Star", NULL, TUI_ACT_ROUTINE, 0, NULL, 2),
    };
    ASSERT_EQ_INT(tui_keytable_init(&t, two, 2, why, sizeof why), 0, "one key in two views");
}

static void test_table_refuses_unregistered_state_change(void) {
    static const tui_keyentry bad[] = {
        E("list", 'R', TUI_VERB_NONE, "Restart", NULL, TUI_ACT_ROUTINE, 0, NULL, 1) };
    tui_keytable t;
    char why[160];
    errno = 0;
    ASSERT_EQ_INT(tui_keytable_init(&t, bad, 1, why, sizeof why), -1, "a state change on R");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT(strstr(why, "not in the registry") != NULL, "the reason names the registry");
    ASSERT(strstr(why, "Restart") != NULL, "the reason names the action");
    /* A serious action on the reserved harmless 'r' (restart!) is the very case. */
    static const tui_keyentry r[] = {
        E("list", 'r', TUI_VERB_NONE, "Restart", NULL, TUI_ACT_SERIOUS, 0, "Restart %s?", 1) };
    ASSERT_EQ_INT(tui_keytable_init(&t, r, 1, why, sizeof why), -1, "restart on r");
    /* The registry key of another verb. */
    static const tui_keyentry v[] = {
        E("list", 'd', TUI_VERB_STAR, "Star", NULL, TUI_ACT_ROUTINE, 0, NULL, 1) };
    ASSERT_EQ_INT(tui_keytable_init(&t, v, 1, why, sizeof why), -1, "d for the star verb");
    ASSERT(strstr(why, "another verb") != NULL, "the reason");
    /* A harmless action on a state-changing key. */
    static const tui_keyentry h[] = {
        E("list", 'd', TUI_VERB_NONE, "Show details", NULL, TUI_ACT_HARMLESS, 0, NULL, 1) };
    ASSERT_EQ_INT(tui_keytable_init(&t, h, 1, why, sizeof why), -1, "a harmless action on d");
    ASSERT(strstr(why, "harmless") != NULL, "the reason");
}

static void test_table_refuses_serious_without_confirmation(void) {
    tui_keytable t;
    char why[160];
    static const tui_keyentry none[] = {
        E("list", TUI_KC_DELETE_D, TUI_VERB_DELETE, "Delete", NULL, TUI_ACT_SERIOUS, 0, NULL, 1) };
    ASSERT_EQ_INT(tui_keytable_init(&t, none, 1, why, sizeof why), -1, "serious, no confirmation");
    ASSERT(strstr(why, "confirmation") != NULL, "the reason");
    static const tui_keyentry notarget[] = {
        E("list", TUI_KC_DELETE_D, TUI_VERB_DELETE, "Delete", NULL, TUI_ACT_SERIOUS, 0,
          "Are you sure?", 1) };
    ASSERT_EQ_INT(tui_keytable_init(&t, notarget, 1, why, sizeof why), -1,
                  "a confirmation that does not name the target");
    static const tui_keyentry menu_only[] = {
        E("list", TUI_KC_NONE, TUI_VERB_NONE, "Deploy", NULL, TUI_ACT_SERIOUS, 0, NULL, 1) };
    ASSERT_EQ_INT(tui_keytable_init(&t, menu_only, 1, why, sizeof why), -1,
                  "a serious menu action needs it too");
    static const tui_keyentry irrev[] = {
        E("list", TUI_KC_NONE, TUI_VERB_NONE, "Erase", NULL, TUI_ACT_RARE, 1, NULL, 1) };
    ASSERT_EQ_INT(tui_keytable_init(&t, irrev, 1, why, sizeof why), -1,
                  "an irreversible rare action needs it");
}

static void test_table_refuses_rare_with_direct_key(void) {
    tui_keytable t;
    char why[160];
    static const tui_keyentry rare[] = {
        E("list", '*', TUI_VERB_STAR, "Rotate secret", NULL, TUI_ACT_RARE, 0, NULL, 1) };
    ASSERT_EQ_INT(tui_keytable_init(&t, rare, 1, why, sizeof why), -1, "rare with a direct key");
    ASSERT(strstr(why, "menu") != NULL, "the reason names the menu");
}

static void test_table_refuses_malformed(void) {
    tui_keytable t;
    char why[160];
    static const tui_keyentry dup[] = {
        E("list", '*', TUI_VERB_STAR, "Star", NULL, TUI_ACT_ROUTINE, 0, NULL, 1),
        E(NULL, '*', TUI_VERB_STAR, "Star again", NULL, TUI_ACT_ROUTINE, 0, NULL, 2) };
    ASSERT_EQ_INT(tui_keytable_init(&t, dup, 2, why, sizeof why), -1, "a key bound twice");
    ASSERT(strstr(why, "twice") != NULL, "the reason");
    static const tui_keyentry noname[] = {
        E("list", '*', TUI_VERB_STAR, NULL, NULL, TUI_ACT_ROUTINE, 0, NULL, 1) };
    ASSERT_EQ_INT(tui_keytable_init(&t, noname, 1, why, sizeof why), -1, "no name");
    static const tui_keyentry empty[] = {
        E("list", '*', TUI_VERB_STAR, "", NULL, TUI_ACT_ROUTINE, 0, NULL, 1) };
    ASSERT_EQ_INT(tui_keytable_init(&t, empty, 1, why, sizeof why), -1, "empty name");
    static const tui_keyentry cls[] = {
        E("list", '*', TUI_VERB_STAR, "Star", NULL, (tui_action_class)9, 0, NULL, 1) };
    ASSERT_EQ_INT(tui_keytable_init(&t, cls, 1, why, sizeof why), -1, "unknown class");
    ASSERT_EQ_INT(tui_keytable_init(&t, noname, 1, NULL, 0), -1, "refused without a reason buffer");
}

static void test_find(void) {
    tui_keytable t;
    ASSERT_EQ_INT(tui_keytable_init(&t, g_table, N_TABLE, NULL, 0), 0, "table");
    tui_event star = key_ev("*"), rd = key_ev("+"), help = key_ev("?"), x = key_ev("x");
    ASSERT_EQ_INT(tui_keytable_find(&t, "list", &star), 0, "star in the list");
    ASSERT_EQ_INT(tui_keytable_find(&t, "detail", &star), -1, "not in the detail view");
    ASSERT_EQ_INT(tui_keytable_find(&t, "detail", &rd), 5, "read in the detail view");
    ASSERT_EQ_INT(tui_keytable_find(&t, "list", &rd), -1, "not in the list");
    ASSERT_EQ_INT(tui_keytable_find(&t, "list", &help), 6, "a key of every view");
    ASSERT_EQ_INT(tui_keytable_find(&t, "detail", &help), 6, "in the other view too");
    ASSERT_EQ_INT(tui_keytable_find(&t, NULL, &help), 6, "with no view, the common ones");
    ASSERT_EQ_INT(tui_keytable_find(&t, NULL, &star), -1, "no view: a view's key is not found");
    ASSERT_EQ_INT(tui_keytable_find(&t, "list", &x), -1, "an unbound key");
    ASSERT_EQ_INT(tui_keytable_find(NULL, "list", &star), -1, "no table");
    tui_event rs = { .type = TUI_EV_RESIZE };
    ASSERT_EQ_INT(tui_keytable_find(&t, "list", &rs), -1, "not a key");
    /* Menu-only entries are never found by a key. */
    for (int c = 1; c < 128; c++) {
        tui_event e = { .type = TUI_EV_KEY, .key = TUI_KEY_CHAR, .cp = (uint32_t)c };
        int i = tui_keytable_find(&t, "list", &e);
        ASSERT(i < 0 || t.entries[i].key == (tui_keycode)c, "found entries have that key");
    }
}

/* ── The menu ────────────────────────────────────────────────────────── */

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

static void fx_draw(Fx *f, tui_actionmenu *m) {
    tui_canvas all = tui_canvas_of(f->scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_actionmenu_render(m, all);
    tui_buf_reset(&f->buf);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
}

static void fx_close(Fx *f) {
    tui_screen_free(f->scr);
    tui_buf_free(&f->buf);
    if (f->s) pty_close(f->s);
}

static const PtyStyleKey legend[] = {
    { '.', PTY_ATTR_NONE, 0, 0 },
    { 'A', PTY_ATTR_BOLD, PTY_FG_CYAN, PTY_BG_DEFAULT },      /* the frame and the title */
    { 'S', PTY_ATTR_BOLD | PTY_ATTR_REVERSE, 0, 0 },          /* the focused button */
    { 'B', PTY_ATTR_NONE, 97, PTY_COLOR_IDX(17) },            /* the selected row: 256 colours */
    { 'M', PTY_ATTR_BOLD, 97, PTY_COLOR_IDX(17) },            /* its '>' and its quick key */
    { 'Q', PTY_ATTR_BOLD, PTY_FG_CYAN, PTY_BG_DEFAULT },      /* a quick key: the accent colour */
    { 0, 0, 0, 0 } };

static void test_menu_lists_the_actions_with_their_keys(void) {
    tui_keytable t;
    ASSERT_EQ_INT(tui_keytable_init(&t, g_table, N_TABLE, NULL, 0), 0, "table");
    tui_actionmenu m;
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "list", "mail 7"), 0, "open");
    ASSERT_EQ_INT(m.state, TUI_ACTION_OPEN, "open");
    Fx f;
    ASSERT(fx_open(&f, 12, 60) == 0, "fixtures");
    fx_draw(&f, &m);
    /* Every action of the view but the harmless Reload, the direct key beside it. */
    const char *const expect[] = {
        "", "", "",
        "       ┌─ Actions ──────────────────────────────────┐",
        "       │ > s  *  Star                               │",
        "       │   d  d  Delete                             │",
        "       │   t     Restart service - starts a process │",
        "       │   r     Rotate secret                      │",
        "       └────────────────────────────────────────────┘",
        "", "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    const char *const map[] = {
        "A.MBMBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.A",    /* the selected row */
        "A...Q........................................A",       /* the next one: its quick key */
        NULL };
    ASSERT_REGION_STYLE(f.s, PTY_REGION(4, 7, 2, 46), map, legend);
    ASSERT_EQ_STR(pty_cell_text(f.s, 4, 9), ">", "the selection marker");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 4, 13), PTY_COLOR_IDX(17), "the selected row: dark blue");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 5, 13), PTY_BG_DEFAULT, "the other rows: the terminal's own");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 3, 9), PTY_FG_CYAN, "the frame is the accent");
    /* Movement keys move; Esc cancels. */
    ASSERT_EQ_INT(menu_send(&m, "j"), TUI_ACTION_OPEN, "j moves");
    fx_draw(&f, &m);
    ASSERT_EQ_STR(pty_cell_text(f.s, 5, 9), ">", "the marker follows");
    ASSERT_EQ_INT(menu_send(&m, "\x1b"), TUI_ACTION_CANCELLED, "Esc cancels");
    ASSERT_EQ_INT(menu_send(&m, "\r"), TUI_ACTION_CANCELLED, "a cancelled menu stays so");
    tui_actionmenu_free(&m);

    /* The other view has its own actions; a table with none shows an empty menu. */
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "detail", NULL), 0, "detail");
    fx_draw(&f, &m);
    ASSERT(pty_screen_contains(f.s, "+  Mark read"), "the read key");
    ASSERT(!pty_screen_contains(f.s, "Star"), "not another view's action");
    tui_actionmenu_free(&m);
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "nowhere", NULL), 0, "no actions");
    ASSERT_EQ_INT(menu_send(&m, "\r"), TUI_ACTION_OPEN, "Enter on an empty menu does nothing");
    ASSERT_EQ_INT(menu_send(&m, "\x03"), TUI_ACTION_CANCELLED, "ctrl-c cancels");
    tui_actionmenu_free(&m);
    fx_close(&f);
}

static void test_menu_routine_action_is_chosen_without_asking(void) {
    tui_keytable t;
    tui_keytable_init(&t, g_table, N_TABLE, NULL, 0);
    tui_actionmenu m;
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "list", "mail 7"), 0, "open");
    ASSERT_EQ_INT(menu_send(&m, "\r"), TUI_ACTION_CHOSEN, "Enter on Star chooses it");
    ASSERT_EQ_INT(m.entry, 0, "entry 0");
    ASSERT_EQ_INT(t.entries[m.entry].id, 1, "the program's id");
    ASSERT_EQ_INT(menu_send(&m, "j"), TUI_ACTION_CHOSEN, "chosen stays chosen");
    tui_actionmenu_free(&m);
    /* A rare action without a question is chosen at once as well. */
    tui_actionmenu_open(&m, &t, "list", "mail 7");
    ASSERT_EQ_INT(menu_send(&m, "jjj"), TUI_ACTION_OPEN, "down to Rotate secret");
    ASSERT_EQ_INT(menu_send(&m, "\r"), TUI_ACTION_CHOSEN, "chosen");
    ASSERT_EQ_INT(t.entries[m.entry].id, 4, "Rotate secret");
    tui_actionmenu_free(&m);
}

static void test_menu_serious_action_asks_naming_the_target(void) {
    tui_keytable t;
    tui_keytable_init(&t, g_table, N_TABLE, NULL, 0);
    tui_actionmenu m;
    tui_actionmenu_open(&m, &t, "list", "mail 7");
    ASSERT_EQ_INT(menu_send(&m, "j\r"), TUI_ACTION_OPEN, "Delete asks first");
    Fx f;
    ASSERT(fx_open(&f, 12, 60) == 0, "fixtures");
    fx_draw(&f, &m);
    const char *const expect[] = {
        "", "", "",
        "          ┌─ Delete ────────────────────────────┐",
        "          │ Delete mail 7? It cannot be undone. │",
        "          │                                     │",
        "          │           [ Yes ]  [ No ]           │",
        "          └─────────────────────────────────────┘",
        "", "", "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    ASSERT(pty_screen_contains(f.s, "mail 7"), "the target is named");
    /* The default is No, in the focus style; Yes is plain. */
    int r = 6, c = 31;                                       /* the "[ No ]" button */
    char d[160];
    pty_cell_describe(f.s, r, c, d, sizeof d);
    ASSERT(strstr(d, "reverse") != NULL, "No is focused");
    pty_cell_describe(f.s, r, c - 9, d, sizeof d);
    ASSERT(strstr(d, "reverse") == NULL, "Yes is not");
    ASSERT_EQ_INT(menu_send(&m, "\r"), TUI_ACTION_CANCELLED, "Enter on the default never performs it");
    tui_actionmenu_free(&m);

    /* Left or Tab moves to Yes, Enter then performs; y performs at once. */
    tui_actionmenu_open(&m, &t, "list", "mail 7");
    menu_send(&m, "j\r");
    ASSERT_EQ_INT(menu_send(&m, "\t"), TUI_ACTION_OPEN, "Tab");
    ASSERT_EQ_INT(menu_send(&m, "\r"), TUI_ACTION_CHOSEN, "Enter on Yes performs");
    ASSERT_EQ_INT(t.entries[m.entry].id, 2, "Delete");
    tui_actionmenu_free(&m);
    tui_actionmenu_open(&m, &t, "list", "mail 7");
    menu_send(&m, "jj\r");                                  /* Restart service */
    ASSERT_EQ_INT(menu_send(&m, "y"), TUI_ACTION_CHOSEN, "y performs");
    ASSERT_EQ_INT(t.entries[m.entry].id, 3, "Restart service");
    tui_actionmenu_free(&m);
    tui_actionmenu_open(&m, &t, "list", "mail 7");
    menu_send(&m, "jj\r");
    fx_draw(&f, &m);
    ASSERT(pty_screen_contains(f.s, "Restart the service of mail 7?"), "its own question, target named");
    ASSERT_EQ_INT(menu_send(&m, "n"), TUI_ACTION_CANCELLED, "n refuses");
    tui_actionmenu_free(&m);
    tui_actionmenu_open(&m, &t, "list", "mail 7");
    menu_send(&m, "j\r");
    ASSERT_EQ_INT(menu_send(&m, "\x1b"), TUI_ACTION_CANCELLED, "Esc refuses");
    tui_actionmenu_free(&m);
    tui_actionmenu_open(&m, &t, "list", "mail 7");
    menu_send(&m, "j\r");
    ASSERT_EQ_INT(menu_send(&m, "\x03"), TUI_ACTION_CANCELLED, "ctrl-c refuses");
    tui_actionmenu_free(&m);
    fx_close(&f);
}

static void test_run_by_direct_key(void) {
    tui_keytable t;
    tui_keytable_init(&t, g_table, N_TABLE, NULL, 0);
    tui_actionmenu m;
    ASSERT_EQ_INT(tui_actionmenu_run(&m, &t, 0, "mail 7"), 0, "star");
    ASSERT_EQ_INT(m.state, TUI_ACTION_CHOSEN, "a routine action needs no question");
    ASSERT_EQ_INT(m.entry, 0, "entry");
    tui_actionmenu_free(&m);

    ASSERT_EQ_INT(tui_actionmenu_run(&m, &t, 1, "mail 7"), 0, "delete");
    ASSERT_EQ_INT(m.state, TUI_ACTION_OPEN, "a serious action asks");
    Fx f;
    ASSERT(fx_open(&f, 12, 60) == 0, "fixtures");
    fx_draw(&f, &m);
    ASSERT(pty_screen_contains(f.s, "Delete mail 7? It cannot be undone."), "the target is named");
    ASSERT_EQ_INT(menu_send(&m, "\r"), TUI_ACTION_CANCELLED, "Enter on the default does nothing");
    tui_actionmenu_free(&m);
    tui_actionmenu_run(&m, &t, 1, "mail 7");
    ASSERT_EQ_INT(menu_send(&m, "y"), TUI_ACTION_CHOSEN, "y performs");
    tui_actionmenu_free(&m);
    tui_actionmenu_run(&m, &t, 1, NULL);                    /* no target: the question still reads */
    fx_draw(&f, &m);
    ASSERT(pty_screen_contains(f.s, "Delete ? It cannot"), "an empty target");
    tui_actionmenu_free(&m);
    fx_close(&f);

    errno = 0;
    ASSERT_EQ_INT(tui_actionmenu_run(&m, &t, N_TABLE, NULL), -1, "no such entry");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_actionmenu_run(&m, &t, -1, NULL), -1, "negative entry");
    ASSERT_EQ_INT(tui_actionmenu_run(NULL, &t, 0, NULL), -1, "no menu");
    ASSERT_EQ_INT(tui_actionmenu_run(&m, NULL, 0, NULL), -1, "no table");
    ASSERT_EQ_INT(tui_actionmenu_open(NULL, &t, "list", NULL), -1, "open: no menu");
    ASSERT_EQ_INT(tui_actionmenu_open(&m, NULL, "list", NULL), -1, "open: no table");
    ASSERT_EQ_INT(tui_actionmenu_handle(NULL, NULL), TUI_ACTION_CANCELLED, "handle: no menu");
    tui_actionmenu_free(NULL);
    tui_actionmenu_render(NULL, tui_canvas_of(NULL));
}

static void test_menu_256_colours_and_8_colours(void) {
    tui_keytable t;
    tui_keytable_init(&t, g_table, N_TABLE, NULL, 0);
    char *term = getenv("TERM") ? strdup(getenv("TERM")) : NULL;
    tui_actionmenu m;
    Fx f;
    ASSERT(fx_open(&f, 12, 60) == 0, "fixtures");
    setenv("TERM", "linux", 1);                              /* 8 colours: bold only */
    tui_actionmenu_open(&m, &t, "list", "x");
    fx_draw(&f, &m);
    ASSERT_EQ_INT(pty_cell_bg(f.s, 4, 13), PTY_BG_DEFAULT, "8 colours: no background");
    char d[160];
    pty_cell_describe(f.s, 4, 13, d, sizeof d);
    ASSERT(strstr(d, "bold") != NULL, "8 colours: the selection is bold");
    ASSERT_EQ_STR(pty_cell_text(f.s, 4, 9), ">", "the marker is there as well");
    tui_actionmenu_free(&m);
    if (term) setenv("TERM", term, 1);
    else unsetenv("TERM");
    free(term);
    fx_close(&f);
}

/* ── The help ────────────────────────────────────────────────────────── */

/* The help overlay and the key handler read one table: every row of the
 * help is a key the table binds in that view to the action it names, and
 * every bound key has its row. */
static void test_help_matches_the_handler(void) {
    tui_keytable t;
    ASSERT_EQ_INT(tui_keytable_init(&t, g_table, N_TABLE, NULL, 0), 0, "table");
    const char *views[] = { "list", "detail", "other" };
    for (int v = 0; v < 3; v++) {
        tui_keyhelp h;
        ASSERT_EQ_INT(tui_keyhelp_open(&h, &t, views[v], "Shortcuts"), 0, "open");
        int row = 0;
        for (int i = 0; i < t.n; i++) {
            const tui_keyentry *e = &t.entries[i];
            if (e->key == TUI_KC_NONE || (e->view && strcmp(e->view, views[v]) != 0)) continue;
            ASSERT(row < h.dialog.nitems, "a row for the entry");
            char name[32];
            tui_keycode_name(e->key, name, sizeof name);
            ASSERT_EQ_STR(h.dialog.items[2 * row], name, "the key shown");
            ASSERT(strstr(h.dialog.items[2 * row + 1], e->action) == h.dialog.items[2 * row + 1],
                   "the action named");
            /* The key, sent to the handler, finds that very entry. */
            tui_event ev = { .type = TUI_EV_KEY, .key = e->key >= TUI_KEY_ENTER ? (tui_key)e->key : TUI_KEY_CHAR,
                             .cp = e->key >= TUI_KEY_ENTER ? 0 : e->key };
            ASSERT_EQ_INT(tui_keytable_find(&t, views[v], &ev), i, "the handler runs the entry the help names");
            row++;
        }
        ASSERT_EQ_INT(h.dialog.nitems, row, "no row the table does not bind");
        tui_keyhelp_free(&h);
    }
}

static void test_help_back_entry(void) {
    static const tui_keyentry back[] = {
        { NULL, TUI_KC_BACK, TUI_VERB_NONE, "Back", NULL, TUI_ACT_HARMLESS, 0, NULL, 1 },
    };
    tui_keytable t;
    ASSERT_EQ_INT(tui_keytable_init(&t, back, 1, NULL, 0), 0, "table");
    tui_keyhelp h;
    ASSERT_EQ_INT(tui_keyhelp_open(&h, &t, "list", "T"), 0, "open");
    ASSERT_EQ_INT(h.dialog.nitems, 1, "one row");
    ASSERT_EQ_STR(h.dialog.items[0], "Esc, q, Backspace", "the Back entry lists all its keys");
    ASSERT_EQ_STR(h.dialog.items[1], "Back", "the action");
    tui_keyhelp_free(&h);
}

static void test_help_picture(void) {
    tui_keytable t;
    tui_keytable_init(&t, g_table, N_TABLE, NULL, 0);
    tui_keyhelp h;
    ASSERT_EQ_INT(tui_keyhelp_open(&h, &t, "list", "List shortcuts"), 0, "open");
    Fx f;
    ASSERT(fx_open(&f, 10, 50) == 0, "fixtures");
    tui_canvas all = tui_canvas_of(f.scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_dialog_render(&h.dialog, all);
    tui_screen_render(f.scr, &f.buf);
    pty_feed(f.s, f.buf.data, f.buf.len);
    const char *const expect[] = {
        "", "",
        "              ┌─ List shortcuts ───┐",
        "              │ *  Star            │",
        "              │ d  Delete          │",
        "              │ r  Reload          │",
        "              │ ?  Help            │",
        "              └────────────────────┘",
        "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    char d[160];
    pty_cell_describe(f.s, 3, 16, d, sizeof d);
    ASSERT(strstr(d, "bold") != NULL, "the key is bold");
    pty_cell_describe(f.s, 3, 19, d, sizeof d);
    ASSERT(strstr(d, "bold") == NULL, "the name is not");
    tui_keyhelp_free(&h);
    fx_close(&f);

    static const tui_keyentry withdesc[] = {
        E("v", TUI_KC_STAR, TUI_VERB_STAR, "Star", "toggle", TUI_ACT_ROUTINE, 0, NULL, 1) };
    tui_keytable t2;
    tui_keytable_init(&t2, withdesc, 1, NULL, 0);
    tui_keyhelp_open(&h, &t2, "v", "T");
    ASSERT_EQ_STR(h.dialog.items[1], "Star - toggle", "the description follows the name");
    tui_keyhelp_free(&h);
    ASSERT_EQ_INT(tui_keyhelp_open(&h, &t2, "w", "T"), 0, "no keys in the view");
    ASSERT_EQ_INT(h.dialog.nitems, 0, "no rows");
    tui_keyhelp_free(&h);
    errno = 0;
    ASSERT_EQ_INT(tui_keyhelp_open(NULL, &t2, "v", "T"), -1, "no help");
    ASSERT_EQ_INT(tui_keyhelp_open(&h, NULL, "v", "T"), -1, "no table");
    tui_keyhelp_free(NULL);
}

/* ── The description of a view ───────────────────────────────────────── */

#define DESC_NAMES "Alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi "

static void test_description_rows_are_validated(void) {
    tui_keytable t;
    char why[160];
    static const tui_keyentry ok[] = {
        E("list", 0, TUI_VERB_NONE, NULL, "One line.\nTwo.\nThree.", TUI_ACT_HARMLESS, 0, NULL, 0),
        E("other", 0, TUI_VERB_NONE, NULL, "Another view's own text.", TUI_ACT_HARMLESS, 0, NULL, 0),
        E("list", '*', TUI_VERB_STAR, "Star", NULL, TUI_ACT_ROUTINE, 0, NULL, 1) };
    ASSERT_EQ_INT(tui_keytable_init(&t, ok, 3, why, sizeof why), 0, "three lines are fine");
    static const tui_keyentry four[] = {
        E("list", 0, TUI_VERB_NONE, NULL, "1\n2\n3\n4", TUI_ACT_HARMLESS, 0, NULL, 0) };
    ASSERT_EQ_INT(tui_keytable_init(&t, four, 1, why, sizeof why), -1, "four lines");
    ASSERT(strstr(why, "three lines") != NULL, "the reason");
    static const tui_keyentry wrapped[] = {
        E("list", 0, TUI_VERB_NONE, NULL, DESC_NAMES DESC_NAMES DESC_NAMES DESC_NAMES DESC_NAMES,
          TUI_ACT_HARMLESS, 0, NULL, 0) };
    ASSERT_EQ_INT(tui_keytable_init(&t, wrapped, 1, why, sizeof why), -1, "wraps to more than three");
    static const tui_keyentry wrapped3[] = {
        E("list", 0, TUI_VERB_NONE, NULL, DESC_NAMES DESC_NAMES, TUI_ACT_HARMLESS, 0, NULL, 0) };
    ASSERT_EQ_INT(tui_keytable_init(&t, wrapped3, 1, why, sizeof why), 0, "wraps to three");
    static const tui_keyentry two[] = {
        E("list", 0, TUI_VERB_NONE, NULL, "a", TUI_ACT_HARMLESS, 0, NULL, 0),
        E(NULL, 0, TUI_VERB_NONE, NULL, "b", TUI_ACT_HARMLESS, 0, NULL, 0) };
    ASSERT_EQ_INT(tui_keytable_init(&t, two, 2, why, sizeof why), -1, "two for one view");
    ASSERT(strstr(why, "second") != NULL, "the reason");
    static const tui_keyentry empty[] = {
        E("list", 0, TUI_VERB_NONE, NULL, "", TUI_ACT_HARMLESS, 0, NULL, 0) };
    ASSERT_EQ_INT(tui_keytable_init(&t, empty, 1, why, sizeof why), -1, "empty text");
    static const tui_keyentry withkey[] = {
        E("list", 'x', TUI_VERB_NONE, NULL, "text", TUI_ACT_HARMLESS, 0, NULL, 0) };
    ASSERT_EQ_INT(tui_keytable_init(&t, withkey, 1, why, sizeof why), -1, "a key on it");
    static const tui_keyentry withcls[] = {
        E("list", 0, TUI_VERB_NONE, NULL, "text", TUI_ACT_RARE, 0, NULL, 0) };
    ASSERT_EQ_INT(tui_keytable_init(&t, withcls, 1, why, sizeof why), -1, "a class on it");
    fault_reset();
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    errno = 0;
    ASSERT_EQ_INT(tui_keytable_init(&t, ok, 3, why, sizeof why), -1, "no memory to count the lines");
    ASSERT_EQ_INT(errno, ENOMEM, "errno");
    fault_reset();
}

static void test_help_shows_the_description(void) {
    static const tui_keyentry keys[] = {
        E("list", 0, TUI_VERB_NONE, NULL, "Shows the things.\nThe cursor is on one thing.",
          TUI_ACT_HARMLESS, 0, NULL, 0),
        E("list", TUI_KC_STAR, TUI_VERB_STAR, "Star", NULL, TUI_ACT_ROUTINE, 0, NULL, 1),
        E("list", TUI_KC_RELOAD, TUI_VERB_NONE, "Reload", NULL, TUI_ACT_HARMLESS, 0, NULL, 2),
        E("plain", TUI_KC_RELOAD, TUI_VERB_NONE, "Reload", NULL, TUI_ACT_HARMLESS, 0, NULL, 3) };
    tui_keytable t;
    ASSERT_EQ_INT(tui_keytable_init(&t, keys, 4, NULL, 0), 0, "table");
    tui_keyhelp h;
    ASSERT_EQ_INT(tui_keyhelp_open(&h, &t, "list", "List shortcuts"), 0, "open");
    ASSERT_EQ_INT(h.dialog.nitems, 2, "the description is not a key row");
    Fx f;
    ASSERT(fx_open(&f, 12, 50) == 0, "fixtures");
    tui_canvas all = tui_canvas_of(f.scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_dialog_render(&h.dialog, all);
    tui_screen_render(f.scr, &f.buf);
    pty_feed(f.s, f.buf.data, f.buf.len);
    char d[160];
    int r = 3;
    ASSERT_EQ_STR(pty_cell_text(f.s, r, 11), "S", "the description is under the title");
    pty_cell_describe(f.s, r, 11, d, sizeof d);
    ASSERT(strstr(d, "bold") == NULL, "plain, not the keys' bold");
    ASSERT_EQ_STR(pty_cell_text(f.s, r + 1, 11), "T", "its second line");
    ASSERT_EQ_STR(pty_cell_text(f.s, r + 2, 11), " ", "a blank row");
    ASSERT_EQ_STR(pty_cell_text(f.s, r + 3, 11), "*", "then the keys");
    pty_cell_describe(f.s, r + 3, 11, d, sizeof d);
    ASSERT(strstr(d, "bold") != NULL, "the key is bold");
    ASSERT_EQ_INT(pty_cell_fg(f.s, r, 11), PTY_FG_DEFAULT, "default colour text");
    tui_keyhelp_free(&h);
    fx_close(&f);

    /* A view without one shows the keys only: the first row is a key. */
    ASSERT_EQ_INT(tui_keyhelp_open(&h, &t, "plain", "Plain"), 0, "open plain");
    ASSERT(fx_open(&f, 12, 50) == 0, "fixtures");
    all = tui_canvas_of(f.scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_dialog_render(&h.dialog, all);
    tui_screen_render(f.scr, &f.buf);
    pty_feed(f.s, f.buf.data, f.buf.len);
    ASSERT(!pty_screen_contains(f.s, "Shows the things"), "no description");
    tui_keyhelp_free(&h);
    fx_close(&f);

    /* The description is for every view when it names none. */
    static const tui_keyentry all_views[] = {
        E(NULL, 0, TUI_VERB_NONE, NULL, "Everywhere.", TUI_ACT_HARMLESS, 0, NULL, 0) };
    tui_keytable_init(&t, all_views, 1, NULL, 0);
    tui_keyhelp_open(&h, &t, "any", "T");
    ASSERT_EQ_STR(h.dialog.text, "Everywhere.", "applies to every view");
    tui_keyhelp_free(&h);

    /* Short of rows the description gives way, one row stays for the keys. */
    tui_keytable_init(&t, keys, 4, NULL, 0);
    tui_keyhelp_open(&h, &t, "list", "L");
    ASSERT(fx_open(&f, 6, 40) == 0, "fixtures");
    all = tui_canvas_of(f.scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_dialog_render(&h.dialog, all);
    tui_screen_render(f.scr, &f.buf);
    pty_feed(f.s, f.buf.data, f.buf.len);
    ASSERT(pty_screen_contains(f.s, "Shows the things"), "the first line is kept");
    ASSERT(pty_screen_contains(f.s, "Star"), "and a key row stays");
    fx_close(&f);
    tui_keyhelp_free(&h);

    /* A long description wraps to the box width, never to more than three rows. */
    static const tui_keyentry longd[] = {
        E("list", 0, TUI_VERB_NONE, NULL, DESC_NAMES DESC_NAMES,
          TUI_ACT_HARMLESS, 0, NULL, 0),
        E("list", TUI_KC_RELOAD, TUI_VERB_NONE, "Reload", NULL, TUI_ACT_HARMLESS, 0, NULL, 1) };
    ASSERT_EQ_INT(tui_keytable_init(&t, longd, 2, NULL, 0), 0, "a long description");
    tui_keyhelp_open(&h, &t, "list", "L");
    ASSERT(fx_open(&f, 14, 30) == 0, "fixtures");
    all = tui_canvas_of(f.scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_dialog_render(&h.dialog, all);
    tui_screen_render(f.scr, &f.buf);
    pty_feed(f.s, f.buf.data, f.buf.len);
    int top = -1, rel = -1;
    for (int r = 0; r < 14; r++) {
        char row[128];
        pty_row_text(f.s, r, row, sizeof row);
        if (strstr(row, "┌─ L")) top = r;
        if (strstr(row, "Reload")) rel = r;
    }
    ASSERT(pty_screen_contains(f.s, "Alpha beta gamma delta"), "wrapped to the box");
    ASSERT(pty_screen_contains(f.s, "Reload"), "the keys follow");
    ASSERT_EQ_INT(rel - top, 5, "three description rows and a blank before the keys");
    fx_close(&f);
    tui_keyhelp_free(&h);

    /* Allocation failures on the way. */
    tui_keytable_init(&t, keys, 4, NULL, 0);
    for (int skip = 0; skip < 12; skip++) {
        fault_reset();
        fault_arm(F_MALLOC, skip, 1, ENOMEM);
        if (tui_keyhelp_open(&h, &t, "list", "T") == 0) tui_keyhelp_free(&h);
    }
    fault_reset();
}

/* ── Allocation failures ─────────────────────────────────────────────── */


/* ── The quick keys ──────────────────────────────────────────────────── */

#define SIMPLE(name) E("v", TUI_KC_NONE, TUI_VERB_NONE, name, NULL, TUI_ACT_RARE, 0, NULL, 0)

static void assert_quick(const tui_keyentry *e, int n, const char *given, const char *expect,
                         int missing, const char *what) {
    tui_keytable t;
    char out[64];
    ASSERT_EQ_INT(tui_keytable_init(&t, e, n, NULL, 0), 0, what);
    ASSERT_EQ_INT(tui_keytable_assign_quick(&t, "v", given, out), missing, what);
    for (int i = 0; i < n; i++) {
        char w[96];
        snprintf(w, sizeof w, "%s: entry %d", what, i);
        ASSERT_EQ_INT(out[i], expect[i] == '-' ? 0 : expect[i], w);
    }
}

static void test_quick_keys_assignment(void) {
    /* The first free letter of the name, then the next ones, then a digit. */
    static const tui_keyentry share[] = { SIMPLE("Share"), SIMPLE("Sync"), SIMPLE("Save") };
    assert_quick(share, 3, NULL, "sya", 0, "automatic: the first free letter");
    static const tui_keyentry zz[] = { SIMPLE("Zz"), SIMPLE("Zz"), SIMPLE("Zz") };
    assert_quick(zz, 3, NULL, "z12", 0, "a clash falls to the next letter, then a digit");
    static const tui_keyentry words[] = { SIMPLE("Show all"), SIMPLE("Sort all") };
    assert_quick(words, 2, NULL, "sa", 0, "the next word's first letter");
    /* The list keys are never assigned. */
    static const tui_keyentry lk[] = { SIMPLE("Jack"), SIMPLE("Quit it"), SIMPLE("Kgj"), SIMPLE("Gq") };
    assert_quick(lk, 4, NULL, "ai12", 0, "j k g G q are never assigned");
    /* Explicit letters win and stay unique; unusable ones are ignored. */
    static const tui_keyentry ex[] = { SIMPLE("Alpha"), SIMPLE("Bravo"), SIMPLE("Charlie"), SIMPLE("Delta") };
    assert_quick(ex, 4, "bbxx", "brxd", 0, "explicit letters win, a repeated one is automatic");
    assert_quick(ex, 4, "jqG\0", "abcd", 0, "list keys given are ignored");
    assert_quick(ex, 4, "\x80\x01:?", "abcd", 0, "unusable letters given are ignored");
    assert_quick(ex, 4, "D5\0\0", "D5ce", 0, "an uppercase letter and a digit may be given");
    static const tui_keyentry ver[] = { SIMPLE("Delete"), SIMPLE("Drop") };
    assert_quick(ver, 2, "-d", "ed", 0, "a given d beats the verb table");
    /* The verb table: the same letter in every program. */
    static const tui_keyentry verbs[] = {
        SIMPLE("Archive"), SIMPLE("Start"), SIMPLE("Copy"), SIMPLE("Delete"), SIMPLE("Edit"),
        SIMPLE("Forward"), SIMPLE("Label"), SIMPLE("Move"), SIMPLE("New"), SIMPLE("Open"),
        SIMPLE("Pause"), SIMPLE("Reload"), SIMPLE("Restart"), SIMPLE("Unarchive"),
        SIMPLE("Revert"), SIMPLE("Rename"), SIMPLE("Stop") };
    assert_quick(verbs, 17, NULL, "abcdeflmnoprtuvwx", 0, "the verb table");
    static const tui_keyentry cased[] = { SIMPLE("DELETE all"), SIMPLE("Deleted"), SIMPLE("Del") };
    assert_quick(cased, 3, NULL, "del", 0, "the verb is the first whole word, any case");
    /* Delete is d in two menus with different other entries. */
    static const tui_keyentry m1[] = { SIMPLE("Dump"), SIMPLE("Delete"), SIMPLE("Dig") };
    static const tui_keyentry m2[] = { SIMPLE("Disable"), SIMPLE("Delete messages") };
    assert_quick(m1, 3, NULL, "udi", 0, "menu one: delete is d");
    assert_quick(m2, 2, NULL, "id", 0, "menu two: delete is d");
    /* More entries than characters: the rest has none and is counted. */
    static const tui_keyentry many[] = {
        SIMPLE("Zz"), SIMPLE("Zz"), SIMPLE("Zz"), SIMPLE("Zz"), SIMPLE("Zz"), SIMPLE("Zz"),
        SIMPLE("Zz"), SIMPLE("Zz"), SIMPLE("Zz"), SIMPLE("Zz"), SIMPLE("Zz"), SIMPLE("Zz") };
    assert_quick(many, 12, NULL, "z123456789--", 2, "too many entries are counted");
    /* Only the menu's entries have one. */
    tui_keytable t;
    char out[N_TABLE];
    ASSERT_EQ_INT(tui_keytable_init(&t, g_table, N_TABLE, NULL, 0), 0, "table");
    ASSERT_EQ_INT(tui_keytable_assign_quick(&t, "list", NULL, out), 0, "list");
    ASSERT(memcmp(out, "sdtr\0\0\0", 7) == 0, "list: s d t r, none for the harmless and the other view");
    errno = 0;
    ASSERT_EQ_INT(tui_keytable_assign_quick(NULL, "list", NULL, out), -1, "no table");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_keytable_assign_quick(&t, "list", NULL, NULL), -1, "no output");
}

static void test_quick_key_runs_its_entry(void) {
    tui_keytable t;
    tui_keytable_init(&t, g_table, N_TABLE, NULL, 0);
    tui_actionmenu m;
    /* A routine entry: chosen at once, as Enter on it does. */
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "list", "mail 7"), 0, "open");
    ASSERT_EQ_INT(menu_send(&m, "s"), TUI_ACTION_CHOSEN, "s runs Star");
    ASSERT_EQ_INT(m.entry, 0, "the entry");
    tui_actionmenu_free(&m);
    /* A menu-only entry. */
    tui_actionmenu_open(&m, &t, "list", "mail 7");
    ASSERT_EQ_INT(menu_send(&m, "r"), TUI_ACTION_CHOSEN, "r runs Rotate secret");
    ASSERT_EQ_INT(m.entry, 3, "the entry");
    tui_actionmenu_free(&m);
    /* A serious entry opens its confirmation first, and No performs nothing. */
    Fx f;
    ASSERT(fx_open(&f, 12, 60) == 0, "fixtures");
    tui_actionmenu_open(&m, &t, "list", "mail 7");
    ASSERT_EQ_INT(menu_send(&m, "d"), TUI_ACTION_OPEN, "d asks first");
    fx_draw(&f, &m);
    ASSERT(pty_screen_contains(f.s, "Delete mail 7? It cannot be undone."), "the target is named");
    ASSERT_EQ_INT(menu_send(&m, "\r"), TUI_ACTION_CANCELLED, "Enter on the default No");
    tui_actionmenu_free(&m);
    tui_actionmenu_open(&m, &t, "list", "mail 7");
    menu_send(&m, "t");
    ASSERT_EQ_INT(m.state, TUI_ACTION_OPEN, "t asks first");
    ASSERT_EQ_INT(menu_send(&m, "y"), TUI_ACTION_CHOSEN, "Yes performs");
    ASSERT_EQ_INT(m.entry, 2, "Restart service");
    tui_actionmenu_free(&m);
    /* A letter that is no quick key changes nothing; the list keys still work. */
    tui_actionmenu_open(&m, &t, "list", "mail 7");
    ASSERT_EQ_INT(menu_send(&m, "zjk"), TUI_ACTION_OPEN, "z, j, k change nothing but the row");
    ASSERT_EQ_INT(menu_send(&m, "G"), TUI_ACTION_OPEN, "G goes last");
    ASSERT_EQ_INT(menu_send(&m, "\r"), TUI_ACTION_CHOSEN, "Enter runs the row");
    ASSERT_EQ_INT(m.entry, 3, "the last entry");
    tui_actionmenu_free(&m);
    /* q and Backspace close the menu like Esc. */
    tui_actionmenu_open(&m, &t, "list", "x");
    ASSERT_EQ_INT(menu_send(&m, "q"), TUI_ACTION_CANCELLED, "q closes");
    tui_actionmenu_free(&m);
    tui_actionmenu_open(&m, &t, "list", "x");
    ASSERT_EQ_INT(menu_send(&m, "\x7f"), TUI_ACTION_CANCELLED, "Backspace closes");
    tui_actionmenu_free(&m);
    /* In a confirmation q is not the menu's: the dialog keeps its own keys. */
    tui_actionmenu_open(&m, &t, "list", "x");
    menu_send(&m, "d");
    ASSERT_EQ_INT(menu_send(&m, "s"), TUI_ACTION_OPEN, "a quick key is no answer to a question");
    ASSERT_EQ_INT(menu_send(&m, "\x1b"), TUI_ACTION_CANCELLED, "Esc refuses");
    tui_actionmenu_free(&m);
    /* Events that are no plain character are not quick keys. */
    tui_actionmenu_open(&m, &t, "list", "x");
    tui_event alt = key_ev("\x1b" "s"), ctl = key_ev("\x13");
    tui_event rs = { .type = TUI_EV_RESIZE };
    ASSERT_EQ_INT(tui_actionmenu_handle(&m, &alt), TUI_ACTION_OPEN, "Alt+s is not s");
    ASSERT_EQ_INT(tui_actionmenu_handle(&m, &ctl), TUI_ACTION_OPEN, "ctrl-s is not s");
    ASSERT_EQ_INT(tui_actionmenu_handle(&m, &rs), TUI_ACTION_OPEN, "a resize is not a key");
    tui_actionmenu_free(&m);
    fx_close(&f);
}

static void test_open_keys_gives_the_programs_letters(void) {
    tui_keytable t;
    tui_keytable_init(&t, g_table, N_TABLE, NULL, 0);
    tui_actionmenu m;
    /* The program wants x for Star and w for Rotate secret. */
    char given[N_TABLE] = { 'x', 0, 0, 'w', 0, 0, 0 };
    ASSERT_EQ_INT(tui_actionmenu_open_keys(&m, &t, "list", "mail 7", given), 0, "open");
    ASSERT_EQ_INT(menu_send(&m, "s"), TUI_ACTION_OPEN, "s is nothing now");
    ASSERT_EQ_INT(menu_send(&m, "x"), TUI_ACTION_CHOSEN, "x runs Star");
    ASSERT_EQ_INT(m.entry, 0, "the entry");
    tui_actionmenu_free(&m);
    tui_actionmenu_open_keys(&m, &t, "list", "mail 7", given);
    ASSERT_EQ_INT(menu_send(&m, "w"), TUI_ACTION_CHOSEN, "w runs Rotate secret");
    ASSERT_EQ_INT(m.entry, 3, "the entry");
    tui_actionmenu_free(&m);
    /* More entries than characters: the rest stay reachable by the list keys. */
    static tui_keyentry many[12];
    for (int i = 0; i < 12; i++) many[i] = (tui_keyentry)SIMPLE("Zz");
    tui_keytable_init(&t, many, 12, NULL, 0);
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "v", NULL), 0, "open");
    ASSERT_EQ_INT(menu_send(&m, "GG"), TUI_ACTION_OPEN, "G goes last");
    ASSERT_EQ_INT(menu_send(&m, "\r"), TUI_ACTION_CHOSEN, "Enter runs it");
    ASSERT_EQ_INT(m.entry, 11, "the entry with no quick key");
    tui_actionmenu_free(&m);
    ASSERT_EQ_INT(tui_actionmenu_open_keys(NULL, &t, "v", NULL, NULL), -1, "no menu");
}

/* The menu of ten entries, at 80x24 and at 40 columns: the letters are in the
 * accent colour in a column of their own, and a long entry is cut with an
 * ellipsis while its letter stays. */
static const tui_keyentry g_ten[] = {
    E("v", TUI_KC_DELETE_D, TUI_VERB_DELETE, "Delete", NULL, TUI_ACT_SERIOUS, 1, "Delete %s?", 1),
    E("v", TUI_KC_NONE, TUI_VERB_NONE, "Restart the whole service and its workers", NULL,
      TUI_ACT_SERIOUS, 0, "Restart %s?", 2),
    E("v", TUI_KC_NONE, TUI_VERB_NONE, "Rename", NULL, TUI_ACT_RARE, 0, NULL, 3),
    E("v", TUI_KC_NONE, TUI_VERB_NONE, "Archive", NULL, TUI_ACT_RARE, 0, NULL, 4),
    E("v", TUI_KC_NONE, TUI_VERB_NONE, "Label", NULL, TUI_ACT_RARE, 0, NULL, 5),
    E("v", TUI_KC_MOVE, TUI_VERB_MOVE, "Move", NULL, TUI_ACT_ROUTINE, 0, NULL, 6),
    E("v", TUI_KC_NONE, TUI_VERB_NONE, "Export", NULL, TUI_ACT_RARE, 0, NULL, 7),
    E("v", TUI_KC_NONE, TUI_VERB_NONE, "Import", NULL, TUI_ACT_RARE, 0, NULL, 8),
    E("v", TUI_KC_NONE, TUI_VERB_NONE, "Verify", NULL, TUI_ACT_RARE, 0, NULL, 9),
    E("v", TUI_KC_NONE, TUI_VERB_NONE, "Zip", NULL, TUI_ACT_RARE, 0, NULL, 10),
};

static void check_ten(int rows, int cols) {
    tui_keytable t;
    ASSERT_EQ_INT(tui_keytable_init(&t, g_ten, 10, NULL, 0), 0, "table");
    tui_actionmenu m;
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "v", "x"), 0, "open");
    char out[10];
    ASSERT_EQ_INT(tui_keytable_assign_quick(&t, "v", NULL, out), 0, "all assigned");
    Fx f;
    ASSERT(fx_open(&f, rows, cols) == 0, "fixtures");
    fx_draw(&f, &m);
    /* The box: its top-left corner; the letter column is 4 cells in. */
    int r0 = -1, c0 = -1;
    for (int r = 0; r < rows && r0 < 0; r++)
        for (int c = 0; c < cols; c++)
            if (strcmp(pty_cell_text(f.s, r, c), "┌") == 0) {
                r0 = r;
                c0 = c;
                break;
            }
    ASSERT(r0 >= 0, "the menu is drawn");
    for (int i = 0; i < 10; i++) {
        char want[2] = { out[i], '\0' }, w[64];
        snprintf(w, sizeof w, "row %d: its letter in the letter column", i);
        ASSERT_EQ_STR(pty_cell_text(f.s, r0 + 1 + i, c0 + 4), want, w);
        snprintf(w, sizeof w, "row %d: the letter's colour", i);
        if (i > 0) ASSERT_EQ_INT(pty_cell_fg(f.s, r0 + 1 + i, c0 + 4), PTY_FG_CYAN, w);
        ASSERT_EQ_STR(pty_cell_text(f.s, r0 + 1 + i, c0 + 5), " ", "a gap after the letter");
    }
    ASSERT_EQ_INT(pty_cell_fg(f.s, r0 + 1, c0 + 4), 97, "the selected row keeps the selection's colours");
    ASSERT(strstr(out, "d") && out[0] == 'd' && out[2] == 'w' && out[3] == 'a' && out[4] == 'l' &&
           out[5] == 'm', "delete d, rename w, archive a, label l, move m");
    if (cols >= 60) {
        ASSERT(pty_screen_contains(f.s, "Restart the whole service and its workers"), "the whole name at 80");
    } else {
        ASSERT_EQ_STR(pty_cell_text(f.s, r0 + 2, c0 + 4), "t",
                      "the long entry keeps its letter at 40 columns");
        ASSERT(!pty_screen_contains(f.s, "workers"), "the long entry is cut");
        ASSERT(pty_screen_contains(f.s, "…"), "with an ellipsis");
    }
    tui_actionmenu_free(&m);
    fx_close(&f);
}

static void test_quick_keys_at_80x24_and_40_columns(void) {
    check_ten(24, 80);
    check_ten(24, 40);
}

static void test_menu_help_lists_the_quick_keys(void) {
    tui_keytable t;
    tui_keytable_init(&t, g_table, N_TABLE, NULL, 0);
    tui_actionmenu m;
    Fx f;
    ASSERT(fx_open(&f, 14, 70) == 0, "fixtures");
    tui_actionmenu_open(&m, &t, "list", "mail 7");
    menu_send(&m, "jj");
    ASSERT_EQ_INT(menu_send(&m, "?"), TUI_ACTION_OPEN, "? opens the help");
    fx_draw(&f, &m);
    ASSERT(pty_screen_contains(f.s, "letter"), "the help names the letter");
    ASSERT(pty_screen_contains(f.s, "run the entry"), "letter: run the entry");
    ASSERT(pty_screen_contains(f.s, "Esc, q, Backspace"), "and the list keys");
    ASSERT_EQ_INT(menu_send(&m, "s"), TUI_ACTION_OPEN, "a quick key does nothing in the help");
    ASSERT_EQ_INT(m.state, TUI_ACTION_OPEN, "still open");
    ASSERT_EQ_INT(menu_send(&m, "q"), TUI_ACTION_OPEN, "q closes the help only");
    fx_draw(&f, &m);
    ASSERT(pty_screen_contains(f.s, "Actions"), "the menu is back");
    ASSERT_EQ_INT(menu_send(&m, "\r"), TUI_ACTION_OPEN, "Enter on the row it stood on (Restart) asks");
    ASSERT_EQ_INT(menu_send(&m, "y"), TUI_ACTION_CHOSEN, "Yes");
    ASSERT_EQ_INT(m.entry, 2, "the row the list stood on before the help");
    tui_actionmenu_free(&m);
    fx_close(&f);
}

static void test_allocation_failures(void) {
    fault_reset();
    tui_keytable t;
    tui_keytable_init(&t, g_table, N_TABLE, NULL, 0);
    tui_actionmenu m;
    tui_keyhelp h;
    errno = 0;
    fault_arm(F_MALLOC, 0, 1, ENOMEM);                      /* the quick keys */
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "list", "x"), -1, "menu: the quick keys");
    ASSERT_EQ_INT(errno, ENOMEM, "errno");
    fault_reset();
    errno = 0;
    fault_arm(F_MALLOC, 1, 1, ENOMEM);                      /* the block */
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "list", "x"), -1, "menu: the block");
    ASSERT_EQ_INT(errno, ENOMEM, "errno");
    fault_reset();
    fault_arm(F_MALLOC, 2, 1, ENOMEM);                      /* the dialog's title copy */
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "list", "x"), -1, "menu: the dialog");
    fault_reset();
    errno = 0;
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_keyhelp_open(&h, &t, "list", "T"), -1, "help: the block");
    ASSERT_EQ_INT(errno, ENOMEM, "errno");
    fault_reset();
    fault_arm(F_MALLOC, 1, 1, ENOMEM);
    ASSERT_EQ_INT(tui_keyhelp_open(&h, &t, "list", "T"), -1, "help: the dialog");
    fault_reset();

    /* The question for a direct key cannot be built: refused, not performed. */
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    errno = 0;
    ASSERT_EQ_INT(tui_actionmenu_run(&m, &t, 1, "x"), -1, "run: the question");
    ASSERT_EQ_INT(errno, ENOMEM, "errno");
    fault_reset();
    fault_arm(F_MALLOC, 1, 1, ENOMEM);
    ASSERT_EQ_INT(tui_actionmenu_run(&m, &t, 1, "x"), -1, "run: the dialog");
    fault_reset();

    /* From the menu: the same, and the action is not performed. */
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "list", "x"), 0, "open");
    menu_send(&m, "j");
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(menu_send(&m, "\r"), TUI_ACTION_CANCELLED, "the question fails: cancelled");
    fault_reset();
    tui_actionmenu_free(&m);

    /* The same through a quick key. */
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "list", "x"), 0, "open");
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(menu_send(&m, "d"), TUI_ACTION_CANCELLED, "quick key: the question fails");
    fault_reset();
    tui_actionmenu_free(&m);

    /* The menu's help cannot be built, or the menu cannot come back. */
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "list", "x"), 0, "open");
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(menu_send(&m, "?"), TUI_ACTION_CANCELLED, "help: no memory, cancelled");
    fault_reset();
    tui_actionmenu_free(&m);
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "list", "x"), 0, "open");
    ASSERT_EQ_INT(menu_send(&m, "?"), TUI_ACTION_OPEN, "help open");
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(menu_send(&m, "\x1b"), TUI_ACTION_CANCELLED, "help closed, menu cannot return");
    fault_reset();
    tui_actionmenu_free(&m);
}

static const TestCase tests[] = {
    TEST_CASE(test_keycodes_and_names),
    TEST_CASE(test_registry_and_reserved),
    TEST_CASE(test_way_out),
    TEST_CASE(test_table_accepts_the_policy),
    TEST_CASE(test_description_rows_are_validated),
    TEST_CASE(test_help_shows_the_description),
    TEST_CASE(test_table_refuses_unregistered_state_change),
    TEST_CASE(test_table_refuses_serious_without_confirmation),
    TEST_CASE(test_table_refuses_rare_with_direct_key),
    TEST_CASE(test_table_refuses_malformed),
    TEST_CASE(test_find),
    TEST_CASE(test_menu_lists_the_actions_with_their_keys),
    TEST_CASE(test_menu_routine_action_is_chosen_without_asking),
    TEST_CASE(test_menu_serious_action_asks_naming_the_target),
    TEST_CASE(test_run_by_direct_key),
    TEST_CASE(test_menu_256_colours_and_8_colours),
    TEST_CASE(test_quick_keys_assignment),
    TEST_CASE(test_quick_key_runs_its_entry),
    TEST_CASE(test_open_keys_gives_the_programs_letters),
    TEST_CASE(test_quick_keys_at_80x24_and_40_columns),
    TEST_CASE(test_menu_help_lists_the_quick_keys),
    TEST_CASE(test_help_matches_the_handler),
    TEST_CASE(test_help_back_entry),
    TEST_CASE(test_help_picture),
    TEST_CASE(test_allocation_failures),
};

int main(int argc, char **argv) {
    setenv("TERM", "xterm-256color", 1);   /* the depth the picture tests expect */
    unsetenv("COLORTERM");
    return RUN_SUITE("actions", tests, 150, 60);
}
