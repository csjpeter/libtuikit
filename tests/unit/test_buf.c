/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2026 Péter Császár */

/* Unit tests of tui_buf: appending, formatting, writing to a descriptor. */

#include "test_helpers.h"
#include "tuikit/api.h"
#include "tuikit/buf.h"

#include <errno.h>
#include <fcntl.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static void test_init_is_empty(void) {
    tui_buf b;
    tui_buf_init(&b);
    ASSERT_EQ_INT(b.len, 0, "length of a new buffer");
    ASSERT(b.data == NULL, "a new buffer allocates nothing");
    ASSERT_EQ_INT(b.failed, 0, "failed flag of a new buffer");
    tui_buf_free(&b);
}

static void test_append_and_puts(void) {
    tui_buf b;
    tui_buf_init(&b);
    ASSERT_EQ_INT(tui_buf_append(&b, "ab", 2), 0, "append");
    ASSERT_EQ_INT(tui_buf_puts(&b, "cd"), 0, "puts");
    ASSERT_EQ_INT(tui_buf_append(&b, "\0x", 2), 0, "append with a NUL byte");
    ASSERT_EQ_INT(b.len, 6, "length");
    ASSERT_BYTES(b.data, 4, "abcd", "first four bytes");
    ASSERT(memcmp(b.data, "abcd\0x", 6) == 0, "embedded NUL kept");
    ASSERT_EQ_INT(b.data[b.len], 0, "terminating NUL");
    tui_buf_free(&b);
}

static void test_growth(void) {
    tui_buf b;
    tui_buf_init(&b);
    for (int i = 0; i < 10000; i++)
        if (tui_buf_append(&b, "0123456789", 10) != 0) break;
    ASSERT_EQ_INT(b.len, 100000, "length after 10000 appends");
    ASSERT_EQ_INT(b.failed, 0, "no failure");
    ASSERT(memcmp(b.data + 99990, "0123456789", 10) == 0, "last chunk intact");
    tui_buf_free(&b);
}

static void test_printf(void) {
    tui_buf b;
    tui_buf_init(&b);
    ASSERT_EQ_INT(tui_buf_printf(&b, "\033[%d;%dH", 12, 40), 0, "printf");
    ASSERT_EQ_INT(tui_buf_printf(&b, "%s", ""), 0, "empty printf");
    ASSERT_BYTES(b.data, b.len, "\033[12;40H", "formatted bytes");
    tui_buf_free(&b);
}

static void test_reset_keeps_memory(void) {
    tui_buf b;
    tui_buf_init(&b);
    tui_buf_puts(&b, "hello");
    char *before = b.data;
    tui_buf_reset(&b);
    ASSERT_EQ_INT(b.len, 0, "length after reset");
    ASSERT(b.data == before, "memory kept");
    ASSERT_EQ_STR(b.data, "", "content after reset");
    tui_buf_free(&b);
}

static void test_invalid_arguments(void) {
    tui_buf b;
    tui_buf_init(&b);
    errno = 0;
    ASSERT_EQ_INT(tui_buf_append(&b, NULL, 3), -1, "append from NULL");
    ASSERT_EQ_INT(errno, EINVAL, "errno of append from NULL");
    ASSERT_EQ_INT(tui_buf_append(NULL, "x", 1), -1, "append to NULL");
    ASSERT_EQ_INT(tui_buf_puts(&b, NULL), -1, "puts NULL");
    ASSERT_EQ_INT(tui_buf_printf(&b, NULL), -1, "printf NULL format");
    ASSERT_EQ_INT(tui_buf_write_fd(NULL, 1), -1, "write NULL buffer");
    ASSERT_EQ_INT(tui_buf_append(&b, NULL, 0), 0, "append zero bytes from NULL");
    tui_buf_free(&b);
    tui_buf_init(NULL);
    tui_buf_free(NULL);
    tui_buf_reset(NULL);
}

static void test_failed_is_sticky_and_not_written(void) {
    int p[2];
    ASSERT_EQ_INT(pipe(p), 0, "pipe");
    tui_buf b;
    tui_buf_init(&b);
    tui_buf_puts(&b, "half a frame");
    b.failed = 1;                 /* as after an allocation failure */
    ASSERT_EQ_INT(tui_buf_puts(&b, "more"), -1, "append after failure");
    errno = 0;
    ASSERT_EQ_INT(tui_buf_write_fd(&b, p[1]), -1, "write of a failed buffer");
    ASSERT_EQ_INT(errno, ENOMEM, "errno of a failed write");
    ASSERT_EQ_INT(b.len, 0, "failed buffer emptied");
    ASSERT_EQ_INT(b.failed, 0, "failed flag cleared");
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    char c;
    ASSERT_EQ_INT(read(p[0], &c, 1), -1, "nothing was written");
    tui_buf_free(&b);
    close(p[0]);
    close(p[1]);
}

static void test_write_fd(void) {
    int p[2];
    ASSERT_EQ_INT(pipe(p), 0, "pipe");
    tui_buf b;
    tui_buf_init(&b);
    tui_buf_puts(&b, "frame bytes");
    ASSERT_EQ_INT(tui_buf_write_fd(&b, p[1]), 0, "write");
    ASSERT_EQ_INT(b.len, 0, "emptied after write");
    char got[32] = {0};
    ssize_t n = read(p[0], got, sizeof(got) - 1);
    ASSERT_EQ_INT(n, 11, "bytes read back");
    ASSERT_EQ_STR(got, "frame bytes", "content read back");
    tui_buf_puts(&b, "x");
    close(p[0]);
    signal(SIGPIPE, SIG_IGN);
    errno = 0;
    ASSERT_EQ_INT(tui_buf_write_fd(&b, p[1]), -1, "write to a closed pipe");
    ASSERT_EQ_INT(errno, EPIPE, "errno of a write to a closed pipe");
    signal(SIGPIPE, SIG_DFL);
    tui_buf_free(&b);
    close(p[1]);
}

/* A pipe that fills up: write_fd must wait (EAGAIN) rather than lose bytes. */
static void test_write_fd_nonblocking_full_pipe(void) {
    int p[2];
    ASSERT_EQ_INT(pipe(p), 0, "pipe");
    fcntl(p[1], F_SETFL, O_NONBLOCK);
    pid_t pid = fork();
    ASSERT(pid >= 0, "fork");
    if (pid == 0) {
        close(p[1]);
        /* Drain slowly so the writer meets a full pipe. */
        usleep(100 * 1000);
        size_t total = 0;
        char buf[4096];
        ssize_t n;
        while ((n = read(p[0], buf, sizeof(buf))) > 0) total += (size_t)n;
        _exit(total == 300000 ? 0 : 1);
    }
    close(p[0]);
    tui_buf b;
    tui_buf_init(&b);
    char line[100];
    memset(line, 'x', sizeof(line));
    for (int i = 0; i < 3000; i++) tui_buf_append(&b, line, sizeof(line));
    ASSERT_EQ_INT(b.len, 300000, "prepared bytes");
    ASSERT_EQ_INT(tui_buf_write_fd(&b, p[1]), 0, "write through a full pipe");
    close(p[1]);
    int st = 0;
    waitpid(pid, &st, 0);
    ASSERT(WIFEXITED(st) && WEXITSTATUS(st) == 0, "reader got every byte");
    tui_buf_free(&b);
}

/* The version string is the project's, as CMake derived it. */
static void test_version(void) {
    const char *v = tui_version();
    ASSERT(v != NULL, "a version string");
    ASSERT_EQ_STR(v, TUIKIT_EXPECTED_VERSION, "tui_version() matches the build's version");
}

static const TestCase tests[] = {
    TEST_CASE(test_version),
    TEST_CASE(test_init_is_empty),
    TEST_CASE(test_append_and_puts),
    TEST_CASE(test_growth),
    TEST_CASE(test_printf),
    TEST_CASE(test_reset_keeps_memory),
    TEST_CASE(test_invalid_arguments),
    TEST_CASE(test_failed_is_sticky_and_not_written),
    TEST_CASE(test_write_fd),
    TEST_CASE(test_write_fd_nonblocking_full_pipe),
};

int main(int argc, char **argv) {
    return RUN_SUITE("buf", tests, 40, 30);
}
