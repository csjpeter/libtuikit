# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-03
# Builds the example program tuikit-demo and its PTY test.

# The example program and its PTY test.  The test is self-contained
# (libptytest only), so "manage.sh consumer" can build both against the
# installed packages as a consuming project would.

add_executable(tuikit-demo tuikit_demo.c)
target_compile_options(tuikit-demo PRIVATE ${TUIKIT_WARNINGS})
target_link_libraries(tuikit-demo PRIVATE tuikit::tuikit)

add_executable(test-tuikit-demo test_tuikit_demo.c)
target_compile_options(test-tuikit-demo PRIVATE -Wall -Wextra)
target_link_libraries(test-tuikit-demo PRIVATE ptytest::ptytest)
add_dependencies(test-tuikit-demo tuikit-demo)

add_test(NAME pty.test-tuikit-demo
         COMMAND ${TUIKIT_TEST_WRAPPER} $<TARGET_FILE:test-tuikit-demo>
                 $<TARGET_FILE:tuikit-demo>)
set_tests_properties(pty.test-tuikit-demo PROPERTIES LABELS pty TIMEOUT 120)
