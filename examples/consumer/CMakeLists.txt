# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-03
# A consuming project that builds the example against the installed libtuikit packages.

# A consuming project, as one of the workspace's programs would be:
# libtuikit through find_package(), libptytest for the PTY test.
cmake_minimum_required(VERSION 3.20)
project(tuikit_consumer LANGUAGES C)
set(CMAKE_C_STANDARD 11)

find_package(tuikit 0.1 REQUIRED)
find_package(ptytest 0.1 REQUIRED)

add_executable(tuikit-demo ../tuikit_demo.c)
target_compile_options(tuikit-demo PRIVATE -Wall -Wextra -Werror)
target_link_libraries(tuikit-demo PRIVATE tuikit::tuikit)

add_executable(test-tuikit-demo ../test_tuikit_demo.c)
target_compile_options(test-tuikit-demo PRIVATE -Wall -Wextra -Werror)
target_link_libraries(test-tuikit-demo PRIVATE ptytest::ptytest)
