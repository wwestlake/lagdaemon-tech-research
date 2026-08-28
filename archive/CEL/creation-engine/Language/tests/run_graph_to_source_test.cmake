# Runs `celc --graph-to-source <TEST_CELG>` and diffs its stdout
# against EXPECTED_FILE -- a text-level, deterministic-output check
# (Language/tests/nodegen/operator_coverage.celg exercises catalog
# types the flagship run_nodegen_parity_test.cmake execution path
# doesn't reach -- GetPosition, Sub, Div, the remaining Compare
# variants, CallFunction -- see nodegen_fixtures.h's own comment on why
# this graph is never actually run).
execute_process(
    COMMAND "${CELC_EXE}" --graph-to-source "${TEST_CELG}"
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
    RESULT_VARIABLE exit_code
)

if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "--graph-to-source ${TEST_CELG} failed (exit ${exit_code}).\n--- stderr ---\n${actual_error}")
endif()

if(NOT EXISTS "${EXPECTED_FILE}")
    message(FATAL_ERROR "No .expected fixture at ${EXPECTED_FILE} for ${TEST_CELG}.\n--- celc stdout ---\n${actual_output}")
endif()

file(READ "${EXPECTED_FILE}" expected_output)
string(REPLACE "\r\n" "\n" expected_output "${expected_output}")
string(REPLACE "\r\n" "\n" actual_output "${actual_output}")

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR "generated source mismatch for ${TEST_CELG}.\n--- expected ---\n${expected_output}\n--- actual ---\n${actual_output}")
endif()
