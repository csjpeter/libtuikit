/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* The file and directory chooser dialog, with a background directory read. */

#define _DEFAULT_SOURCE

#include "tuikit/filechooser.h"
#include "tuikit/dialog.h"
#include "tuikit/lineedit.h"
#include "tuikit/listview.h"
#include "tuikit/text.h"
#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FC_PATH 4096

typedef struct {
    char  *name;
    int    is_dir;
    off_t  size;
    time_t mtime;
} fc_entry;

/* One background read.  Owned by the chooser and by its thread (refs 2);
 * whoever lets go last frees it, so a chooser can walk away from a read
 * that is stuck on a slow mount. */
typedef struct {
    pthread_mutex_t mu;
    int             refs;
    int             done;
    int             err;
    char           *dir;
    fc_entry       *ent;
    int             n;
    int             wfd;     /* the chooser's wake-up socket, duplicated */
} fc_job;

typedef struct {
    int ent;                 /* index in all[]; -1 for the special rows */
    int special;             /* 0 an entry, 1 ".", 2 ".." */
} fc_row;

typedef struct {
    tui_fc_mode  mode;
    char        *title;
    char        *filter;
    int          show_hidden;
    int          sock[2];
    char         dir[FC_PATH];     /* the directory shown or being read, with a trailing slash */
    char         buf[FC_PATH];     /* the typing field */
    char         path[FC_PATH];    /* the result */
    char         msg[160];         /* a refusal, shown in the status row */
    char         sel_name[256];    /* select this entry when the listing arrives */
    tui_lineedit edit;
    tui_listview list;
    fc_job      *job;
    int          loading;
    int          lerr;             /* errno of the last read, 0 when it worked */
    fc_entry    *all;
    int          nall;
    fc_row      *rows;
    int          nrows;
    tui_dialog   confirm;
    int          confirming;
} fc_priv;

/* dupstr() through the library's own malloc call. */
static char *dupstr(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* ── Paths ───────────────────────────────────────────────────────────── */

/* @p rel (absolute, or relative to the absolute @p base) with '.', '..' and
 * '//' resolved by the text alone.  No trailing slash except for "/". */
static int norm(const char *base, const char *rel, char *out, size_t cap) {
    char tmp[2 * FC_PATH + 2];
    if (rel[0] == '/') {
        if (strlen(rel) >= sizeof tmp) return -1;
        strcpy(tmp, rel);
    } else if ((size_t)snprintf(tmp, sizeof tmp, "%s/%s", base, rel) >= sizeof tmp) {
        return -1;
    }
    size_t len = 0;
    out[0] = '\0';
    for (char *s = tmp; *s;) {
        while (*s == '/') s++;
        char *e = s;
        while (*e && *e != '/') e++;
        size_t n = (size_t)(e - s);
        if (n == 2 && s[0] == '.' && s[1] == '.') {
            while (len > 0 && out[len - 1] != '/') len--;
            if (len > 0) len--;
            out[len] = '\0';
        } else if (n > 0 && !(n == 1 && s[0] == '.')) {
            if (len + 1 + n >= cap) return -1;
            out[len++] = '/';
            memcpy(out + len, s, n);
            len += n;
            out[len] = '\0';
        }
        s = e;
    }
    if (len == 0) strcpy(out, "/");
    return 0;
}

static void add_slash(char *s) {
    size_t n = strlen(s);
    if (n > 0 && s[n - 1] != '/' && n + 1 < FC_PATH) {
        s[n] = '/';
        s[n + 1] = '\0';
    }
}

/* ── The background read ─────────────────────────────────────────────── */

static void job_unref(fc_job *j) {
    pthread_mutex_lock(&j->mu);
    int last = --j->refs == 0;
    pthread_mutex_unlock(&j->mu);
    if (!last) return;
    for (int i = 0; i < j->n; i++) free(j->ent[i].name);
    free(j->ent);
    free(j->dir);
    close(j->wfd);
    pthread_mutex_destroy(&j->mu);
    free(j);
}

static int entry_cmp(const void *a, const void *b) {
    const fc_entry *x = a, *y = b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;      /* directories first */
    int c = strcasecmp(x->name, y->name);
    return c ? c : strcmp(x->name, y->name);
}

static void *job_main(void *arg) {
    fc_job *j = arg;
    fc_entry *ent = NULL;
    int n = 0, cap = 0, err = 0;
    DIR *d = opendir(j->dir);
    if (!d) {
        err = errno;
    } else {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            if (n == cap) {
                int ncap = cap ? cap * 2 : 64;
                fc_entry *g = realloc(ent, (size_t)ncap * sizeof *ent);
                if (!g) {
                    err = ENOMEM;
                    break;
                }
                ent = g;
                cap = ncap;
            }
            struct stat st;
            if (fstatat(dirfd(d), de->d_name, &st, 0) != 0 &&
                fstatat(dirfd(d), de->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0)
                memset(&st, 0, sizeof st);                       /* gone meanwhile */
            char *name = dupstr(de->d_name);
            if (!name) {
                err = ENOMEM;
                break;
            }
            ent[n].name = name;
            ent[n].is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            ent[n].size = st.st_size;
            ent[n].mtime = st.st_mtime;
            n++;
        }
        closedir(d);
        if (n > 1) qsort(ent, (size_t)n, sizeof *ent, entry_cmp);
    }
    pthread_mutex_lock(&j->mu);
    j->ent = ent;
    j->n = n;
    j->err = err;
    j->done = 1;
    pthread_mutex_unlock(&j->mu);
    char b = 'x';
    (void)send(j->wfd, &b, 1, MSG_DONTWAIT | MSG_NOSIGNAL);   /* a full socket already wakes */
    job_unref(j);
    return NULL;
}

/* ── The list ────────────────────────────────────────────────────────── */

static int visible(const fc_priv *p, const fc_entry *e) {
    if (!p->show_hidden && e->name[0] == '.') return 0;
    if (p->mode == TUI_FC_DIR) return e->is_dir;
    if (!e->is_dir && p->filter && fnmatch(p->filter, e->name, 0) != 0) return 0;
    return 1;
}

static const char *row_name(const fc_priv *p, int i) {
    const fc_row *r = &p->rows[i];
    if (r->special == 1) return ".";
    if (r->special == 2) return "..";
    return p->all[r->ent].name;
}

static int row_is_dir(const fc_priv *p, int i) {
    return p->rows[i].special || p->all[p->rows[i].ent].is_dir;
}

/* Rebuilds the rows from the entries; the selection stays on the same name
 * when @p keep, else it goes to the first row. */
static void rebuild(fc_priv *p, int keep) {
    char name[256] = "";
    int was_special = 0;
    if (keep && p->list.selected >= 0 && p->list.selected < p->nrows) {
        snprintf(name, sizeof name, "%s", row_name(p, p->list.selected));
        was_special = p->rows[p->list.selected].special;
    }
    free(p->rows);
    p->rows = NULL;
    p->nrows = 0;
    int cap = p->nall + 2;
    p->rows = malloc((size_t)cap * sizeof *p->rows);
    if (!p->rows) {
        tui_listview_set_count(&p->list, 0);
        return;
    }
    if (p->mode == TUI_FC_DIR) p->rows[p->nrows++] = (fc_row){ -1, 1 };
    if (strcmp(p->dir, "/") != 0) p->rows[p->nrows++] = (fc_row){ -1, 2 };
    for (int i = 0; i < p->nall; i++)
        if (visible(p, &p->all[i])) p->rows[p->nrows++] = (fc_row){ i, 0 };
    tui_listview_set_count(&p->list, p->nrows);
    int at = 0;
    for (int i = 0; keep && i < p->nrows; i++)
        if (!strcmp(row_name(p, i), name) && (p->rows[i].special != 0) == (was_special != 0)) {
            at = i;
            break;
        }
    tui_listview_select(&p->list, at);
}

static void leaf_of(const fc_priv *p, const char **leaf, size_t *dirlen) {
    const char *slash = strrchr(p->buf, '/');
    *leaf = slash ? slash + 1 : p->buf;
    *dirlen = slash ? (size_t)(slash - p->buf) + 1 : 0;
}

static int select_leaf(fc_priv *p) {
    const char *leaf;
    size_t dl;
    leaf_of(p, &leaf, &dl);
    if (!*leaf) {
        tui_listview_select(&p->list, 0);
        return 1;
    }
    for (int i = 0; i < p->nrows; i++)
        if (!p->rows[i].special && !strncmp(row_name(p, i), leaf, strlen(leaf))) {
            tui_listview_select(&p->list, i);
            return 1;
        }
    return 0;
}

static void start_load(tui_filechooser *fc, const char *dir);

/* ── Taking in a finished read ───────────────────────────────────────── */

int tui_filechooser_poll(tui_filechooser *fc) {
    if (!fc || !fc->priv) return 0;
    fc_priv *p = fc->priv;
    char junk[64];
    while (recv(p->sock[0], junk, sizeof junk, MSG_DONTWAIT) > 0) {
    }
    if (!p->job) return 0;
    fc_job *j = p->job;
    pthread_mutex_lock(&j->mu);
    int done = j->done;
    pthread_mutex_unlock(&j->mu);
    if (!done) return 0;
    for (int i = 0; i < p->nall; i++) free(p->all[i].name);
    free(p->all);
    p->all = j->ent;
    p->nall = j->n;
    p->lerr = j->err;
    j->ent = NULL;
    j->n = 0;
    p->job = NULL;
    p->loading = 0;
    job_unref(j);
    rebuild(p, 0);
    if (p->sel_name[0]) {
        for (int i = 0; i < p->nrows; i++)
            if (!p->rows[i].special && !strcmp(row_name(p, i), p->sel_name)) {
                tui_listview_select(&p->list, i);
                break;
            }
        p->sel_name[0] = '\0';
    } else {
        select_leaf(p);
    }
    return 1;
}

static void start_load(tui_filechooser *fc, const char *dir) {
    fc_priv *p = fc->priv;
    if (p->job) {
        job_unref(p->job);                    /* walk away from it */
        p->job = NULL;
    }
    snprintf(p->dir, sizeof p->dir, "%s", dir);
    add_slash(p->dir);
    for (int i = 0; i < p->nall; i++) free(p->all[i].name);
    free(p->all);
    p->all = NULL;
    p->nall = 0;
    p->loading = 0;
    p->lerr = 0;
    rebuild(p, 0);
    fc_job *j = calloc(1, sizeof *j);
    if (!j) {
        p->lerr = ENOMEM;
        return;
    }
    j->dir = dupstr(p->dir);
    j->wfd = fcntl(p->sock[1], F_DUPFD_CLOEXEC, 0);
    if (!j->dir || j->wfd < 0) {
        p->lerr = j->dir ? errno : ENOMEM;
        free(j->dir);
        if (j->wfd >= 0) close(j->wfd);
        free(j);
        return;
    }
    pthread_mutex_init(&j->mu, NULL);
    j->refs = 2;
    pthread_t th;
    int rc = pthread_create(&th, NULL, job_main, j);
    if (rc != 0) {
        p->lerr = rc;
        j->refs = 1;
        job_unref(j);
        return;
    }
    pthread_detach(th);
    p->job = j;
    p->loading = 1;
}

/* ── The typing field and the list follow each other ─────────────────── */

/* The field changed: show the directory it names and the entry it points to. */
static void sync_from_field(tui_filechooser *fc) {
    fc_priv *p = fc->priv;
    const char *leaf;
    size_t dl;
    leaf_of(p, &leaf, &dl);
    char dirpart[FC_PATH], abs[FC_PATH];
    memcpy(dirpart, p->buf, dl);
    dirpart[dl] = '\0';
    if (norm(p->dir, dirpart, abs, sizeof abs) != 0) return;
    add_slash(abs);
    if (strcmp(abs, p->dir) != 0) {
        start_load(fc, abs);
        return;
    }
    select_leaf(p);
}

static int row_path(const fc_priv *p, int i, char *out, size_t cap) {
    const fc_row *r = &p->rows[i];
    if (r->special == 1) return snprintf(out, cap, "%s", p->dir) < (int)cap ? 0 : -1;
    if (r->special == 2) {
        if (norm(p->dir, "..", out, cap) != 0) return -1;
        add_slash(out);
        return 0;
    }
    const fc_entry *e = &p->all[r->ent];
    return snprintf(out, cap, "%s%s%s", p->dir, e->name, e->is_dir ? "/" : "") < (int)cap ? 0 : -1;
}

static void set_field(fc_priv *p, const char *text) {
    tui_lineedit_set_text(&p->edit, text);
}

static void follow_row(fc_priv *p) {
    char path[FC_PATH];
    if (p->list.selected < 0 || p->list.selected >= p->nrows) return;
    if (row_path(p, p->list.selected, path, sizeof path) == 0) set_field(p, path);
}

/* Tab: completes the name being typed from the entries of the shown directory. */
static void complete(tui_lineedit *le) {
    tui_filechooser *fc = le->user;
    fc_priv *p = fc->priv;
    const char *leaf;
    size_t dl;
    leaf_of(p, &leaf, &dl);
    char dirpart[FC_PATH], abs[FC_PATH];
    memcpy(dirpart, p->buf, dl);
    dirpart[dl] = '\0';
    if (p->loading || norm(p->dir, dirpart, abs, sizeof abs) != 0) return;
    add_slash(abs);
    if (strcmp(abs, p->dir) != 0) return;
    size_t ll = strlen(leaf);
    char common[256] = "";
    int matches = 0, last = -1;
    for (int i = 0; i < p->nrows; i++) {
        if (p->rows[i].special || strncmp(row_name(p, i), leaf, ll) != 0) continue;
        const char *nm = row_name(p, i);
        if (matches++ == 0) {
            snprintf(common, sizeof common, "%s", nm);
        } else {
            size_t k = 0;
            while (common[k] && common[k] == nm[k]) k++;
            common[k] = '\0';
        }
        last = i;
    }
    if (matches == 0) return;
    size_t cl = strlen(common);
    while (cl > ll && ((unsigned char)common[cl] & 0xC0) == 0x80) cl--;    /* not mid-character */
    common[cl] = '\0';
    char text[300];
    snprintf(text, sizeof text, "%s%s", common, matches == 1 && row_is_dir(p, last) ? "/" : "");
    if (strlen(text) > ll || (matches == 1 && strlen(text) >= ll))
        tui_lineedit_replace(le, dl, le->len, text);
}

/* ── Choosing ────────────────────────────────────────────────────────── */

static void refuse(fc_priv *p, const char *what, const char *name) {
    snprintf(p->msg, sizeof p->msg, "%s: %s", what, name);
}

static void choose(tui_filechooser *fc, const char *path) {
    fc_priv *p = fc->priv;
    snprintf(p->path, sizeof p->path, "%s", path);
    fc->path = p->path;
    fc->state = TUI_FC_OK;
}

static void activate(tui_filechooser *fc) {
    fc_priv *p = fc->priv;
    if (p->buf[0] == '\0') return;
    char abs[FC_PATH], parent[FC_PATH];
    if (norm(p->dir, p->buf, abs, sizeof abs) != 0) {
        refuse(p, "path too long", "");
        return;
    }
    char here[FC_PATH];
    snprintf(here, sizeof here, "%s", p->dir);
    if (strlen(here) > 1) here[strlen(here) - 1] = '\0';
    if (!strcmp(abs, here)) {                       /* the field names the shown directory */
        if (fc->mode == TUI_FC_DIR && !p->loading) choose(fc, abs);
        return;
    }
    const char *slash = strrchr(abs, '/');
    const char *name = slash + 1;
    size_t pl = (size_t)(slash - abs) + 1;
    memcpy(parent, abs, pl);
    parent[pl] = '\0';
    if (strcmp(parent, p->dir) != 0) {
        /* The '..' row puts the parent directory into the field: an ancestor of the shown one. */
        char into[FC_PATH];
        snprintf(into, sizeof into, "%s", abs);
        add_slash(into);
        if (strncmp(p->dir, into, strlen(into)) == 0) {
            set_field(p, into);
            start_load(fc, into);
        }
        return;
    }
    if (p->loading) return;                            /* the read of its directory is on */
    if (p->lerr) {
        refuse(p, "cannot read", strerror(p->lerr));
        return;
    }
    const fc_entry *e = NULL;
    for (int i = 0; i < p->nall && !e; i++)
        if (!strcmp(p->all[i].name, name)) e = &p->all[i];
    if (e && e->is_dir) {
        char into[FC_PATH];
        snprintf(into, sizeof into, "%s", abs);
        add_slash(into);
        set_field(p, into);
        start_load(fc, into);
        return;
    }
    if (fc->mode == TUI_FC_DIR) {
        refuse(p, e ? "not a directory" : "no such directory", name);
        return;
    }
    if (!e) {
        if (fc->mode == TUI_FC_OPEN) refuse(p, "no such file", name);
        else choose(fc, abs);
        return;
    }
    if (fc->mode == TUI_FC_SAVE) {
        char text[FC_PATH + 40];
        snprintf(text, sizeof text, "%s exists.\nReplace it?", name);
        tui_dialog_free(&p->confirm);
        if (tui_dialog_confirm(&p->confirm, "Replace file", text, 0) == 0) {
            p->confirming = 1;
            snprintf(p->path, sizeof p->path, "%s", abs);
        }
        return;
    }
    choose(fc, abs);
}

/* Backspace with nothing typed after the directory goes up one. */
static int go_up(tui_filechooser *fc) {
    fc_priv *p = fc->priv;
    const char *leaf;
    size_t dl;
    leaf_of(p, &leaf, &dl);
    if (*leaf || dl == 0) return 0;
    char abs[FC_PATH], up[FC_PATH];
    if (norm(p->dir, p->buf, abs, sizeof abs) != 0) return 0;
    if (!strcmp(abs, "/")) return 1;
    const char *base = strrchr(abs, '/') + 1;
    snprintf(p->sel_name, sizeof p->sel_name, "%s", base);
    if (norm(abs, "..", up, sizeof up) != 0) return 1;
    add_slash(up);
    set_field(p, up);
    start_load(fc, up);
    return 1;
}

tui_fc_state tui_filechooser_handle(tui_filechooser *fc, const tui_event *ev) {
    if (!fc || !fc->priv || !ev) return fc ? fc->state : TUI_FC_CANCELLED;
    fc_priv *p = fc->priv;
    if (fc->state != TUI_FC_ACTIVE) return fc->state;
    tui_filechooser_poll(fc);
    if (p->confirming) {
        tui_dialog_state s = tui_dialog_handle(&p->confirm, ev);
        if (s == TUI_DIALOG_OPEN) return fc->state;
        p->confirming = 0;
        if (s == TUI_DIALOG_OK && p->confirm.answer) {
            fc->path = p->path;
            fc->state = TUI_FC_OK;
        }
        return fc->state;
    }
    if (ev->type == TUI_EV_KEY) p->msg[0] = '\0';
    int key = ev->type == TUI_EV_KEY && ev->key != TUI_KEY_CHAR && ev->mods == 0;
    if (ev->type == TUI_EV_KEY &&
        ((ev->key == TUI_KEY_ESC) || TUI_EV_IS_CTRL(ev, 'c'))) {
        fc->state = TUI_FC_CANCELLED;
        return fc->state;
    }
    if (ev->type == TUI_EV_KEY && ev->key == TUI_KEY_ENTER) {
        activate(fc);
        return fc->state;
    }
    if (key && ev->key == TUI_KEY_BACKSPACE && go_up(fc)) return fc->state;
    if (ev->type == TUI_EV_KEY && ev->key == TUI_KEY_CHAR && (ev->mods & TUI_MOD_ALT) &&
        ev->cp == 'h') {
        p->show_hidden = !p->show_hidden;
        rebuild(p, 1);
        return fc->state;
    }
    if (key) {
        switch (ev->key) {
        case TUI_KEY_UP:   tui_listview_move_up(&p->list); follow_row(p); return fc->state;
        case TUI_KEY_DOWN: tui_listview_move_down(&p->list); follow_row(p); return fc->state;
        case TUI_KEY_PGUP: tui_listview_page_up(&p->list); follow_row(p); return fc->state;
        case TUI_KEY_PGDN: tui_listview_page_down(&p->list); follow_row(p); return fc->state;
        case TUI_KEY_HOME: tui_listview_home(&p->list); follow_row(p); return fc->state;
        case TUI_KEY_END:  tui_listview_end(&p->list); follow_row(p); return fc->state;
        default: break;
        }
    }
    char before[FC_PATH];
    snprintf(before, sizeof before, "%s", p->buf);
    tui_lineedit_handle(&p->edit, ev);
    if (strcmp(before, p->buf) != 0) sync_from_field(fc);
    return fc->state;
}

/* ── Opening and closing ─────────────────────────────────────────────── */

int tui_filechooser_open(tui_filechooser *fc, const char *title, tui_fc_mode mode,
                         const char *start, const char *filter) {
    if (!fc || mode < TUI_FC_OPEN || mode > TUI_FC_SAVE) {
        errno = EINVAL;
        return -1;
    }
    memset(fc, 0, sizeof *fc);
    fc->mode = mode;
    fc->state = TUI_FC_ACTIVE;
    fc->notify_fd = -1;
    fc_priv *p = calloc(1, sizeof *p);
    if (!p) {
        errno = ENOMEM;
        return -1;
    }
    fc->priv = p;
    p->mode = mode;
    p->sock[0] = p->sock[1] = -1;
    static const char *const titles[] = { "Open file", "Choose directory", "Save as" };
    p->title = dupstr(title ? title : titles[mode]);
    p->filter = filter ? dupstr(filter) : NULL;
    char cwd[FC_PATH], dir[FC_PATH];
    const char *base = "/";
    if (getcwd(cwd, sizeof cwd)) base = cwd;
    if (!p->title || (filter && !p->filter) || norm(base, start ? start : ".", dir, sizeof dir) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, p->sock) != 0) {
        int e = errno ? errno : ENOMEM;
        tui_filechooser_free(fc);
        errno = e;
        return -1;
    }
    fc->notify_fd = p->sock[0];
    add_slash(dir);
    snprintf(p->dir, sizeof p->dir, "%s", dir);
    tui_lineedit_init(&p->edit, p->buf, sizeof p->buf, dir);
    p->edit.tab_fn = complete;
    p->edit.user = fc;
    tui_listview_init(&p->list);
    start_load(fc, dir);
    return 0;
}

void tui_filechooser_set_hidden(tui_filechooser *fc, int show) {
    if (!fc || !fc->priv) return;
    fc_priv *p = fc->priv;
    p->show_hidden = show ? 1 : 0;
    rebuild(p, 1);
}

int tui_filechooser_loading(const tui_filechooser *fc) {
    return fc && fc->priv ? ((const fc_priv *)fc->priv)->loading : 0;
}

const char *tui_filechooser_dir(const tui_filechooser *fc) {
    return fc && fc->priv ? ((const fc_priv *)fc->priv)->dir : NULL;
}

void tui_filechooser_free(tui_filechooser *fc) {
    if (!fc || !fc->priv) return;
    fc_priv *p = fc->priv;
    if (p->job) job_unref(p->job);
    for (int i = 0; i < p->nall; i++) free(p->all[i].name);
    free(p->all);
    free(p->rows);
    free(p->title);
    free(p->filter);
    if (p->sock[0] >= 0) close(p->sock[0]);
    if (p->sock[1] >= 0) close(p->sock[1]);
    tui_lineedit_free(&p->edit);
    tui_dialog_free(&p->confirm);
    free(p);
    fc->priv = NULL;
    fc->path = NULL;
    fc->notify_fd = -1;
}

/* ── Drawing ─────────────────────────────────────────────────────────── */

static void fmt_size(off_t size, char *out, size_t n) {
    static const char unit[] = "BKMGT";
    double v = (double)size;
    int k = 0;
    if (size < 1024) {
        snprintf(out, n, "%lldB", (long long)size);
        return;
    }
    while (v >= 1024 && k < 4) {
        v /= 1024;
        k++;
    }
    snprintf(out, n, v < 10 ? "%.1f%c" : "%.0f%c", v, unit[k]);
}

static void fmt_date(time_t t, char *out, size_t n) {
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tzset();
    localtime_r(&t, &tm);
    strftime(out, n, "%Y-%m-%d %H:%M", &tm);
}

static void draw_row(void *ctx, tui_canvas row, int index, int selected) {
    fc_priv *p = ctx;
    tui_style st = tui_scheme_style(TUI_ROLE_TEXT);
    if (selected)
        st = tui__selection_compose(tui__selection_base(tui__colors_from_env()), st,
                                    tui_style_attrs(0), 1);
    tui_canvas_clear(row, st);
    int cols = tui_canvas_cols(row);
    if (selected) {
        tui_style mark = st;
        mark.attrs = (uint16_t)(mark.attrs | TUI_ATTR_BOLD);
        tui_canvas_put(row, 0, 0, ">", mark);
    }
    const fc_row *r = &p->rows[index];
    char label[300];
    snprintf(label, sizeof label, "%s%s", row_name(p, index), row_is_dir(p, index) ? "/" : "");
    int meta = cols >= 36;
    int dc = cols - 16, sc = dc - 7;
    int name_w = (meta ? sc - 1 : cols) - 2;
    if (name_w < 1) name_w = 1;
    tui_canvas_put_aligned(tui_canvas_sub(row, tui_rect_make(0, 2, 1, name_w)), 0, label,
                           TUI_ALIGN_LEFT, st);
    if (!meta || r->special) return;
    const fc_entry *e = &p->all[r->ent];
    char size[24], date[24];
    if (!e->is_dir) {
        fmt_size(e->size, size, sizeof size);
        tui_canvas_put_aligned(tui_canvas_sub(row, tui_rect_make(0, sc, 1, 6)), 0, size,
                               TUI_ALIGN_RIGHT, st);
    }
    fmt_date(e->mtime, date, sizeof date);
    tui_canvas_put(row, 0, dc, date, st);
}

void tui_filechooser_render(tui_filechooser *fc, tui_canvas c) {
    if (!fc || !fc->priv || !c.screen) return;
    fc_priv *p = fc->priv;
    tui_filechooser_poll(fc);
    int avail_w = tui_canvas_cols(c) - 2, avail_h = tui_canvas_rows(c) - 2;
    int w = avail_w < 76 ? avail_w : 76, h = avail_h < 20 ? avail_h : 20;
    tui_style fill = tui_scheme_style(TUI_ROLE_TEXT);
    tui_rect box = tui_rect_center(tui_canvas_rect(c), h, w);
    tui_canvas_fill(c, box, fill);
    tui_canvas_box(c, box, p->title, tui_scheme_style(TUI_ROLE_FRAME));
    tui_rect content = tui_rect_make(box.row + 1, box.col + 2, box.rows - 2, box.cols - 4);
    tui_canvas cc = tui_canvas_sub(c, content);
    int rows = tui_canvas_rows(cc), cols = tui_canvas_cols(cc);
    tui_screen_set_cursor(c.screen, 0, 0, 0);

    /* The status row: what is going on, what went wrong, or what is shown. */
    char line[400];
    tui_style sst = tui_scheme_style(TUI_ROLE_HINT);
    if (p->msg[0]) {
        snprintf(line, sizeof line, "%s", p->msg);
        sst = tui_scheme_style(TUI_ROLE_ERROR);
    } else if (p->loading) {
        snprintf(line, sizeof line, "Reading...");
        sst = tui_scheme_style(TUI_ROLE_PROGRESS);
    } else if (p->lerr) {
        snprintf(line, sizeof line, "cannot read: %s", strerror(p->lerr));
        sst = tui_scheme_style(TUI_ROLE_ERROR);
    } else {
        int n = 0;
        for (int i = 0; i < p->nrows; i++) n += p->rows[i].special == 0;
        snprintf(line, sizeof line, "%d %s%s%s%s", n, n == 1 ? "entry" : "entries",
                 p->show_hidden ? ", hidden shown" : "", p->filter ? ", filter " : "",
                 p->filter ? p->filter : "");
    }
    if (rows > 1)
        tui_canvas_put_aligned(tui_canvas_sub(cc, tui_rect_make(1, 0, 1, cols)), 0, line,
                               TUI_ALIGN_LEFT, sst);
    int lrows = rows - 2;
    if (lrows > 0) {
        int bar = tui__scrollbar_needed(p->nrows, lrows);
        tui_canvas wide = tui_canvas_sub(c, tui_rect_make(content.row, content.col, content.rows,
                                                          content.cols + 1));
        tui_canvas list = tui_canvas_sub(bar ? wide : cc,
                                         tui_rect_make(2, 0, lrows, bar ? cols + 1 : cols));
        tui_listview_render(&p->list, list, fill, draw_row, p);
    }
    tui_lineedit_render(&p->edit, tui_canvas_sub(cc, tui_rect_make(0, 0, 1, cols)), NULL,
                        tui__input_style(tui_scheme_style(TUI_ROLE_INPUT), 1));
    if (p->confirming) {
        tui_screen_set_cursor(c.screen, 0, 0, 0);
        tui_dialog_render(&p->confirm, c);
    }
}
