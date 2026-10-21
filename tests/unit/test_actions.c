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
    { 'M', PTY_ATTR_BOLD, 97, PTY_COLOR_IDX(17) },            /* its '>' */
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
        "        ┌─ Actions ───────────────────────────────┐",
        "        │ > *  Star                               │",
        "        │   d  Delete                             │",
        "        │      Restart service - starts a process │",
        "        │      Rotate secret                      │",
        "        └─────────────────────────────────────────┘",
        "", "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    const char *const map[] = {
        "A.MBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.A",     /* the selected row */
        "A.........................................A",     /* the next one */
        NULL };
    ASSERT_REGION_STYLE(f.s, PTY_REGION(4, 8, 2, 43), map, legend);
    ASSERT_EQ_STR(pty_cell_text(f.s, 4, 10), ">", "the selection marker");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 4, 13), PTY_COLOR_IDX(17), "the selected row: dark blue");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 5, 13), PTY_BG_DEFAULT, "the other rows: the terminal's own");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 3, 9), PTY_FG_CYAN, "the frame is the accent");
    /* Movement keys move; Esc cancels. */
    ASSERT_EQ_INT(menu_send(&m, "j"), TUI_ACTION_OPEN, "j moves");
    fx_draw(&f, &m);
    ASSERT_EQ_STR(pty_cell_text(f.s, 5, 10), ">", "the marker follows");
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
    ASSERT_EQ_STR(pty_cell_text(f.s, 4, 10), ">", "the marker is there as well");
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
            if (e->view && strcmp(e->view, views[v]) != 0) continue;
            ASSERT(row < h.dialog.nitems, "a row for the entry");
            char name[32];
            tui_keycode_name(e->key, name, sizeof name);
            ASSERT_EQ_STR(h.dialog.items[2 * row], e->key == TUI_KC_NONE ? ":" : name, "the key shown");
            ASSERT(strstr(h.dialog.items[2 * row + 1], e->action) == h.dialog.items[2 * row + 1],
                   "the action named");
            if (e->key == TUI_KC_NONE) {   /* menu-only: ':' stands for it */
                row++;
                continue;
            }
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
    ASSERT(fx_open(&f, 12, 50) == 0, "fixtures");
    tui_canvas all = tui_canvas_of(f.scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_dialog_render(&h.dialog, all);
    tui_screen_render(f.scr, &f.buf);
    pty_feed(f.s, f.buf.data, f.buf.len);
    const char *const expect[] = {
        "", "",
        "    ┌─ List shortcuts ──────────────────────┐",
        "    │ *  Star                               │",
        "    │ d  Delete                             │",
        "    │ :  Restart service - starts a process │",
        "    │ :  Rotate secret                      │",
        "    │ r  Reload                             │",
        "    │ ?  Help                               │",
        "    └───────────────────────────────────────┘",
        "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    char d[160];
    pty_cell_describe(f.s, 3, 6, d, sizeof d);
    ASSERT(strstr(d, "bold") != NULL, "the key is bold");
    pty_cell_describe(f.s, 3, 9, d, sizeof d);
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
    for (int rr = 0; rr < 14; rr++) {
        char row[128];
        pty_row_text(f.s, rr, row, sizeof row);
        if (strstr(row, "┌─ L")) top = rr;
        if (strstr(row, "Reload")) rel = rr;
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


/* ── The search of the help (TUI-59) ─────────────────────────────────── */

static tui_dialog_state help_send(tui_keyhelp *h, const char *keys) {
    tui_decoder dec;
    tui_decoder_init(&dec);
    tui_decoder_feed(&dec, keys, strlen(keys));
    tui_event ev;
    tui_dialog_state st = h->dialog.state;
    while (tui_decoder_next(&dec, &ev)) st = tui_dialog_handle(&h->dialog, &ev);
    if (tui_decoder_timeout(&dec, &ev)) st = tui_dialog_handle(&h->dialog, &ev);
    tui_decoder_free(&dec);
    return st;
}

/* Draws the help and returns the rows of the screen joined by newlines. */
static void help_draw(Fx *f, tui_keyhelp *h, char *out, size_t size) {
    tui_canvas all = tui_canvas_of(f->scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_dialog_render(&h->dialog, all);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
    out[0] = '\0';
    for (int r = 0; r < 14; r++) {
        char row[256];
        pty_row_text(f->s, r, row, sizeof row);
        strncat(out, row, size - strlen(out) - 1);
        strncat(out, "\n", size - strlen(out) - 1);
    }
}

static const tui_keyentry g_search_table[] = {
    E("list", TUI_KC_STAR, TUI_VERB_STAR, "Star", "toggle the star", TUI_ACT_ROUTINE, 0, NULL, 1),
    E("list", TUI_KC_DELETE_D, TUI_VERB_DELETE, "Delete", NULL, TUI_ACT_SERIOUS, 1,
      "Delete %s? It cannot be undone.", 2),
    E("list", TUI_KC_NONE, TUI_VERB_NONE, "Restart service", "starts a process", TUI_ACT_SERIOUS, 0,
      "Restart the service of %s?", 3),
    E("list", TUI_KC_NONE, TUI_VERB_NONE, "Rotate secret", NULL, TUI_ACT_RARE, 0, NULL, 4),
    E("list", TUI_KC_RELOAD, TUI_VERB_NONE, "Reload", "fetch again", TUI_ACT_HARMLESS, 0, NULL, 5),
    E("list", TUI_KC_HELP, TUI_VERB_NONE, "Help", NULL, TUI_ACT_HARMLESS, 0, NULL, 6),
};

/* The entries shown after the keys @p typed, as the names of their actions. */
static void search_shows(const char *typed, const char *const *want, int n, const char *what) {
    tui_keytable t;
    ASSERT_EQ_INT(tui_keytable_init(&t, g_search_table, 6, NULL, 0), 0, "table");
    tui_keyhelp h;
    ASSERT_EQ_INT(tui_keyhelp_open(&h, &t, "list", "Search"), 0, "open");
    Fx f;
    ASSERT(fx_open(&f, 14, 60) == 0, "fixtures");
    char scr[4096];
    help_draw(&f, &h, scr, sizeof scr);    /* the window is known once drawn */
    char keys[128];
    snprintf(keys, sizeof keys, "/%s", typed);
    help_send(&h, keys);
    help_draw(&f, &h, scr, sizeof scr);
    static const char *const all[] = { "Star", "Delete", "Restart service", "Rotate secret",
                                       "Reload", "Help" };
    for (int i = 0; i < 6; i++) {
        int expected = 0;
        for (int k = 0; k < n; k++) expected |= strcmp(all[i], want[k]) == 0;
        char msg[160];
        snprintf(msg, sizeof msg, "%s: %s %s", what, all[i], expected ? "shown" : "hidden");
        ASSERT((strstr(scr, all[i]) != NULL) == expected, msg);
    }
    fx_close(&f);
    tui_keyhelp_free(&h);
}

static void test_help_search_narrows_to_whole_entries(void) {
    search_shows("d", (const char *const[]){ "Delete", "Reload" }, 2,
                 "by a letter (key or action name)");
    search_shows("star", (const char *const[]){ "Star", "Restart service" }, 2, "by the action name");
    search_shows("STAR", (const char *const[]){ "Star", "Restart service" }, 2, "case-insensitive");
    search_shows("fetch", (const char *const[]){ "Reload" }, 1, "by a word of the description");
    search_shows("process", (const char *const[]){ "Restart service" }, 1, "a menu-only action by its text");
    search_shows("rotate", (const char *const[]){ "Rotate secret" }, 1, "a menu-only action by its name");
    search_shows("star toggle", (const char *const[]){ "Star" }, 1, "two words, both required");
    search_shows("toggle  star", (const char *const[]){ "Star" }, 1, "two words, any order and spacing");
    search_shows("zzz", NULL, 0, "nothing");
    search_shows("star fetch", NULL, 0, "two words no entry has both");
    search_shows("", (const char *const[]){ "Star", "Delete", "Restart service", "Rotate secret",
                                           "Reload", "Help" }, 6, "an empty search shows all");
}

static void test_help_search_by_key_and_menu_key(void) {
    tui_keytable t;
    tui_keytable_init(&t, g_search_table, 6, NULL, 0);
    tui_keyhelp h;
    tui_keyhelp_open(&h, &t, "list", "Search");
    Fx f;
    ASSERT(fx_open(&f, 14, 60) == 0, "fixtures");
    char scr[4096];
    help_draw(&f, &h, scr, sizeof scr);
    help_send(&h, "/:");
    help_draw(&f, &h, scr, sizeof scr);
    ASSERT(strstr(scr, ":  Restart service") && strstr(scr, ":  Rotate secret"), "':' finds the menu-only actions");
    ASSERT(strstr(scr, "Star") == NULL && strstr(scr, "Reload") == NULL, "and only them");
    help_send(&h, "\x15" "?");   /* ctrl-u clears the line, then a key name */
    help_draw(&f, &h, scr, sizeof scr);
    ASSERT(strstr(scr, "?  Help") && strstr(scr, "Star") == NULL, "by the key");
    fx_close(&f);
    tui_keyhelp_free(&h);
}

/* Esc clears the search first, closes on the next press; Enter keeps the list. */
static void test_help_search_esc_enter_and_empty(void) {
    tui_keytable t;
    tui_keytable_init(&t, g_search_table, 6, NULL, 0);
    tui_keyhelp h;
    tui_keyhelp_open(&h, &t, "list", "Search");
    Fx f;
    ASSERT(fx_open(&f, 14, 60) == 0, "fixtures");
    char scr[4096];
    help_draw(&f, &h, scr, sizeof scr);
    help_send(&h, "/rel");
    help_draw(&f, &h, scr, sizeof scr);
    ASSERT(strstr(scr, "/ rel"), "the line shows the typed text");
    ASSERT_EQ_INT(help_send(&h, "\r"), TUI_DIALOG_OPEN, "Enter leaves the line");
    help_draw(&f, &h, scr, sizeof scr);
    ASSERT(strstr(scr, "Reload") && strstr(scr, "Star") == NULL, "and keeps the narrowed list");
    ASSERT(strstr(scr, "/ rel"), "the query stays visible");
    ASSERT_EQ_INT(help_send(&h, "q"), TUI_DIALOG_OK, "q goes back as before");
    tui_keyhelp_free(&h);

    tui_keyhelp_open(&h, &t, "list", "Search");
    help_draw(&f, &h, scr, sizeof scr);
    help_send(&h, "/rel");
    ASSERT_EQ_INT(help_send(&h, "\x1b"), TUI_DIALOG_OPEN, "Esc in the line clears, does not close");
    help_draw(&f, &h, scr, sizeof scr);
    ASSERT(strstr(scr, "Star") && strstr(scr, "Help") && strstr(scr, "/ ") == NULL, "all entries again");
    ASSERT_EQ_INT(help_send(&h, "\x1b"), TUI_DIALOG_OK, "the next Esc closes");
    tui_keyhelp_free(&h);

    tui_keyhelp_open(&h, &t, "list", "Search");
    help_draw(&f, &h, scr, sizeof scr);
    help_send(&h, "/rel\r");
    ASSERT_EQ_INT(help_send(&h, "\x1b"), TUI_DIALOG_OPEN, "Esc after Enter clears the kept search");
    help_draw(&f, &h, scr, sizeof scr);
    ASSERT(strstr(scr, "Star") != NULL, "all entries again");
    help_send(&h, "/restrat");
    help_draw(&f, &h, scr, sizeof scr);
    ASSERT(strstr(scr, "No key matches \"restrat\""), "an empty result says so");
    ASSERT(strstr(scr, "Star") == NULL, "and shows no entry");
    ASSERT_EQ_INT(help_send(&h, "\x03"), TUI_DIALOG_OK, "ctrl-c closes from the line");
    fx_close(&f);
    tui_keyhelp_free(&h);
}

static void test_help_search_highlight_and_line_colours(void) {
    tui_keytable t;
    tui_keytable_init(&t, g_search_table, 6, NULL, 0);
    tui_keyhelp h;
    tui_keyhelp_open(&h, &t, "list", "Search");
    Fx f;
    ASSERT(fx_open(&f, 14, 60) == 0, "fixtures");
    char scr[4096];
    help_draw(&f, &h, scr, sizeof scr);
    help_send(&h, "/star TOGG\r");
    help_draw(&f, &h, scr, sizeof scr);
    int row = -1, col = -1;
    for (int r = 0; r < 14; r++) {
        char line[256];
        pty_row_text(f.s, r, line, sizeof line);
        if (strstr(line, "Star - toggle")) row = r;
    }
    ASSERT(row >= 0, "the entry is shown");
    for (int c = 0; c < 60 && col < 0; c++)
        if (strcmp(pty_cell_text(f.s, row, c), "S") == 0) col = c;
    char d[160];
    pty_cell_describe(f.s, row, col, d, sizeof d);
    ASSERT(strstr(d, "underline") && strstr(d, "bold"), "a matched word in the action is highlighted");
    ASSERT_EQ_INT(pty_cell_fg(f.s, row, col), PTY_FG_CYAN, "in the accent colour");
    pty_cell_describe(f.s, row, col + 4, d, sizeof d);
    ASSERT(strstr(d, "underline") == NULL, "the rest of the line is not");
    ASSERT_EQ_INT(pty_cell_fg(f.s, row, col + 4), PTY_FG_DEFAULT, "default colour");
    int tg = col + 7;      /* "Star - toggle": the T of toggle */
    ASSERT_EQ_STR(pty_cell_text(f.s, row, tg), "t", "the description word");
    pty_cell_describe(f.s, row, tg, d, sizeof d);
    ASSERT(strstr(d, "underline") != NULL, "a matched word of the description is highlighted");
    pty_cell_describe(f.s, row, tg + 4, d, sizeof d);
    ASSERT(strstr(d, "underline") == NULL, "only the typed part");
    fx_close(&f);
    tui_keyhelp_free(&h);
}

static void test_help_search_scrolls_and_survives_failures(void) {
    tui_keytable t;
    tui_keytable_init(&t, g_search_table, 6, NULL, 0);
    tui_keyhelp h;
    tui_keyhelp_open(&h, &t, "list", "Search");
    Fx f;
    ASSERT(fx_open(&f, 8, 60) == 0, "fixtures");     /* too small for all rows */
    char scr[4096];
    help_draw(&f, &h, scr, sizeof scr);
    help_send(&h, "/e");          /* most entries contain an e; the line takes a row */
    help_draw(&f, &h, scr, sizeof scr);
    help_send(&h, "\x1b[B\x1b[B\x1b[6~\x1b[F");   /* Down, Down, PgDn, End while typing */
    help_draw(&f, &h, scr, sizeof scr);
    ASSERT(strstr(scr, "/ e"), "the line stays");
    help_send(&h, "\x1b[H\x1b[5~\x1b[A");
    help_draw(&f, &h, scr, sizeof scr);
    fx_close(&f);
    tui_keyhelp_free(&h);

    /* The search buffer is not given if memory is short; '/' then does nothing. */
    tui_keyhelp_open(&h, &t, "list", "Search");
    fault_reset();
    fault_arm(F_CALLOC, 0, 1, ENOMEM);
    help_send(&h, "/");
    fault_reset();
    ASSERT_EQ_INT(h.dialog.answer, 0, "no line without a buffer");
    ASSERT_EQ_INT(help_send(&h, "q"), TUI_DIALOG_OK, "the help still works");
    tui_keyhelp_free(&h);

    /* A help of a bare dialog (no table) searches too. */
    tui_dialog d;
    const char *pairs[] = { "k", "Kilo", "m", "Mega" };
    tui_dialog_help(&d, "Bare", pairs, 2);
    tui_event ev = { .type = TUI_EV_KEY, .key = TUI_KEY_CHAR, .cp = '/' };
    tui_dialog_handle(&d, &ev);
    ASSERT_EQ_INT(d.answer, 1, "the line is open");
    ASSERT_EQ_INT(tui_dialog_is_checked(&d, 0), 0, "the search buffer is no tick");
    tui_dialog_free(&d);
}

/* ── Allocation failures ─────────────────────────────────────────────── */

static void test_allocation_failures(void) {
    fault_reset();
    tui_keytable t;
    tui_keytable_init(&t, g_table, N_TABLE, NULL, 0);
    tui_actionmenu m;
    tui_keyhelp h;
    errno = 0;
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_actionmenu_open(&m, &t, "list", "x"), -1, "menu: the block");
    ASSERT_EQ_INT(errno, ENOMEM, "errno");
    fault_reset();
    fault_arm(F_MALLOC, 1, 1, ENOMEM);                      /* the dialog's title copy */
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
    TEST_CASE(test_help_matches_the_handler),
    TEST_CASE(test_help_back_entry),
    TEST_CASE(test_help_picture),
    TEST_CASE(test_help_search_narrows_to_whole_entries),
    TEST_CASE(test_help_search_by_key_and_menu_key),
    TEST_CASE(test_help_search_esc_enter_and_empty),
    TEST_CASE(test_help_search_highlight_and_line_colours),
    TEST_CASE(test_help_search_scrolls_and_survives_failures),
    TEST_CASE(test_allocation_failures),
};

int main(int argc, char **argv) {
    setenv("TERM", "xterm-256color", 1);   /* the depth the picture tests expect */
    unsetenv("COLORTERM");
    return RUN_SUITE("actions", tests, 150, 60);
}
