/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-22 */
/* Colour schemes: the built-in styles of the roles, the scheme file parser and the search. */

#include "internal.h"
#include "tuikit/scheme.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATH_BYTES       4096
#define SCHEME_MAX_BYTES 65536
#define LINE_MAX_BYTES   256
#define TOKEN_MAX_BYTES  64

static const char *const k_role_names[TUI_ROLE_COUNT] = {
    "text", "header", "status", "selection", "cursor", "accent", "frame", "input",
    "input.focus", "tab.active", "tab.inactive", "ok", "error", "hint", "progress"
};

const char *tui_role_name(tui_role role) {
    if ((int)role < 0 || (int)role >= TUI_ROLE_COUNT) return NULL;
    return k_role_names[role];
}

int tui_role_from_name(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < TUI_ROLE_COUNT; i++)
        if (strcmp(name, k_role_names[i]) == 0) return i;
    return -1;
}

/* ── Built-in styles: what default.scheme holds ─────────────────────── */

tui_style tui_scheme_builtin(tui_role role) {
    switch (role) {
    case TUI_ROLE_HEADER:       return tui_style_make(TUI_WHITE, TUI_BLUE, TUI_ATTR_BOLD);
    case TUI_ROLE_STATUS:       return tui_style_attrs(TUI_ATTR_REVERSE);
    case TUI_ROLE_SELECTION:    return tui_style_make(TUI_COLOR_DEFAULT, TUI_COLOR_RGB(0x00, 0x00, 0x5f), 0);
    case TUI_ROLE_CURSOR:       return tui_style_attrs(TUI_ATTR_REVERSE);
    case TUI_ROLE_ACCENT:       return tui_style_make(TUI_CYAN, TUI_COLOR_DEFAULT, TUI_ATTR_BOLD);
    case TUI_ROLE_FRAME:        return tui_style_make(TUI_CYAN, TUI_COLOR_DEFAULT, TUI_ATTR_BOLD);
    case TUI_ROLE_INPUT_FOCUS:  return tui_style_attrs(TUI_ATTR_UNDERLINE);
    case TUI_ROLE_TAB_ACTIVE:   return tui_style_make(TUI_CYAN, TUI_COLOR_DEFAULT, TUI_ATTR_BOLD);
    case TUI_ROLE_OK:           return tui_style_make(TUI_GREEN, TUI_COLOR_DEFAULT, TUI_ATTR_BOLD);
    case TUI_ROLE_ERROR:        return tui_style_make(TUI_RED, TUI_COLOR_DEFAULT, TUI_ATTR_BOLD);
    case TUI_ROLE_HINT:         return tui_style_make(TUI_YELLOW, TUI_COLOR_DEFAULT, 0);
    case TUI_ROLE_PROGRESS:     return tui_style_make(TUI_YELLOW, TUI_COLOR_DEFAULT, TUI_ATTR_ITALIC);
    default:                    return TUI_STYLE_DEFAULT;  /* text, input, tab.inactive */
    }
}

void tui_scheme_defaults(tui_scheme *sc) {
    for (int i = 0; i < TUI_ROLE_COUNT; i++) sc->styles[i] = tui_scheme_builtin((tui_role)i);
}

/* ── Reporting ──────────────────────────────────────────────────────── */

static void note(tui_scheme_report *rep, const char *origin, int line, const char *fmt, ...) {
    char what[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(what, sizeof what, fmt, ap);
    va_end(ap);
    if (rep->problems++ == 0) {
        if (line > 0) snprintf(rep->first, sizeof rep->first, "%s:%d: %s", origin, line, what);
        else          snprintf(rep->first, sizeof rep->first, "%s: %s", origin, what);
    }
}

/* ── Colours ────────────────────────────────────────────────────────── */

static int hexv(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_color(const char *t, tui_color *out) {
    static const char *const names[8] = { "black", "red", "green", "yellow", "blue", "magenta",
                                          "cyan", "white" };
    if (strcmp(t, "-") == 0 || strcmp(t, "default") == 0) {
        *out = TUI_COLOR_DEFAULT;
        return 0;
    }
    if (t[0] == '#') {
        if (strlen(t) != 7) return -1;
        unsigned v = 0;
        for (int i = 1; i < 7; i++) {
            int h = hexv(t[i]);
            if (h < 0) return -1;
            v = v * 16 + (unsigned)h;
        }
        *out = TUI_COLOR_RGB(v >> 16, (v >> 8) & 0xFFu, v & 0xFFu);
        return 0;
    }
    if (isdigit((unsigned char)t[0])) {
        char *end;
        long n = strtol(t, &end, 10);
        if (*end || strlen(t) > 3 || n > 255) return -1;
        *out = n < 16 ? TUI_COLOR_16(n) : TUI_COLOR_256(n);
        return 0;
    }
    unsigned bright = 0;
    if (strncmp(t, "bright-", 7) == 0) {
        bright = 8;
        t += 7;
    }
    for (unsigned i = 0; i < 8; i++) {
        if (strcmp(t, names[i]) == 0) {
            *out = TUI_COLOR_16(i + bright);
            return 0;
        }
    }
    return -1;
}

static int parse_attrs(const char *t, uint16_t *out) {
    static const struct { const char *name; uint16_t bit; } attrs[] = {
        { "bold", TUI_ATTR_BOLD }, { "dim", TUI_ATTR_DIM }, { "italic", TUI_ATTR_ITALIC },
        { "underline", TUI_ATTR_UNDERLINE }, { "strike", TUI_ATTR_STRIKE },
        { "reverse", TUI_ATTR_REVERSE }, { "blink", TUI_ATTR_BLINK }
    };
    *out = 0;
    if (strcmp(t, "-") == 0) return 0;
    while (*t) {
        size_t n = strcspn(t, ",");
        int found = 0;
        for (size_t i = 0; i < sizeof attrs / sizeof attrs[0]; i++) {
            if (strlen(attrs[i].name) == n && strncmp(t, attrs[i].name, n) == 0) {
                *out = (uint16_t)(*out | attrs[i].bit);
                found = 1;
            }
        }
        if (!found) return -1;
        t += n;
        if (*t == ',') {
            t++;
            if (!*t) return -1;
        }
    }
    return 0;
}

/* The value `fg bg attrs`: a missing part is `-`.  A `#` in the third
 * place onward starts a comment (in the first two it is a colour).  0, or -1
 * with @p why set. */
static int parse_value(const char *v, tui_style *out, const char **why) {
    char tok[3][TOKEN_MAX_BYTES];
    int n = 0;
    for (;;) {
        while (isspace((unsigned char)*v)) v++;
        if (!*v || (n >= 2 && *v == '#')) break;
        size_t len = strcspn(v, " \t\r\n");
        if (n == 3) {
            *why = "too many parts";
            return -1;
        }
        if (len >= TOKEN_MAX_BYTES) {
            *why = "a part is too long";
            return -1;
        }
        memcpy(tok[n], v, len);
        tok[n][len] = 0;
        n++;
        v += len;
    }
    if (n == 0) {
        *why = "empty value";
        return -1;
    }
    for (int i = n; i < 3; i++) strcpy(tok[i], "-");
    tui_style st = TUI_STYLE_DEFAULT;
    if (parse_color(tok[0], &st.fg) != 0) {
        *why = "bad foreground colour";
        return -1;
    }
    if (parse_color(tok[1], &st.bg) != 0) {
        *why = "bad background colour";
        return -1;
    }
    if (parse_attrs(tok[2], &st.attrs) != 0) {
        *why = "bad attribute";
        return -1;
    }
    *out = st;
    return 0;
}

/* ── Lines ──────────────────────────────────────────────────────────── */

/* The next line of *@p pp into @p buf, trimmed.  1 for a line, 0 at the
 * end, -1 for a line too long (skipped). */
static int next_line(const char **pp, char *buf) {
    const char *p = *pp;
    if (!*p) return 0;
    size_t len = strcspn(p, "\n");
    *pp = p[len] ? p + len + 1 : p + len;
    if (len >= LINE_MAX_BYTES) return -1;
    memcpy(buf, p, len);
    buf[len] = 0;
    while (len > 0 && isspace((unsigned char)buf[len - 1])) buf[--len] = 0;
    return 1;
}

/* Splits "key = value" in place.  0 with *key and *val, 1 for a blank or
 * comment line, -1 for a line without '='. */
static int split_kv(char *line, char **key, char **val) {
    while (isspace((unsigned char)*line)) line++;
    if (!*line || *line == '#') return 1;
    char *eq = strchr(line, '=');
    if (!eq) return -1;
    *eq = 0;
    char *kend = eq;
    while (kend > line && isspace((unsigned char)kend[-1])) *--kend = 0;
    *key = line;
    *val = eq + 1;
    return 0;
}

/* Adds the problems of @p text to @p rep. */
static int parse_into(tui_scheme *sc, const char *text, const char *origin, tui_scheme_report *rep) {
    int before = rep->problems;
    char buf[LINE_MAX_BYTES];
    const char *p = text;
    for (int line = 1;; line++) {
        int r = next_line(&p, buf);
        if (r == 0) break;
        if (r < 0) {
            note(rep, origin, line, "line too long");
            continue;
        }
        char *key, *val;
        int k = split_kv(buf, &key, &val);
        if (k > 0) continue;
        if (k < 0) {
            note(rep, origin, line, "no '=' in '%.40s'", buf);
            continue;
        }
        int role = tui_role_from_name(key);
        if (role < 0) {
            note(rep, origin, line, "unknown key '%.40s'", key);
            continue;
        }
        tui_style st;
        const char *why;
        if (parse_value(val, &st, &why) != 0) {
            note(rep, origin, line, "%s: %s", key, why);
            continue;
        }
        sc->styles[role] = st;
    }
    return rep->problems - before;
}

int tui_scheme_parse(tui_scheme *sc, const char *text, const char *origin, tui_scheme_report *rep) {
    tui_scheme_report local;
    if (!rep) rep = &local;
    rep->problems = 0;
    rep->first[0] = 0;
    return parse_into(sc, text, origin, rep);
}

/* ── Files and the search ───────────────────────────────────────────── */

typedef struct {
    const char *user_dir;   /* $XDG_CONFIG_HOME/tuikit, or NULL */
    const char *etc_dir;    /* /etc/tuikit */
    const char *share_dir;  /* /usr/share/libtuikit/schemes */
} scheme_dirs;

static int join(char *out, size_t cap, const char *a, const char *b, const char *c, const char *d) {
    int n = snprintf(out, cap, "%s%s%s%s", a, b, c, d);
    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

/* Reads a file whole into @p buf.  1 read, 0 when it does not exist, -1
 * when it is there and could not be read (a problem is noted). */
static int read_file(const char *path, char *buf, tui_scheme_report *rep) {
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT) return 0;
        note(rep, path, 0, "cannot open: %s", strerror(errno));
        return -1;
    }
    size_t n = fread(buf, 1, SCHEME_MAX_BYTES, f);
    int bad = ferror(f);
    int err = errno;
    int more = !bad && n == SCHEME_MAX_BYTES && fgetc(f) != EOF;
    fclose(f);
    if (bad) {
        note(rep, path, 0, "cannot read: %s", strerror(err));
        return -1;
    }
    if (more) {
        note(rep, path, 0, "file too long");
        return -1;
    }
    buf[n] = 0;
    return 1;
}

static int name_ok(const char *name) {
    size_t n = strlen(name);
    if (n == 0 || n >= 64 || name[0] == '.') return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!isalnum(c) && c != '_' && c != '-' && c != '.') return 0;
    }
    return 1;
}

/* The `scheme = NAME` of one tuikit.conf.  1 when it names one (in
 * @p name), 0 when the file does not exist or has none. */
static int conf_select(const char *dir, char *name, char *buf, tui_scheme_report *rep) {
    char path[PATH_BYTES];
    if (!dir || join(path, sizeof path, dir, "/tuikit.conf", "", "") != 0) return 0;
    if (read_file(path, buf, rep) <= 0) return 0;
    const char *p = buf;
    char line[LINE_MAX_BYTES];
    int found = 0;
    for (int ln = 1;; ln++) {
        int r = next_line(&p, line);
        if (r == 0) break;
        if (r < 0) {
            note(rep, path, ln, "line too long");
            continue;
        }
        char *key, *val;
        int k = split_kv(line, &key, &val);
        if (k > 0) continue;
        if (k < 0) {
            note(rep, path, ln, "no '=' in '%.40s'", line);
            continue;
        }
        if (strcmp(key, "scheme") != 0) continue;
        while (isspace((unsigned char)*val)) val++;
        if (!name_ok(val)) {
            note(rep, path, ln, "bad scheme name '%.40s'", val);
            continue;
        }
        strcpy(name, val);
        found = 1;
    }
    return found;
}

static int load_in(const scheme_dirs *d, const char *name, tui_scheme *sc, tui_scheme_report *rep) {
    char selected[64];
    char path[PATH_BYTES];
    char *buf = malloc(SCHEME_MAX_BYTES + 1);
    tui_scheme_defaults(sc);
    if (!buf) {
        note(rep, "tuikit", 0, "out of memory");
        return rep->problems;
    }
    if (!name) {
        if (conf_select(d->user_dir, selected, buf, rep) || conf_select(d->etc_dir, selected, buf, rep))
            name = selected;
        else
            goto done;
    }
    if (!name_ok(name)) {
        note(rep, "tuikit", 0, "bad scheme name '%.40s'", name);
        goto done;
    }
    for (int i = 0; i < 2; i++) {
        int bad = i == 0 ? (!d->user_dir || join(path, sizeof path, d->user_dir, "/schemes/", name, ".scheme"))
                         : join(path, sizeof path, d->share_dir, "/", name, ".scheme");
        if (bad) continue;
        int r = read_file(path, buf, rep);
        if (r > 0) {
            parse_into(sc, buf, path, rep);
            goto done;
        }
    }
    if (strcmp(name, "default") != 0 && rep->problems == 0)
        note(rep, "tuikit", 0, "scheme '%s' not found", name);
done:
    free(buf);
    return rep->problems;
}

int tui_scheme_load(tui_scheme *sc, const char *name, tui_scheme_report *rep) {
    tui_scheme_report local;
    char user[PATH_BYTES];
    scheme_dirs d = { NULL, "/etc/tuikit", "/usr/share/libtuikit/schemes" };
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (!rep) rep = &local;
    rep->problems = 0;
    rep->first[0] = 0;
    if (xdg && xdg[0] == '/') {
        if (join(user, sizeof user, xdg, "/tuikit", "", "") == 0) d.user_dir = user;
    } else if (home && home[0] == '/') {
        if (join(user, sizeof user, home, "/.config/tuikit", "", "") == 0) d.user_dir = user;
    }
    return load_in(&d, name, sc, rep);
}

/* The same search with the directories given: for the tests. */
int tui__scheme_load_in(const char *user_dir, const char *etc_dir, const char *share_dir,
                        const char *name, tui_scheme *sc, tui_scheme_report *rep) {
    scheme_dirs d = { user_dir, etc_dir, share_dir };
    rep->problems = 0;
    rep->first[0] = 0;
    return load_in(&d, name, sc, rep);
}

/* ── The active scheme ──────────────────────────────────────────────── */

static tui_scheme        g_active;
static tui_scheme_report g_report;
static int               g_loaded;

int tui_scheme_init(void) {
    int n = tui_scheme_load(&g_active, NULL, &g_report);
    g_loaded = 1;
    return n;
}

tui_style tui_scheme_style(tui_role role) {
    if (!g_loaded) tui_scheme_init();
    if ((int)role < 0 || (int)role >= TUI_ROLE_COUNT) return TUI_STYLE_DEFAULT;
    return g_active.styles[role];
}

const char *tui_scheme_diagnostic(void) {
    if (!g_loaded) tui_scheme_init();
    return g_report.problems > 0 ? g_report.first : NULL;
}

/* ── Colour depth ───────────────────────────────────────────────────── */

static void index_rgb(unsigned i, int rgb[3]) {
    static const int base[16][3] = {
        {0, 0, 0}, {205, 0, 0}, {0, 205, 0}, {205, 205, 0}, {0, 0, 238}, {205, 0, 205},
        {0, 205, 205}, {229, 229, 229}, {127, 127, 127}, {255, 0, 0}, {0, 255, 0},
        {255, 255, 0}, {92, 92, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255}
    };
    static const int lv[6] = { 0, 95, 135, 175, 215, 255 };
    if (i < 16) {
        memcpy(rgb, base[i], sizeof base[i]);
    } else if (i < 232) {
        rgb[0] = lv[(i - 16) / 36];
        rgb[1] = lv[((i - 16) / 6) % 6];
        rgb[2] = lv[(i - 16) % 6];
    } else {
        rgb[0] = rgb[1] = rgb[2] = 8 + 10 * (int)(i - 232);
    }
}

tui_color tui_color_reduce(tui_color c, int colors) {
    uint32_t kind = c >> 24;
    uint32_t v = c & 0xFFFFFFu;
    int rgb[3];
    if (kind == 0 || kind > 3) return c;
    if (colors >= 256) {
        if (kind != 3) return c;
    } else if (colors >= 16) {
        if (kind == 1) return c;
    } else if (kind == 1 && v < 8) {
        return c;
    }
    if (kind == 3) {
        rgb[0] = (int)(v >> 16);
        rgb[1] = (int)((v >> 8) & 0xFFu);
        rgb[2] = (int)(v & 0xFFu);
    } else {
        index_rgb(v, rgb);
    }
    if (colors >= 256) {
        /* the nearest of the colour cube and the greys */
        unsigned best = 16;
        long best_d = -1;
        for (unsigned i = 16; i < 256; i++) {
            int p[3];
            index_rgb(i, p);
            long d = 0;
            for (int k = 0; k < 3; k++) d += (long)(p[k] - rgb[k]) * (p[k] - rgb[k]);
            if (best_d < 0 || d < best_d) {
                best_d = d;
                best = i;
            }
        }
        return TUI_COLOR_256(best);
    }
    /* 16 and 8 colours: a channel is on from 64 up, so a dark blue stays
     * blue (its hue, and so its meaning); the bright variant is for 16
     * colours and a channel from 192 up. */
    unsigned idx = (unsigned)(rgb[0] >= 64) | ((unsigned)(rgb[1] >= 64) << 1) | ((unsigned)(rgb[2] >= 64) << 2);
    if (colors >= 16 && (rgb[0] >= 192 || rgb[1] >= 192 || rgb[2] >= 192)) idx += 8;
    return TUI_COLOR_16(idx);
}
