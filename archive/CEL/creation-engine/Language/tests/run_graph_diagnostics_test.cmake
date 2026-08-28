# GS11: runs `celc --check-graph-diagnostics <TEST_CELG>` and diffs its
# stdout against EXPECTED_FILE -- proves a sema-only failure (one
# GenerateSource itself can't see, e.g. a CallFunction naming an
# undefined function) gets mapped back to the specific node that
# produced it via ce::lang::nodegen::CheckGeneratedSource's source map,
# not just surfaced as an opaque compiler error. Asserts non-zero exit
# (the fixture is deliberately broken).
execute_process(
    COMMAND "${CELC_EXE}" --check-graph-diagnostics "${TEST_CELG}"
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
    RESULT_VARIABLE exit_code
)

if(exit_code EQUAL 0)
    message(FATAL_ERROR "${TEST_CELG} was expected to report diagnostics, but celc exited 0.\n--- stdout ---\n${actual_output}")
endif()

file(READ "${EXPECTED_FILE}" expected_output)
string(REPLACE "\r\n" "\n" expected_output "${expected_output}")
string(REPLACE "\r\n" "\n" actual_output "${actual_output}")
string(STRIP "${expected_output}" expected_output)
string(STRIP "${actual_output}" actual_output)

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR "diagnostics mismatch for ${TEST_CELG}.\n--- expected ---\n${expected_output}\n--- actual ---\n${actual_output}\n--- stderr ---\n${actual_error}")
endif()
