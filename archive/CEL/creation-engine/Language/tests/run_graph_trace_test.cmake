# GS11: runs `celc --run-graph <TEST_CELG> --ticks TICKS --dt DT --trace`
# and diffs its FULL stdout (trace lines + the usual position/checksum
# pair) against EXPECTED_FILE -- proves --trace emits a genuine runtime
# execution trace (one "trace node <id>" line per node actually
# reached, in the order it was reached), not just a static annotation.
execute_process(
    COMMAND "${CELC_EXE}" --run-graph "${TEST_CELG}" --ticks "${TICKS}" --dt "${DT}" --trace
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
    RESULT_VARIABLE exit_code
)

if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "--run-graph --trace ${TEST_CELG} failed (exit ${exit_code}).\n--- stderr ---\n${actual_error}")
endif()

file(READ "${EXPECTED_FILE}" expected_output)
string(REPLACE "\r\n" "\n" expected_output "${expected_output}")
string(REPLACE "\r\n" "\n" actual_output "${actual_output}")
string(STRIP "${expected_output}" expected_output)
string(STRIP "${actual_output}" actual_output)

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR "trace output mismatch for ${TEST_CELG}.\n--- expected ---\n${expected_output}\n--- actual ---\n${actual_output}")
endif()
