/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Unit tests of tui_fieldset. */

/*
 * Unit tests of tui_fieldset, shaped after email-cli's compose dialog: an
 * address field with a contact drop-down and ";," separators, a plain
 * field, and issue-manager's choice field.  What it draws is rendered, fed
 * to libptytest's emulator and compared cell by cell.
 */

#define _GNU_SOURCE             /* strcasestr */
#include "test_helpers.h"
#include "tuikit/fieldset.h"

#include <errno.h>

int g_tests_run = 0;
int g_tests_failed = 0;

typedef struct {
    const char *addr, *name, *label;
} contact;

static const contact g_contacts[] = {
    { "alice@example.com", "Alice Smith", "alice@example.com  Alice Smith" },
    { "albert@example.org", "Albert", "albert@example.org  Albert" },
    { "bob@example.net", "Bob", "bob@example.net  Bob" },
};

static int g_complete_calls;
static char g_last_token[64];

/* email-cli's matching: a non-empty token, case-insensitive, in the
 * address or the name. */
static int complete_contacts(void *user, int field, const char *token,
                             tui_completion *out, int max) {
    (void)user;
    (void)field;
    g_complete_calls++;
    snprintf(g_last_token, sizeof(g_last_token), "%s", token);
    if (!token[0]) return 0;
    int n = 0;
    for (size_t i = 0; i < sizeof(g_contacts) / sizeof(g_contacts[0]) && n < max; i++) {
        if (strcasestr(g_contacts[i].addr, token) || strcasestr(g_contacts[i].name, token)) {
            out[n].text = g_contacts[i].addr;
            out[n].label = g_contacts[i].label;
            n++;
        }
    }
    return n;
}

static int validate(void *user, const tui_fieldset *f, char *err, size_t errsz) {
    (void)user;
    if (!f->fields[0].edit.buf[0]) {
        snprintf(err, errsz, "To: is empty \xE2\x80\x94 enter at least one recipient.");
        return 0;
    }
    return -1;
}

static const char *const g_types[] = { "Bug", "Feature", "Task" };

typedef struct {
    char to[128], cc[64], subj[64];
    tui_field fields[4];
    tui_fieldset f;
} compose;

static void compose_init(compose *c) {
    memset(c, 0, sizeof(*c));
    tui_field_text(&c->fields[0], "To:", c->to, sizeof(c->to), NULL);
    c->fields[0].separators = ";,";
    c->fields[0].complete = complete_contacts;
    tui_field_text(&c->fields[1], "Cc:", c->cc, sizeof(c->cc), NULL);
    tui_field_text(&c->fields[2], "Subject:", c->subj, sizeof(c->subj), NULL);
    tui_field_choice(&c->fields[3], "Type:", g_types, 3, 0);
    tui_fieldset_init(&c->f, c->fields, 4);
    c->f.validate = validate;
}

static tui_dialog_state send(tui_fieldset *f, const char *keys) {
    tui_decoder d;
    tui_decoder_init(&d);
    tui_decoder_feed(&d, keys, strlen(keys));
    tui_event ev;
    tui_dialog_state st = f->state;
    while (tui_decoder_next(&d, &ev)) st = tui_fieldset_handle(f, &ev);
    if (tui_decoder_timeout(&d, &ev)) st = tui_fieldset_handle(f, &ev);
    tui_decoder_free(&d);
    return st;
}

typedef struct {
    tui_screen *scr;
    tui_buf     buf;
    PtySession *s;
} Fx;

static int fx_open(Fx *x, int rows, int cols) {
    x->scr = tui_screen_new(rows, cols);
    tui_buf_init(&x->buf);
    x->s = pty_open(cols, rows);
    return x->scr && x->s ? 0 : -1;
}

static int fx_draw(Fx *x, tui_fieldset *f, int rows) {
    tui_canvas all = tui_canvas_of(x->scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    int used = tui_fieldset_render(f, tui_canvas_sub(all, tui_rect_make(0, 0, rows, tui_canvas_cols(all))));
    tui_buf_reset(&x->buf);
    tui_screen_render(x->scr, &x->buf);
    pty_feed(x->s, x->buf.data, x->buf.len);
    return used;
}

static void fx_close(Fx *x) {
    tui_screen_free(x->scr);
    tui_buf_free(&x->buf);
    if (x->s) pty_close(x->s);
}

static const PtyStyleKey legend[] = {
    { '.', PTY_ATTR_NONE, 0, 0 },
    /* The palette's roles (policies/tui-interface-model.md). */
    { 'A', PTY_ATTR_BOLD, PTY_FG_CYAN, PTY_BG_DEFAULT },     /* accent: the focused label */
    { 'B', PTY_ATTR_NONE, 97, PTY_COLOR_IDX(17) },        /* selection, 256 colours */
    { 'M', PTY_ATTR_BOLD, 97, PTY_COLOR_IDX(17) },        /* its '>' */
    { 'E', PTY_ATTR_BOLD, PTY_FG_RED, PTY_BG_DEFAULT },      /* error */
    { 0, 0, 0, 0 } };

static void test_layout_and_focus(void) {
    compose c;
    compose_init(&c);
    Fx x;
    ASSERT(fx_open(&x, 8, 50) == 0, "fixtures");
    ASSERT_EQ_INT(fx_draw(&x, &c.f, 8), 4, "four rows");
    const char *const expect[] = { "To:", "Cc:", "Subject:", "Type:    Bug", "", "", "", "", NULL };
    ASSERT_SCREEN_EQ(x.s, expect);
    const char *const labels[] = { "AAA", "...", "........", ".........", NULL };
    ASSERT_REGION_STYLE(x.s, PTY_REGION(0, 0, 4, 9), labels, legend);
    int r, col;
    pty_cursor(x.s, &r, &col);
    ASSERT_EQ_INT(r, 0, "cursor in To");
    ASSERT_EQ_INT(col, 9, "cursor after the label column");

    send(&c.f, "\t");
    ASSERT_EQ_INT(c.f.focus, 1, "Tab: next field");
    send(&c.f, "\x1b[B");
    ASSERT_EQ_INT(c.f.focus, 2, "Down: next field");
    send(&c.f, "\x1b[Z");
    ASSERT_EQ_INT(c.f.focus, 1, "Shift-Tab: previous field");
    send(&c.f, "\x1b[A\x1b[A");
    ASSERT_EQ_INT(c.f.focus, 3, "Up wraps to the last field");
    send(&c.f, "\t");
    ASSERT_EQ_INT(c.f.focus, 0, "Tab wraps to the first");

    /* The choice field: Right, Space, Left; focused it shows < value >. */
    tui_fieldset_focus(&c.f, 3);
    send(&c.f, "\x1b[C");
    ASSERT_EQ_INT(c.fields[3].choice, 1, "Right");
    send(&c.f, " ");
    ASSERT_EQ_INT(c.fields[3].choice, 2, "Space");
    send(&c.f, " ");
    ASSERT_EQ_INT(c.fields[3].choice, 0, "wraps forward");
    send(&c.f, "\x1b[D");
    ASSERT_EQ_INT(c.fields[3].choice, 2, "Left wraps back");
    send(&c.f, "x");
    ASSERT_EQ_INT(c.fields[3].choice, 2, "other keys ignored by a choice");
    fx_draw(&x, &c.f, 8);
    const char *const choice[] = { "Type:    < Task >", NULL };
    ASSERT_REGION_EQ(x.s, PTY_REGION(3, 0, 1, 50), choice);
    tui_fieldset_focus(&c.f, 99);
    ASSERT_EQ_INT(c.f.focus, 3, "focus clamped");
    tui_fieldset_focus(&c.f, -3);
    ASSERT_EQ_INT(c.f.focus, 0, "focus clamped below");
    tui_fieldset_free(&c.f);
    fx_close(&x);
}

static void test_completion(void) {
    compose c;
    compose_init(&c);
    Fx x;
    ASSERT(fx_open(&x, 8, 50) == 0, "fixtures");
    g_complete_calls = 0;
    send(&c.f, "al");
    ASSERT(g_complete_calls >= 2, "asked after every edit");
    ASSERT_EQ_STR(g_last_token, "al", "token");
    ASSERT_EQ_INT(c.f.ncand, 2, "two candidates");
    ASSERT_EQ_INT(fx_draw(&x, &c.f, 8), 6, "fields and drop-down rows");
    const char *const expect[] = {
        "To:      al",
        "           alice@example.com  Alice Smith",
        "           albert@example.org  Albert",
        "Cc:", "Subject:", "Type:    Bug", "", "", NULL };
    ASSERT_SCREEN_EQ(x.s, expect);
    /* TUI-17: nothing is chosen when the drop-down opens. */
    ASSERT_EQ_INT(c.f.cand_sel, -1, "nothing chosen at first");
    const char *const styles[] = { "", ".............", ".............", NULL };
    ASSERT_REGION_STYLE(x.s, PTY_REGION(0, 0, 3, 13), styles, legend);
    send(&c.f, "\t");
    ASSERT_EQ_STR(c.to, "al", "Tab: nothing longer is shared, nothing changes");
    ASSERT_EQ_INT(c.f.ncand, 2, "Tab leaves the drop-down open");
    ASSERT_EQ_INT(c.f.focus, 0, "Tab stays in the field");

    send(&c.f, "\x1b[B");
    ASSERT_EQ_INT(c.f.cand_sel, 0, "Down moves onto the first candidate");
    fx_draw(&x, &c.f, 8);
    /* TUI-49: the table's selection: '>' in bold, the item row on dark blue. */
    const char *const chosen[] = { "", ".........MBBB", ".............", NULL };
    ASSERT_REGION_STYLE(x.s, PTY_REGION(0, 0, 3, 13), chosen, legend);
    ASSERT_EQ_STR(pty_cell_text(x.s, 1, 9), ">", "the marker");
    ASSERT_EQ_STR(pty_cell_text(x.s, 1, 11), "a", "the text from the third column");
    ASSERT_EQ_INT(pty_cell_bg(x.s, 1, 49), PTY_COLOR_IDX(17), "the whole item row");
    ASSERT_EQ_INT(pty_cell_bg(x.s, 2, 49), PTY_COLOR_DEFAULT, "the others have none");
    send(&c.f, "\x1b[B\x1b[B");
    ASSERT_EQ_INT(c.f.cand_sel, 1, "Down at the last");
    send(&c.f, "\x1b[A\x1b[A");
    ASSERT_EQ_INT(c.f.cand_sel, -1, "Up from the first: back in the field");
    send(&c.f, "\x1b[A");
    ASSERT_EQ_INT(c.f.cand_sel, -1, "Up in the field: stays");
    ASSERT_EQ_INT(c.f.focus, 0, "focus stays while the drop-down is open");
    send(&c.f, "\x1b[Z");
    ASSERT_EQ_INT(c.f.focus, 3, "shift-Tab: no completion role, the previous field");
    tui_fieldset_focus(&c.f, 0);
    send(&c.f, "\x7f" "l");                      /* the drop-down again */
    ASSERT_EQ_INT(c.f.ncand, 2, "two candidates again");
    send(&c.f, "\x1b[B\x1b[B");
    ASSERT_EQ_INT(send(&c.f, "\r"), TUI_DIALOG_OPEN, "Enter on a chosen candidate accepts");
    ASSERT_EQ_STR(c.to, "albert@example.org", "the token replaced");
    ASSERT_EQ_INT(c.f.ncand, 0, "drop-down closed");

    /* A second address after a separator: only that token is replaced. */
    send(&c.f, "; bo");
    ASSERT_EQ_STR(g_last_token, "bo", "the token after the separator, spaces skipped");
    ASSERT_EQ_INT(c.f.ncand, 1, "one candidate");
    send(&c.f, "\t");
    ASSERT_EQ_STR(c.to, "albert@example.org; bob@example.net", "Tab: one candidate, written out");
    ASSERT_EQ_INT(c.f.ncand, 0, "and the drop-down closes");

    /* Esc closes the drop-down and keeps the text; typing opens it again. */
    send(&c.f, ", AL");
    ASSERT_EQ_INT(c.f.ncand, 2, "case-insensitive match");
    ASSERT_EQ_INT(send(&c.f, "\x1b"), TUI_DIALOG_OPEN, "first Esc only closes the list");
    ASSERT_EQ_INT(c.f.ncand, 0, "closed");
    ASSERT_EQ_STR(c.to, "albert@example.org; bob@example.net, AL", "text kept");
    send(&c.f, "i");
    ASSERT_EQ_INT(c.f.ncand, 1, "typing reopens it");
    ASSERT_EQ_INT(send(&c.f, "\x03"), TUI_DIALOG_OPEN, "Ctrl-C too only closes the list");
    send(&c.f, "\x7f\x7f\x7f");
    ASSERT_EQ_INT(c.f.ncand, 0, "an empty token: no candidates");
    ASSERT_EQ_INT(send(&c.f, "\x1b"), TUI_DIALOG_CANCELLED, "Esc without a list cancels");
    ASSERT_EQ_INT(send(&c.f, "a"), TUI_DIALOG_CANCELLED, "closed stays closed");
    tui_fieldset_free(&c.f);
    fx_close(&x);
}

static void set_env(const char *name, const char *value) {
    if (value) setenv(name, value, 1);
    else unsetenv(name);
}

/* TUI-49: the candidate selection follows the colour depth, and a
 * cand_sel_style the program set is its own choice. */
static void test_candidate_selection_depth_and_own_style(void) {
    compose c;
    compose_init(&c);
    Fx x;
    ASSERT(fx_open(&x, 8, 50) == 0, "fixtures");
    const char *oterm = getenv("TERM"), *oct = getenv("COLORTERM");
    char saveterm[64] = "", savect[64] = "";
    int hadterm = oterm != NULL, hadct = oct != NULL;
    if (oterm) snprintf(saveterm, sizeof(saveterm), "%s", oterm);
    if (oct) snprintf(savect, sizeof(savect), "%s", oct);
    char desc[160];

    send(&c.f, "al\x1b[B");
    set_env("COLORTERM", NULL);
    set_env("TERM", "screen");                       /* 16 colours */
    fx_draw(&x, &c.f, 8);
    ASSERT_EQ_INT(pty_cell_bg(x.s, 1, 20), PTY_BG_BLUE, "16 colours: blue");
    ASSERT_EQ_INT(pty_cell_bg(x.s, 2, 20), PTY_BG_DEFAULT, "the other candidate: none");
    set_env("TERM", "linux");                        /* 8 colours */
    fx_draw(&x, &c.f, 8);
    ASSERT_EQ_INT(pty_cell_bg(x.s, 1, 20), PTY_BG_DEFAULT, "8 colours: no background");
    pty_cell_describe(x.s, 1, 11, desc, sizeof(desc));
    ASSERT(strstr(desc, "bold") != NULL, "8 colours: bold");
    ASSERT(strstr(desc, "reverse") == NULL, "8 colours: not reverse video");
    ASSERT_EQ_STR(pty_cell_text(x.s, 1, 9), ">", "the marker");

    set_env("TERM", "xterm-256color");
    c.f.cand_sel_style = tui_style_make(TUI_GREEN, TUI_COLOR_DEFAULT, TUI_ATTR_UNDERLINE);
    fx_draw(&x, &c.f, 8);
    pty_cell_describe(x.s, 1, 11, desc, sizeof(desc));
    ASSERT(strstr(desc, "underline") != NULL, "own style kept: underline");
    ASSERT(strstr(desc, "green") != NULL, "own style kept: green");
    ASSERT_EQ_INT(pty_cell_bg(x.s, 1, 20), PTY_BG_DEFAULT, "own style: no dark blue");
    ASSERT_EQ_STR(pty_cell_text(x.s, 1, 9), ">", "own style: the marker");
    ASSERT_EQ_STR(pty_cell_text(x.s, 2, 9), " ", "the others: blanks");

    set_env("TERM", hadterm ? saveterm : NULL);
    set_env("COLORTERM", hadct ? savect : NULL);
    tui_fieldset_free(&c.f);
    fx_close(&x);
}

static void test_validation_and_submit(void) {
    compose c;
    compose_init(&c);
    Fx x;
    ASSERT(fx_open(&x, 6, 60) == 0, "fixtures");
    tui_fieldset_focus(&c.f, 2);
    ASSERT_EQ_INT(send(&c.f, "\r"), TUI_DIALOG_OPEN, "invalid: stays open");
    ASSERT_EQ_INT(c.f.focus, 0, "focus on the field to fix");
    ASSERT_EQ_INT(fx_draw(&x, &c.f, 6), 5, "error row added");
    const char *const expect[] = {
        "To:", "Cc:", "Subject:", "Type:    Bug",
        "To: is empty \xE2\x80\x94 enter at least one recipient.", "", NULL };
    ASSERT_SCREEN_EQ(x.s, expect);
    const char *const err[] = { "EEEEEEEEEEEEEEE", NULL };
    ASSERT_REGION_STYLE(x.s, PTY_REGION(4, 0, 1, 15), err, legend);
    send(&c.f, "zed@host");                    /* matches no contact: no drop-down */
    ASSERT_EQ_STR(c.f.error, "", "typing clears the message");
    ASSERT_EQ_INT(c.f.ncand, 0, "no drop-down to take the Enter");
    ASSERT_EQ_INT(send(&c.f, "\r"), TUI_DIALOG_OK, "valid: closes");

    /* Without a validator Enter closes at once; set_error by hand. */
    compose d;
    compose_init(&d);
    d.f.validate = NULL;
    tui_fieldset_set_error(&d.f, "a note");
    ASSERT_EQ_STR(d.f.error, "a note", "set");
    tui_fieldset_set_error(&d.f, NULL);
    ASSERT_EQ_STR(d.f.error, "", "cleared");
    char long_msg[400];
    memset(long_msg, 'm', sizeof(long_msg) - 1);
    long_msg[sizeof(long_msg) - 1] = '\0';
    tui_fieldset_set_error(&d.f, long_msg);
    ASSERT_EQ_INT(strlen(d.f.error), sizeof(d.f.error) - 1, "truncated to the buffer");
    ASSERT_EQ_INT(send(&d.f, "\r"), TUI_DIALOG_OK, "Enter closes");
    tui_fieldset_free(&d.f);
    tui_fieldset_free(&c.f);
    fx_close(&x);
}

static void test_render_limits_and_edges(void) {
    compose c;
    compose_init(&c);
    Fx x;
    ASSERT(fx_open(&x, 4, 30) == 0, "fixtures");
    send(&c.f, "a");
    ASSERT_EQ_INT(c.f.ncand, 3, "candidates: a is in every address (example)");
    ASSERT_EQ_INT(fx_draw(&x, &c.f, 2), 2, "only the rows the canvas has");
    /* Unfocused text shows its start; focused choice hides the cursor. */
    tui_fieldset_focus(&c.f, 3);
    fx_draw(&x, &c.f, 4);
    const char *const expect[] = { "To:      a", "Cc:", "Subject:", "Type:    < Bug >", NULL };
    ASSERT_SCREEN_EQ(x.s, expect);
    tui_buf_reset(&x.buf);
    tui_screen_render(x.scr, &x.buf);
    tui_fieldset_free(&c.f);

    /* No fields, no labels, a choice without choices. */
    tui_fieldset empty;
    tui_fieldset_init(&empty, NULL, 3);
    ASSERT_EQ_INT(empty.nfields, 0, "no fields");
    ASSERT_EQ_INT(send(&empty, "a\t"), TUI_DIALOG_OPEN, "keys do nothing");
    ASSERT_EQ_INT(fx_draw(&x, &empty, 4), 0, "nothing drawn");
    tui_fieldset_focus(&empty, 1);
    tui_fieldset_update_completion(&empty);
    tui_field lone[2];
    tui_field_choice(&lone[0], NULL, NULL, 5, 3);
    ASSERT_EQ_INT(lone[0].nchoices, 0, "no choices");
    tui_field_choice(&lone[1], "Pick:", g_types, 3, 9);
    ASSERT_EQ_INT(lone[1].choice, 2, "selection clamped");
    tui_field_choice(&lone[1], "Pick:", g_types, 3, -1);
    ASSERT_EQ_INT(lone[1].choice, 0, "selection clamped below");
    tui_fieldset fs;
    tui_fieldset_init(&fs, lone, 2);
    send(&fs, "\x1b[C ");
    ASSERT_EQ_INT(lone[0].choice, 0, "an empty choice does not move");
    fx_draw(&x, &fs, 4);
    tui_fieldset_focus(&fs, 1);
    tui_fieldset_update_completion(&fs);
    ASSERT_EQ_INT(fs.ncand, 0, "a choice has no completion");
    tui_fieldset_free(&fs);
    ASSERT_EQ_INT(tui_fieldset_handle(NULL, NULL), TUI_DIALOG_CANCELLED, "NULL");
    ASSERT_EQ_INT(tui_fieldset_handle(&fs, NULL), TUI_DIALOG_OPEN, "NULL event");
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_RESIZE;
    ASSERT_EQ_INT(tui_fieldset_handle(&fs, &ev), TUI_DIALOG_OPEN, "not a key");
    ASSERT_EQ_INT(tui_fieldset_render(NULL, tui_canvas_of(x.scr)), 0, "render NULL");
    tui_fieldset_init(NULL, NULL, 0);
    tui_fieldset_free(NULL);
    tui_fieldset_focus(NULL, 0);
    tui_fieldset_set_error(NULL, "x");
    tui_fieldset_update_completion(NULL);
    tui_field_text(NULL, "x", NULL, 0, NULL);
    tui_field_choice(NULL, "x", NULL, 0, 0);
    fx_close(&x);
}

/* TUI-17: Tab writes what every candidate shares, cut on a character. */
static const char *const *g_prefix_set;
static int complete_set(void *user, int field, const char *token,
                        tui_completion *out, int max) {
    (void)user;
    (void)field;
    if (!token[0]) return 0;
    int n = 0;
    for (int i = 0; g_prefix_set[i] && n < max; i++) {
        if (!strncmp(g_prefix_set[i], token, strlen(token))) {
            out[n].text = g_prefix_set[i];
            out[n].label = NULL;
            n++;
        }
    }
    return n;
}

static void test_tab_common_prefix(void) {
    char buf[64];
    tui_field fl;
    tui_field_text(&fl, "To:", buf, sizeof(buf), NULL);
    fl.complete = complete_set;
    fl.separators = ";";
    tui_fieldset f;
    tui_fieldset_init(&f, &fl, 1);
    static const char *const names[] = { "anna@x.org", "anne@x.org", "annika@y.org", "bob@z.org", NULL };
    g_prefix_set = names;
    send(&f, "a");
    ASSERT_EQ_INT(f.ncand, 3, "three candidates");
    send(&f, "\t");
    ASSERT_EQ_STR(buf, "ann", "Tab: the prefix all three share");
    ASSERT_EQ_INT(f.ncand, 3, "still three");
    send(&f, "e\t");
    ASSERT_EQ_STR(buf, "anne@x.org", "one left: written out");
    ASSERT_EQ_INT(f.ncand, 0, "closed");
    send(&f, ";a\x1b[B\x1b[B\x1b[B\r");
    ASSERT_EQ_STR(buf, "anne@x.org;annika@y.org", "after a separator: Down three times, Enter");
    tui_fieldset_free(&f);

    /* A shared prefix that ends inside a character is cut before it. */
    static const char *const accents[] = { "x\xC3\xA9a", "x\xC3\xA8b", NULL };
    g_prefix_set = accents;
    tui_field_text(&fl, "To:", buf, sizeof(buf), NULL);
    fl.complete = complete_set;
    tui_fieldset_init(&f, &fl, 1);
    send(&f, "x");
    ASSERT_EQ_INT(f.ncand, 2, "two candidates");
    send(&f, "\t");
    ASSERT_EQ_STR(buf, "x", "two accents share a byte, not a character");
    tui_fieldset_free(&f);
}

/* A completion returning more than fits, a negative count, a NULL text and
 * an over-long token are all handled. */
static int complete_odd(void *user, int field, const char *token,
                        tui_completion *out, int max) {
    (void)field;
    int mode = *(int *)user;
    snprintf(g_last_token, sizeof(g_last_token), "%.60s", token);
    if (mode == 0) return -5;
    if (mode == 1) {
        for (int i = 0; i < max; i++) { out[i].text = NULL; out[i].label = NULL; }
        return max + 10;
    }
    out[0].text = "X";
    out[0].label = NULL;
    return 1;
}

static void test_completion_edges(void) {
    char buf[600];
    tui_field fl;
    tui_field_text(&fl, "F:", buf, sizeof(buf), NULL);
    fl.complete = complete_odd;
    tui_fieldset f;
    tui_fieldset_init(&f, &fl, 1);
    int mode = 0;
    f.user = &mode;
    send(&f, "q");
    ASSERT_EQ_INT(f.ncand, 0, "a negative count is none");
    mode = 1;
    send(&f, "q");
    ASSERT_EQ_INT(f.ncand, TUI_FIELDSET_CAND_MAX, "capped");
    send(&f, "\t");
    ASSERT_EQ_STR(buf, "qq", "Tab: NULL texts share nothing");
    send(&f, "\x1b[B\r");
    ASSERT_EQ_STR(buf, "", "a NULL text replaces the token with nothing");
    Fx x;
    ASSERT(fx_open(&x, 4, 20) == 0, "fixtures");
    mode = 1;
    send(&f, "z");
    fx_draw(&x, &f, 4);
    mode = 2;
    char longtok[560];
    memset(longtok, 'k', sizeof(longtok) - 1);
    longtok[sizeof(longtok) - 1] = '\0';
    tui_lineedit_set_text(&fl.edit, longtok);
    tui_fieldset_update_completion(&f);
    ASSERT_EQ_INT((int)strlen(g_last_token), 60, "a long token is cut, not overrun");
    send(&f, "\x1b[B\r");
    ASSERT_EQ_INT(buf[strlen(buf) - 1], 'X', "accepted");
    tui_fieldset_free(&f);
    fx_close(&x);
}

static void test_lineedit_replace(void) {
    char buf[32];
    tui_lineedit le;
    tui_lineedit_init(&le, buf, sizeof(buf), "a\xC3\xA9z");     /* a é z */
    tui_lineedit_replace(&le, 2, 3, "Q");                     /* inside é: clamped to it */
    ASSERT_EQ_STR(buf, "a" "Qz", "the character containing the range is replaced");
    ASSERT_EQ_INT(le.cur, 2, "cursor after the new text");
    tui_lineedit_replace(&le, 99, 120, "!");
    ASSERT_EQ_STR(buf, "aQz!", "beyond the end: appended");
    tui_lineedit_replace(&le, 3, 1, NULL);
    ASSERT_EQ_STR(buf, "aQz!", "an empty range and no text: nothing");
    ASSERT_EQ_INT(le.cur, 3, "cursor moved to start");
    tui_lineedit_replace(NULL, 0, 0, "x");
    tui_lineedit_free(&le);
}

/* ── TUI-131: the form in a modal frame ─────────────────────────────── */

static tui_dialog_state fsend(tui_dialog_form *d, const char *keys) {
    tui_decoder dec;
    tui_decoder_init(&dec);
    tui_decoder_feed(&dec, keys, strlen(keys));
    tui_event ev;
    tui_dialog_state st = d->form->state;
    while (tui_decoder_next(&dec, &ev)) st = tui_dialog_form_handle(d, &ev);
    if (tui_decoder_timeout(&dec, &ev)) st = tui_dialog_form_handle(d, &ev);
    tui_decoder_free(&dec);
    return st;
}

/* The row and column where @p text starts on the emulated screen, or -1. */
static int find_text(PtySession *s, int rows, int cols, const char *text, int *row, int *col) {
    size_t n = strlen(text);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c + (int)n <= cols; c++) {
            size_t k = 0;
            while (k < n && strcmp(pty_cell_text(s, r, c + (int)k), (char[2]){ text[k], 0 }) == 0) k++;
            if (k == n) {
                *row = r;
                *col = c;
                return 0;
            }
        }
    return -1;
}

static int validate_nonempty(void *user, const tui_fieldset *f, char *err, size_t errsz) {
    (void)user;
    if (f->fields[0].edit.buf[0]) return -1;
    snprintf(err, errsz, "To is empty");
    return 0;
}

static void test_dialog_form(void) {
    char name[32], url[32];
    static const char *const kinds[] = { "web", "native" };
    tui_field fields[3];
    tui_field_text(&fields[0], "Name:", name, sizeof name, "");
    tui_field_text(&fields[1], "Redirect URL:", url, sizeof url, "");
    tui_field_choice(&fields[2], "Type:", kinds, 2, 0);
    tui_fieldset fs;
    tui_fieldset_init(&fs, fields, 3);
    tui_dialog_form d;
    ASSERT_EQ_INT(tui_dialog_form_init(&d, "New client", NULL, &fs), 0, "create");

    tui_screen *scr = tui_screen_new(14, 60);
    PtySession *s = pty_open(60, 14);
    ASSERT(scr && s, "fixtures");
    tui_buf out;
    tui_buf_init(&out);
    tui_canvas_clear(tui_canvas_of(scr), TUI_STYLE_DEFAULT);
    tui_dialog_form_render(&d, tui_canvas_of(scr));
    tui_screen_render(scr, &out);
    pty_feed(s, out.data, out.len);

    int r, c, tr, tc, br, bc;
    ASSERT(pty_screen_contains(s, "New client"), "the title");
    ASSERT(pty_screen_contains(s, "Name:"), "a label");
    ASSERT(pty_screen_contains(s, "Redirect URL:"), "the widest label");
    ASSERT(pty_screen_contains(s, "web"), "the choice");
    ASSERT_EQ_INT(find_text(s, 14, 60, TUI_DIALOG_FORM_HINT, &br, &bc), 0, "the key hint");
    ASSERT_EQ_INT(find_text(s, 14, 60, "New client", &tr, &tc), 0, "the title position");
    ASSERT(br > tr + 3, "the hint is in the bottom border, below the fields");
    ASSERT_EQ_STR(pty_cell_text(s, tr, tc - 3), "┌", "the title is in the top border");
    ASSERT_EQ_STR(pty_cell_text(s, br, bc - 1), "─", "the hint sits in the bottom border");
    ASSERT_EQ_STR(pty_cell_text(s, br, bc - 2), "└", "the bottom-left corner");
    int rn, cn;
    ASSERT_EQ_INT(find_text(s, 14, 60, "Name:", &rn, &cn), 0, "label position");
    ASSERT_EQ_STR(pty_cell_text(s, rn, cn - 2), "│", "the fields are inside the frame");
    ASSERT_EQ_INT(pty_cell_fg(s, tr, tc), PTY_FG_CYAN, "the frame colour (the accent)");
    ASSERT_EQ_INT(pty_cell_fg(s, br, bc), PTY_FG_CYAN, "the hint is in the frame colour");
    pty_cursor(s, &r, &c);
    ASSERT_EQ_INT(r, rn, "the cursor is in the first field");

    /* The centre: the box is as far from the left as from the right. */
    int right = -1;
    for (int col = 59; col > 0 && right < 0; col--)
        if (strcmp(pty_cell_text(s, tr, col), "┐") == 0) right = col;
    int left = tc - 3;
    ASSERT(right > 0 && (left - (59 - right)) >= -1 && (left - (59 - right)) <= 1, "centred");

    ASSERT_EQ_INT(fsend(&d, "acme\t"), TUI_DIALOG_OPEN, "typing and Tab");
    ASSERT_EQ_STR(name, "acme", "typed into the first field");
    ASSERT_EQ_INT(fs.focus, 1, "Tab moves on");
    fsend(&d, "https://x.example/cb\t\x1b[C");
    ASSERT_EQ_STR(url, "https://x.example/cb", "second field");
    ASSERT_EQ_INT(fields[2].choice, 1, "Right changes the choice");
    ASSERT_EQ_INT(fsend(&d, "\r"), TUI_DIALOG_OK, "Enter submits");
    ASSERT_EQ_INT(fsend(&d, "\x1b"), TUI_DIALOG_OK, "a closed dialog stays closed");
    ASSERT_EQ_INT(fs.state, TUI_DIALOG_OK, "the form's state is the outcome");

    /* Esc cancels. */
    fs.state = TUI_DIALOG_OPEN;
    ASSERT_EQ_INT(fsend(&d, "\x1b"), TUI_DIALOG_CANCELLED, "Esc cancels");
    tui_dialog_form_free(&d);
    tui_dialog_form_free(&d);
    tui_dialog_form_free(NULL);
    tui_buf_free(&out);
    tui_screen_free(scr);
    pty_close(s);
    tui_fieldset_free(&fs);
}

/* A validator keeps the dialog open and shows its reason inside the frame;
 * a custom hint replaces the default; a completing field gets room for its drop-down. */
static void test_dialog_form_validation_and_sizes(void) {
    char to[32];
    tui_field fl;
    tui_field_text(&fl, "To:", to, sizeof to, "");
    fl.complete = complete_contacts;
    fl.separators = ";,";
    tui_fieldset fs;
    tui_fieldset_init(&fs, &fl, 1);
    fs.validate = validate_nonempty;
    tui_dialog_form d;
    ASSERT_EQ_INT(tui_dialog_form_init(&d, "A very long title for a small form dialog, cut to the screen",
                                       " Enter send ", &fs), 0, "create");
    tui_screen *scr = tui_screen_new(12, 40);
    PtySession *s = pty_open(40, 12);
    tui_buf out;
    tui_buf_init(&out);
    ASSERT_EQ_INT(fsend(&d, "\r"), TUI_DIALOG_OPEN, "the validator keeps it open");
    tui_canvas_clear(tui_canvas_of(scr), TUI_STYLE_DEFAULT);
    tui_dialog_form_render(&d, tui_canvas_of(scr));
    tui_screen_render(scr, &out);
    pty_feed(s, out.data, out.len);
    ASSERT(pty_screen_contains(s, "To is empty"), "the reason, inside the frame");
    ASSERT(pty_screen_contains(s, " Enter send "), "the given hint");
    ASSERT(!pty_screen_contains(s, "Tab next"), "not the default one");
    int row, col;
    ASSERT_EQ_INT(find_text(s, 12, 40, "To is empty", &row, &col), 0, "reason position");
    ASSERT_EQ_STR(pty_cell_text(s, row, col - 2), "│", "left border beside it");

    fsend(&d, "al\x1b[B");
    tui_buf_reset(&out);
    tui_canvas_clear(tui_canvas_of(scr), TUI_STYLE_DEFAULT);
    tui_dialog_form_render(&d, tui_canvas_of(scr));
    tui_screen_render(scr, &out);
    pty_feed(s, out.data, out.len);
    ASSERT(pty_screen_contains(s, "alice@example.com"), "the drop-down is inside the box");
    ASSERT(pty_screen_contains(s, " Enter send "), "the hint stays under the drop-down");

    /* A canvas too small for the box: clamped, nothing drawn outside it. */
    tui_screen *tiny = tui_screen_new(4, 10);
    tui_dialog_form_render(&d, tui_canvas_of(tiny));
    tui_screen_free(tiny);
    tui_screen *one = tui_screen_new(1, 1);
    tui_dialog_form_render(&d, tui_canvas_of(one));
    tui_screen_free(one);

    tui_dialog_form_render(NULL, tui_canvas_of(scr));
    tui_dialog_form_render(&d, (tui_canvas){ 0 });
    ASSERT_EQ_INT(tui_dialog_form_handle(NULL, NULL), TUI_DIALOG_CANCELLED, "no dialog");
    errno = 0;
    ASSERT_EQ_INT(tui_dialog_form_init(&d, "t", NULL, NULL), -1, "no form");
    ASSERT_EQ_INT(errno, EINVAL, "errno");
    ASSERT_EQ_INT(tui_dialog_form_init(NULL, "t", NULL, &fs), -1, "no dialog");

    tui_dialog_form_free(&d);
    tui_buf_free(&out);
    tui_screen_free(scr);
    pty_close(s);
    tui_fieldset_free(&fs);
}

static const TestCase tests[] = {
    TEST_CASE(test_dialog_form),
    TEST_CASE(test_dialog_form_validation_and_sizes),
    TEST_CASE(test_layout_and_focus),
    TEST_CASE(test_completion),
    TEST_CASE(test_candidate_selection_depth_and_own_style),
    TEST_CASE(test_tab_common_prefix),
    TEST_CASE(test_validation_and_submit),
    TEST_CASE(test_render_limits_and_edges),
    TEST_CASE(test_completion_edges),
    TEST_CASE(test_lineedit_replace),
};

int main(int argc, char **argv) {
    setenv("TERM", "xterm-256color", 1);   /* the depth the picture tests expect */
    unsetenv("COLORTERM");
    return RUN_SUITE("fieldset", tests, 80, 60);
}
