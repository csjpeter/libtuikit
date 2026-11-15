/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* A small directory tree for the file chooser tests. */

#ifndef TUIKIT_FC_TREE_H
#define TUIKIT_FC_TREE_H

/*
 * <root>/
 *   alpha/            inner.txt (7 bytes)      beta/ (empty)
 *   .dots/            .hidden (1 byte)
 *   a.txt   3 bytes     b.eml   2048 bytes     C.TXT   5 MiB
 *   zlink -> alpha
 * Every modification time is 2026-01-02 03:04:05 UTC.
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static inline void fc_tree_file(const char *root, const char *name, long size) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fclose(f);
    if (truncate(path, size) != 0) perror("truncate");
    struct timeval tv[2] = { { 1767323045, 0 }, { 1767323045, 0 } };
    utimes(path, tv);
}

static inline void fc_tree_make(const char *root) {
    char p[512];
    mkdir(root, 0755);
    const char *const dirs[] = { "alpha", "beta", ".dots" };
    for (int i = 0; i < 3; i++) {
        snprintf(p, sizeof p, "%s/%s", root, dirs[i]);
        mkdir(p, 0755);
    }
    fc_tree_file(root, "alpha/inner.txt", 7);
    fc_tree_file(root, ".dots/.hidden", 1);
    fc_tree_file(root, "a.txt", 3);
    fc_tree_file(root, "b.eml", 2048);
    fc_tree_file(root, "C.TXT", 5 * 1024 * 1024);
    snprintf(p, sizeof p, "%s/zlink", root);
    if (symlink("alpha", p) != 0) perror("symlink");
    const char *const dirs2[] = { "alpha", "beta", ".dots" };
    for (int i = 0; i < 3; i++) {
        snprintf(p, sizeof p, "%s/%s", root, dirs2[i]);
        struct timeval tv[2] = { { 1767323045, 0 }, { 1767323045, 0 } };
        utimes(p, tv);
    }
}

#endif /* TUIKIT_FC_TREE_H */
