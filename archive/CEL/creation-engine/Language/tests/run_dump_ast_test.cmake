# Runs `celc --dump-ast <TEST_CEL>` and diffs its stdout against
# EXPECTED_FILE. Invoked once per fixture via CMake -P from
# Language/CMakeLists.txt's per-file add_test() loop (see that file's
# comment for why one test per fixture, not one big loop test).
execute_process(
    COMMAND "${CELC_EXE}" --dump-ast "${TEST_CEL}"
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
    RESULT_VARIABLE exit_code
)

if(NOT EXISTS "${EXPECTED_FILE}")
    message(FATAL_ERROR "No .expected fixture at ${EXPECTED_FILE} for ${TEST_CEL}.\n--- celc stdout ---\n${actual_output}\n--- celc stderr ---\n${actual_error}")
endif()

file(READ "${EXPECTED_FILE}" expected_output)

# Normalize line endings before comparing -- the .expected fixtures are
# checked in as text and may have been checked out with either CRLF or
# LF depending on git's autocrlf setting, but celc's actual stdout uses
# whatever this process's C++ streams choose.
string(REPLACE "\r\n" "\n" expected_output "${expected_output}")
string(REPLACE "\r\n" "\n" actual_output "${actual_output}")

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR "AST mismatch for ${TEST_CEL} (exit=${exit_code}).\n--- expected ---\n${expected_output}\n--- actual ---\n${actual_output}\n--- celc stderr ---\n${actual_error}")
endif()
