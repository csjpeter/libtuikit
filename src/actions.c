/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-18 */
/* The key table, the action menu and the help made from the table. */

#include "tuikit/actions.h"
#include "tuikit/textview.h"
#include "internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Keys ────────────────────────────────────────────────────────────── */

tui_verb tui_registry_verb(tui_keycode key) {
    switch (key) {
    case TUI_KC_STAR:     return TUI_VERB_STAR;
    case TUI_KC_JUNK:     return TUI_VERB_JUNK;
    case TUI_KC_READ:     return TUI_VERB_READ;
    case TUI_KC_UNREAD:   return TUI_VERB_UNREAD;
    case TUI_KC_DELETE_D:
    case TUI_KC_DELETE:   return TUI_VERB_DELETE;
    case TUI_KC_NEW:      return TUI_VERB_NEW;
    case TUI_KC_EDIT:     return TUI_VERB_EDIT;
    case TUI_KC_MOVE:     return TUI_VERB_MOVE;
    case TUI_KC_MESSAGE:  return TUI_VERB_MESSAGE;
    default:              return TUI_VERB_NONE;
    }
}

int tui_key_reserved(tui_keycode key) {
    return key > 0 && key < 128 && strchr("rsfpocauilhx/jkqgGU?:", (int)key) != NULL;
}

tui_keycode tui_keycode_of(const tui_event *ev) {
    if (!ev || ev->type != TUI_EV_KEY || (ev->mods & TUI_MOD_ALT)) return TUI_KC_NONE;
    if (ev->key == TUI_KEY_CHAR) {
        if (ev->mods & TUI_MOD_CTRL) return TUI_KC_CTRL | ev->cp;
        return ev->cp;
    }
    return ev->mods ? TUI_KC_NONE : (tui_keycode)ev->key;
}

static const char *const g_special[] = {
    "Enter", "Tab", "Backspace", "Esc", "Up", "Down", "Left", "Right", "Home", "End",
    "PageUp", "PageDown", "Insert", "Delete" };

int tui_keycode_name(tui_keycode key, char *buf, size_t size) {
    char tmp[32];
    if (key & TUI_KC_CTRL) {
        snprintf(tmp, sizeof tmp, "ctrl-%c", (char)(key & 0x7f));
    } else if (key == ' ') {
        snprintf(tmp, sizeof tmp, "Space");
    } else if (key >= TUI_KEY_F1 && key <= TUI_KEY_F12) {
        snprintf(tmp, sizeof tmp, "F%u", (unsigned)(key - TUI_KEY_F1 + 1));
    } else if (key >= TUI_KEY_ENTER && key < TUI_KEY_ENTER + sizeof g_special / sizeof g_special[0]) {
        snprintf(tmp, sizeof tmp, "%s", g_special[key - TUI_KEY_ENTER]);
    } else if (key > 0 && key < 0x110000) {
        char *p = tmp;
        if (key < 0x80) {
            *p++ = (char)key;
        } else if (key < 0x800) {
            *p++ = (char)(0xC0 | (key >> 6));
            *p++ = (char)(0x80 | (key & 0x3F));
        } else if (key < 0x10000) {
            *p++ = (char)(0xE0 | (key >> 12));
            *p++ = (char)(0x80 | ((key >> 6) & 0x3F));
            *p++ = (char)(0x80 | (key & 0x3F));
        } else {
            *p++ = (char)(0xF0 | (key >> 18));
            *p++ = (char)(0x80 | ((key >> 12) & 0x3F));
            *p++ = (char)(0x80 | ((key >> 6) & 0x3F));
            *p++ = (char)(0x80 | (key & 0x3F));
        }
        *p = '\0';
    } else {
        tmp[0] = '\0';
    }
    if (buf && size) snprintf(buf, size, "%s", tmp);
    return (int)strlen(tmp);
}

tui_nav tui_nav_key(const tui_event *ev, int depth) {
    tui_keycode k = tui_keycode_of(ev);
    if (k == TUI_KC_QUIT) return TUI_NAV_QUIT;
    if (k == TUI_KC_BACK || k == TUI_KC_BACKSPACE) return depth > 0 ? TUI_NAV_BACK : TUI_NAV_NONE;
    if (k == TUI_KC_ESC) return depth > 0 ? TUI_NAV_BACK : TUI_NAV_QUIT;
    return TUI_NAV_NONE;
}

/* ── The table ───────────────────────────────────────────────────────── */

static int overlap(const char *a, const char *b) {
    return !a || !b || strcmp(a, b) == 0;
}

static int needs_confirm(const tui_keyentry *e) {
    return e->cls == TUI_ACT_SERIOUS || e->irreversible;
}

static int applies(const tui_keyentry *e, const char *view) {
    return !e->view || (view && strcmp(e->view, view) == 0);
}

static int refuse(char *why, size_t size, int i, const tui_keyentry *e, const char *reason) {
    if (why && size)
        snprintf(why, size, "key table entry %d (%s): %s", i, e->action ? e->action : "?", reason);
    errno = EINVAL;
    return -1;
}

/* How many lines @p text takes wrapped to the help's text width; -1 on no memory. */
static int desc_lines(const char *text) {
    tui_textview tv;
    tui_textview_init(&tv);
    int lines = -1;
    if (tui_textview_set_text(&tv, text) == 0) lines = tui_textview_wrap(&tv, TUI__HELP_TEXT_COLS);
    tui_textview_free(&tv);
    return lines;
}

int tui_keytable_init(tui_keytable *t, const tui_keyentry *entries, int n, char *why,
                      size_t size) {
    if (why && size) why[0] = '\0';
    if (!t || n < 0 || (!entries && n > 0)) {
        errno = EINVAL;
        return -1;
    }
    for (int i = 0; i < n; i++) {
        const tui_keyentry *e = &entries[i];
        if (!e->action && e->desc) {
            /* The description row of a view: no action, only the text. */
            if (!e->desc[0] || e->key != TUI_KC_NONE || e->verb != TUI_VERB_NONE ||
                e->cls != TUI_ACT_HARMLESS || e->irreversible || e->confirm)
                return refuse(why, size, i, e, "a view description row carries only the text");
            for (int j = 0; j < i; j++)
                if (!entries[j].action && entries[j].desc && overlap(entries[j].view, e->view))
                    return refuse(why, size, i, e, "a second description for one view");
            int lines = desc_lines(e->desc);
            if (lines < 0) {
                errno = ENOMEM;
                return -1;
            }
            if (lines > 3)
                return refuse(why, size, i, e, "a view description is at most three lines");
            continue;
        }
        if (!e->action || !e->action[0]) return refuse(why, size, i, e, "no action name");
        if ((int)e->cls < TUI_ACT_HARMLESS || e->cls > TUI_ACT_RARE)
            return refuse(why, size, i, e, "unknown class");
        if (needs_confirm(e) && (!e->confirm || !strstr(e->confirm, "%s")))
            return refuse(why, size, i, e,
                          "a serious or irreversible action needs a confirmation naming the target (%s)");
        if (e->key != TUI_KC_NONE) {
            tui_verb reg = tui_registry_verb(e->key);
            if (e->cls == TUI_ACT_HARMLESS) {
                if (reg != TUI_VERB_NONE)
                    return refuse(why, size, i, e,
                                  "a harmless action on a key of the registry, which is for state changes");
            } else if (e->cls == TUI_ACT_RARE) {
                return refuse(why, size, i, e, "a rare action has no direct key: it is in the menu");
            } else if (reg == TUI_VERB_NONE) {
                return refuse(why, size, i, e, "a state-changing direct key that is not in the registry");
            } else if (e->verb != reg) {
                return refuse(why, size, i, e, "the key is the registry key of another verb");
            }
            for (int j = 0; j < i; j++)
                if (entries[j].key == e->key && overlap(entries[j].view, e->view))
                    return refuse(why, size, i, e, "the key is bound twice in one view");
        }
    }
    t->entries = entries;
    t->n = n;
    return 0;
}

int tui_keytable_find(const tui_keytable *t, const char *view, const tui_event *ev) {
    tui_keycode k = tui_keycode_of(ev);
    if (!t || k == TUI_KC_NONE) return -1;
    for (int i = 0; i < t->n; i++)
        if (t->entries[i].key == k && applies(&t->entries[i], view)) return i;
    return -1;
}

/* ── The action menu ─────────────────────────────────────────────────── */

/* The question with every "%s" replaced by the target; the caller frees. */
static char *question(const char *confirm, const char *target) {
    size_t tl = target ? strlen(target) : 0, cl = strlen(confirm), n = cl + 1;
    for (const char *p = confirm; (p = strstr(p, "%s")); p += 2) n += tl;
    char *out = malloc(n), *o = out;
    if (!out) return NULL;
    for (const char *p = confirm; *p;) {
        if (p[0] == '%' && p[1] == 's') {
            if (tl) memcpy(o, target, tl);
            o += tl;
            p += 2;
        } else {
            *o++ = *p++;
        }
    }
    *o = '\0';
    return out;
}

static int start_confirm(tui_actionmenu *m, int entry, const char *target) {
    const tui_keyentry *e = &m->table->entries[entry];
    char *text = question(e->confirm, target);
    if (!text) return -1;
    if (m->have_dialog) tui_dialog_free(&m->dialog);
    m->have_dialog = 0;
    int rc = tui_dialog_confirm(&m->dialog, e->action, text, 0);   /* the default is No */
    free(text);
    if (rc != 0) return -1;
    m->have_dialog = 1;
    m->confirming = 1;
    m->entry = entry;
    return 0;
}

/* ── The quick keys ──────────────────────────────────────────────────── */

/* The verbs several programs offer, and the letter each gets in every one
 * of them (tui-interface-model.md, "Every action in the menu has a quick
 * key").  No letter here is j, k, g or q. */
static const struct { const char *verb; char letter; } g_verbs[] = {
    { "archive", 'a' }, { "start", 'b' },  { "copy", 'c' },    { "delete", 'd' },
    { "edit", 'e' },    { "forward", 'f' }, { "label", 'l' },  { "move", 'm' },
    { "new", 'n' },     { "open", 'o' },   { "pause", 'p' },   { "reload", 'r' },
    { "restart", 't' }, { "unarchive", 'u' }, { "revert", 'v' }, { "rename", 'w' },
    { "stop", 'x' },
};

/* A quick key is a letter or a digit 1-9; the list keys (j k g G q) are the
 * menu's own and never quick keys. */
static int assignable(int c) {
    if (c >= '1' && c <= '9') return 1;
    if (c == 'j' || c == 'k' || c == 'g' || c == 'G' || c == 'q') return 0;
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int in_menu(const tui_keyentry *e, const char *view) {
    return e->action && e->cls != TUI_ACT_HARMLESS && applies(e, view);
}

static int ascii_lower(int c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

/* 1 when the first word of @p action is @p verb (case does not matter). */
static int first_word_is(const char *action, const char *verb) {
    size_t n = strlen(verb);
    for (size_t i = 0; i < n; i++)
        if (ascii_lower((unsigned char)action[i]) != verb[i]) return 0;
    return action[n] == '\0' || action[n] == ' ';
}

int tui_keytable_assign_quick(const tui_keytable *t, const char *view, const char *given,
                              char *out) {
    if (!t || !out) {
        errno = EINVAL;
        return -1;
    }
    unsigned char used[128] = { 0 };
    memset(out, 0, (size_t)t->n);
    /* 1. The program's own letters win; a repeated or unusable one is ignored. */
    for (int i = 0; given && i < t->n; i++) {
        unsigned char c = (unsigned char)given[i];
        if (in_menu(&t->entries[i], view) && c < 128 && assignable(c) && !used[ascii_lower(c)]) {
            used[ascii_lower(c)] = 1;
            out[i] = (char)c;
        }
    }
    /* 2. The common verbs take their table letter, so it is the same everywhere. */
    for (int i = 0; i < t->n; i++) {
        if (out[i] || !in_menu(&t->entries[i], view)) continue;
        for (size_t v = 0; v < sizeof g_verbs / sizeof g_verbs[0]; v++) {
            unsigned char c = (unsigned char)g_verbs[v].letter;
            if (first_word_is(t->entries[i].action, g_verbs[v].verb) && !used[c]) {
                used[c] = 1;
                out[i] = (char)c;
                break;
            }
        }
    }
    /* 3. The rest: the first letters of the words, then the other letters of
     *    the name, then a digit. */
    int missing = 0;
    for (int i = 0; i < t->n; i++) {
        if (out[i] || !in_menu(&t->entries[i], view)) continue;
        const char *a = t->entries[i].action;
        int c = 0;
        for (const char *p = a; *p && !c; p++) {
            unsigned char l = (unsigned char)ascii_lower((unsigned char)*p);
            if ((p == a || p[-1] == ' ') && l < 128 && assignable(l) && !used[l]) c = l;
        }
        for (const char *p = a; *p && !c; p++) {
            unsigned char l = (unsigned char)ascii_lower((unsigned char)*p);
            if (l < 128 && assignable(l) && !used[l] && l > '9') c = l;
        }
        for (int d = '1'; d <= '9' && !c; d++)
            if (!used[d]) c = d;
        if (c) {
            used[c] = 1;
            out[i] = (char)c;
        } else {
            missing++;
        }
    }
    return missing;
}

/* The menu's help: the quick keys, then the list keys. */
static const char *const g_menu_help[] = {
    "letter", "run the entry (a serious one asks first)",
    "Enter", "run the entry the list stands on",
    "j, k, arrows", "move",
    "PgUp, PgDn", "page",
    "g, G, Home, End", "first, last",
    "Esc, q, Backspace", "close the menu",
};
#define MENU_HELP_ROWS ((int)(sizeof g_menu_help / sizeof g_menu_help[0] / 2))

int tui_actionmenu_open(tui_actionmenu *m, const tui_keytable *t, const char *view,
                        const char *target) {
    return tui_actionmenu_open_keys(m, t, view, target, NULL);
}

int tui_actionmenu_open_keys(tui_actionmenu *m, const tui_keytable *t, const char *view,
                             const char *target, const char *quick) {
    if (!m || !t) {
        errno = EINVAL;
        return -1;
    }
    memset(m, 0, sizeof *m);
    m->table = t;
    char *letters = malloc((size_t)(t->n ? t->n : 1));
    if (!letters) {
        errno = ENOMEM;
        return -1;
    }
    tui_keytable_assign_quick(t, view, quick, letters);
    int count = 0, kw = 0;
    size_t chars = target ? strlen(target) + 1 : 1;
    for (int i = 0; i < t->n; i++) {
        const tui_keyentry *e = &t->entries[i];
        if (e->cls == TUI_ACT_HARMLESS || !applies(e, view)) continue;
        int w = tui_keycode_name(e->key, NULL, 0);
        if (w > kw) kw = w;
        count++;
    }
    for (int i = 0; i < t->n; i++) {
        const tui_keyentry *e = &t->entries[i];
        if (e->cls == TUI_ACT_HARMLESS || !applies(e, view)) continue;
        chars += 3 + (size_t)kw + 2 + strlen(e->action) + 1 + (e->desc ? 3 + strlen(e->desc) : 0);
    }
    /* One block: the item pointers, the row count, the table indexes, the
     * quick key of each row, the text. */
    size_t pbytes = (size_t)(count ? count : 1) * sizeof(char *);
    size_t ibytes = (size_t)(count + 1) * sizeof(int);
    size_t lbytes = ((size_t)count + sizeof(void *) - 1) / sizeof(void *) * sizeof(void *);
    char *mem = malloc(pbytes + ibytes + lbytes + chars);
    if (!mem) {
        free(letters);
        errno = ENOMEM;
        return -1;
    }
    m->mem = mem;
    const char **items = (const char **)mem;
    int *ints = (int *)(mem + pbytes);
    ints[0] = count;
    m->idx = ints + 1;
    char *qk = mem + pbytes + ibytes;
    char *text = qk + lbytes;
    m->target = text;
    if (target) memcpy(text, target, strlen(target) + 1);
    else text[0] = '\0';
    text += strlen(m->target) + 1;
    int k = 0;
    for (int i = 0; i < t->n; i++) {
        const tui_keyentry *e = &t->entries[i];
        if (e->cls == TUI_ACT_HARMLESS || !applies(e, view)) continue;
        items[k] = text;
        qk[k] = letters[i] ? letters[i] : ' ';
        m->idx[k++] = i;
        char kn[32];
        tui_keycode_name(e->key, kn, sizeof kn);
        text += sprintf(text, "%c  ", letters[i] ? letters[i] : ' ');
        if (kw) text += sprintf(text, "%-*s  ", kw, kn);
        text += sprintf(text, "%s", e->action);
        if (e->desc) text += sprintf(text, " - %s", e->desc);
        text++;
    }
    free(letters);
    if (tui__dialog_quickmenu(&m->dialog, "Actions", items, count, 0) != 0) {
        free(mem);
        m->mem = NULL;
        return -1;
    }
    m->have_dialog = 1;
    return 0;
}

int tui_actionmenu_run(tui_actionmenu *m, const tui_keytable *t, int entry, const char *target) {
    if (!m || !t || entry < 0 || entry >= t->n) {
        errno = EINVAL;
        return -1;
    }
    memset(m, 0, sizeof *m);
    m->table = t;
    m->entry = entry;
    if (!needs_confirm(&t->entries[entry])) {
        m->state = TUI_ACTION_CHOSEN;
        return 0;
    }
    if (start_confirm(m, entry, target) != 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/* Row @p row was chosen (Enter, or its quick key): run it as Enter does. */
static void choose_row(tui_actionmenu *m, int row) {
    int entry = m->idx[row];
    if (!needs_confirm(&m->table->entries[entry])) {
        m->entry = entry;
        m->state = TUI_ACTION_CHOSEN;
    } else if (start_confirm(m, entry, m->target) != 0) {
        m->state = TUI_ACTION_CANCELLED;   /* never perform what was not confirmed */
    }
}

/* The row of the menu whose quick key is @p ev, or -1. */
static int quick_row(const tui_actionmenu *m, const tui_event *ev) {
    if (ev->type != TUI_EV_KEY || ev->key != TUI_KEY_CHAR || ev->mods != 0 || ev->cp >= 128)
        return -1;
    const char *qk = (const char *)(m->idx + m->idx[-1]);
    for (int k = 0; k < m->idx[-1]; k++)
        if (qk[k] != ' ' && (unsigned char)qk[k] == ev->cp) return k;
    return -1;
}

tui_action_state tui_actionmenu_handle(tui_actionmenu *m, const tui_event *ev) {
    if (!m) return TUI_ACTION_CANCELLED;
    if (m->state != TUI_ACTION_OPEN || !m->have_dialog || !ev) return m->state;
    if (!m->confirming) {
        /* The menu's own keys, before the list's: a quick key runs its entry,
         * ? shows them, q and Backspace go back like Esc. */
        int row = quick_row(m, ev);
        if (row >= 0) {
            choose_row(m, row);
            return m->state;
        }
        if (TUI_EV_IS_CHAR(ev, '?')) {
            int selected = m->dialog.list.selected;
            tui_dialog_free(&m->dialog);
            m->have_dialog = 0;
            if (tui_dialog_help(&m->dialog, "Actions: keys", g_menu_help, MENU_HELP_ROWS) != 0) {
                m->state = TUI_ACTION_CANCELLED;
                return m->state;
            }
            m->have_dialog = 1;
            m->confirming = 2;
            m->entry = selected;   /* where the list stood, until the help closes */
            return m->state;
        }
        if (tui_keycode_of(ev) == TUI_KC_BACK || tui_keycode_of(ev) == TUI_KC_BACKSPACE) {
            m->state = TUI_ACTION_CANCELLED;
            return m->state;
        }
    }
    tui_dialog_state st = tui_dialog_handle(&m->dialog, ev);
    if (st == TUI_DIALOG_OPEN) return m->state;
    if (m->confirming == 2) {
        /* The help closed: back to the menu, on the row it stood on. */
        tui_dialog_free(&m->dialog);
        m->have_dialog = 0;
        if (tui__dialog_quickmenu(&m->dialog, "Actions", (const char *const *)m->mem,
                                  m->idx[-1], m->entry) != 0) {
            m->state = TUI_ACTION_CANCELLED;
            return m->state;
        }
        m->have_dialog = 1;
        m->confirming = 0;
        return m->state;
    }
    if (st == TUI_DIALOG_CANCELLED) {
        m->state = TUI_ACTION_CANCELLED;
    } else if (m->confirming) {
        /* Only an answer of Yes performs; Enter on the default No does not. */
        m->state = m->dialog.answer == 1 ? TUI_ACTION_CHOSEN : TUI_ACTION_CANCELLED;
    } else {
        choose_row(m, m->dialog.answer);
    }
    return m->state;
}

void tui_actionmenu_render(tui_actionmenu *m, tui_canvas c) {
    if (m && m->have_dialog) tui_dialog_render(&m->dialog, c);
}

void tui_actionmenu_free(tui_actionmenu *m) {
    if (!m) return;
    if (m->have_dialog) tui_dialog_free(&m->dialog);
    free(m->mem);
    memset(m, 0, sizeof *m);
}

/* ── The help ────────────────────────────────────────────────────────── */

/* The Back entry names every key that goes back (tui_nav_key()). */
#define HELP_BACK_KEYS "Esc, q, Backspace"

int tui_keyhelp_open(tui_keyhelp *h, const tui_keytable *t, const char *view, const char *title) {
    if (!h || !t) {
        errno = EINVAL;
        return -1;
    }
    memset(h, 0, sizeof *h);
    int count = 0;
    size_t chars = 0;
    for (int i = 0; i < t->n; i++) {
        const tui_keyentry *e = &t->entries[i];
        if (!e->action || !applies(e, view)) continue;
        count++;
        chars += (size_t)(e->key == TUI_KC_BACK ? (int)strlen(HELP_BACK_KEYS)
                          : e->key == TUI_KC_NONE ? 1
                                                  : tui_keycode_name(e->key, NULL, 0)) + 1 + strlen(e->action) + 1 +
                 (e->desc ? 3 + strlen(e->desc) : 0);
    }
    size_t pbytes = 2 * (size_t)(count ? count : 1) * sizeof(char *);
    char *mem = malloc(pbytes + chars + 1);
    if (!mem) {
        errno = ENOMEM;
        return -1;
    }
    const char **pairs = (const char **)mem;
    char *text = mem + pbytes;
    int k = 0;
    for (int i = 0; i < t->n; i++) {
        const tui_keyentry *e = &t->entries[i];
        if (!e->action || !applies(e, view)) continue;
        pairs[2 * k] = text;
        /* A menu-only action has no key: ':' (the menu) stands in its column. */
        text += (e->key == TUI_KC_BACK ? snprintf(text, 32, "%s", HELP_BACK_KEYS)
                 : e->key == TUI_KC_NONE ? snprintf(text, 32, ":")
                                         : tui_keycode_name(e->key, text, 32)) + 1;
        pairs[2 * k + 1] = text;
        text += sprintf(text, "%s", e->action);
        if (e->desc) text += sprintf(text, " - %s", e->desc);
        text++;
        k++;
    }
    if (tui_dialog_help(&h->dialog, title, pairs, count) != 0) {
        free(mem);
        return -1;
    }
    for (int i = 0; i < t->n; i++) {
        const tui_keyentry *e = &t->entries[i];
        if (e->action || !e->desc || !applies(e, view)) continue;
        if (tui__dialog_help_text(&h->dialog, e->desc) != 0) {
            tui_dialog_free(&h->dialog);
            free(mem);
            errno = ENOMEM;
            return -1;
        }
        break;
    }
    h->mem = mem;
    return 0;
}

void tui_keyhelp_free(tui_keyhelp *h) {
    if (!h) return;
    if (h->mem) tui_dialog_free(&h->dialog);
    free(h->mem);
    memset(h, 0, sizeof *h);
}
