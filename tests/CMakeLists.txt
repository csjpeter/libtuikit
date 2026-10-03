# Test suites.  One executable per suite; the kind is the CTest label, so
# "ctest -L unit" and "ctest -L pty" run one kind each.
# See policies/testing-model.md.
#
# libptytest is a dependency of the tests only: the library itself shares
# no code and no logic with it (docs/specification.md, section 2).



set(TUIKIT_TEST_TIMEOUT 180)

# A command every test is run under, e.g. Valgrind (see manage.sh valgrind).
set(TUIKIT_TEST_WRAPPER "" CACHE STRING "Command prefix for every test")

# Fixtures written by the suites go under the build directory, never into
# a fixed /tmp path (policies/project-boundaries-model.md).
set(TUIKIT_TEST_TMPDIR "${CMAKE_CURRENT_BINARY_DIR}/tmp")
file(MAKE_DIRECTORY "${TUIKIT_TEST_TMPDIR}")

find_library(TUIKIT_UTIL_LIB util)

# Every suite is linked with the fault-injection wrappers (common/faults.c):
# inert until a test arms them, and the raise() wrapper writes the coverage
# counters before a signal ends a process.  Test binaries only.
set(TUIKIT_WRAPPED malloc calloc realloc pipe fcntl tcgetattr tcsetattr
                   poll read __read_chk write raise)
set(TUIKIT_WRAP_FLAGS "")
foreach(_fn IN LISTS TUIKIT_WRAPPED)
    list(APPEND TUIKIT_WRAP_FLAGS "LINKER:--wrap=${_fn}")
endforeach()

function(tuikit_add_suite _name _kind _src _lib)
    add_executable(${_name} ${_src} "${CMAKE_CURRENT_SOURCE_DIR}/common/faults.c")
    target_link_options(${_name} PRIVATE ${TUIKIT_WRAP_FLAGS})
    if(ENABLE_COVERAGE)
        target_compile_definitions(${_name} PRIVATE TUIKIT_COVERAGE_BUILD)
    endif()
    target_include_directories(${_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/common")
    target_compile_options(${_name} PRIVATE -Wall -Wextra -Wshadow)
    target_link_libraries(${_name} PRIVATE ${_lib} ptytest::ptytest)
    if(TUIKIT_UTIL_LIB)
        target_link_libraries(${_name} PRIVATE util)   # openpty() on older glibc
    endif()
    target_compile_definitions(${_name} PRIVATE
        TUIKIT_TEST_TMPDIR="${TUIKIT_TEST_TMPDIR}")
    add_test(NAME ${_kind}.${_name} COMMAND ${TUIKIT_TEST_WRAPPER} $<TARGET_FILE:${_name}>)
    set_tests_properties(${_kind}.${_name} PROPERTIES
        LABELS ${_kind}
        TIMEOUT ${TUIKIT_TEST_TIMEOUT})
endfunction()

# --- unit: every module in-process; tui_term and tui_tty over openpty() ----
# Linked statically: a unit test may reach the library's internal headers.
tuikit_add_suite(test-buf     unit unit/test_buf.c     tuikit_static)
target_compile_definitions(test-buf PRIVATE TUIKIT_EXPECTED_VERSION="${TUIKIT_VERSION}")
tuikit_add_suite(test-text    unit unit/test_text.c    tuikit_static)
tuikit_add_suite(test-decoder unit unit/test_decoder.c tuikit_static)
tuikit_add_suite(test-tty     unit unit/test_tty.c     tuikit_static)
tuikit_add_suite(test-term    unit unit/test_term.c    tuikit_static)
tuikit_add_suite(test-screen  unit unit/test_screen.c  tuikit_static)
tuikit_add_suite(test-widgets unit unit/test_widgets.c tuikit_static)
tuikit_add_suite(test-lineedit unit unit/test_lineedit.c tuikit_static)
tuikit_add_suite(test-dialog  unit unit/test_dialog.c  tuikit_static)
tuikit_add_suite(test-fieldset unit unit/test_fieldset.c tuikit_static)
tuikit_add_suite(test-compat  unit unit/test_compat.c  tuikit_static)
tuikit_add_suite(test-faults  unit unit/test_faults.c  tuikit_static)

# --- pty: real programs through a real pseudo-terminal ---------------------
# Linked against the shared library, through the public interface only.
add_executable(term-fixture pty/term_fixture.c)
target_compile_options(term-fixture PRIVATE -Wall -Wextra)
target_link_libraries(term-fixture PRIVATE tuikit::tuikit)

tuikit_add_suite(test-pty pty pty/test_pty.c tuikit_static)
target_compile_definitions(test-pty PRIVATE
    TERM_FIXTURE="$<TARGET_FILE:term-fixture>"
    DEMO="$<TARGET_FILE:tuikit-demo>")
add_dependencies(test-pty term-fixture tuikit-demo)

# --- unit: API conformance -- man page per exported function, clean man
# pages, no reserved names in the headers.
add_test(NAME unit.check-api
         COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/unit/check_api.sh"
                 $<TARGET_FILE:tuikit> "${CMAKE_BINARY_DIR}/man"
                 "${CMAKE_SOURCE_DIR}/include")
set_tests_properties(unit.check-api PROPERTIES LABELS unit TIMEOUT 120)

# --- pty: the scenarios of email-cli's and tg-cli's PTY tests -------------
add_executable(compat-harness pty/compat_harness.c)
target_compile_options(compat-harness PRIVATE -Wall -Wextra)
target_link_libraries(compat-harness PRIVATE tuikit::tuikit)

tuikit_add_suite(test-compat-pty pty pty/test_compat_pty.c tuikit_static)
target_compile_definitions(test-compat-pty PRIVATE
    HARNESS="$<TARGET_FILE:compat-harness>")
add_dependencies(test-compat-pty compat-harness)
