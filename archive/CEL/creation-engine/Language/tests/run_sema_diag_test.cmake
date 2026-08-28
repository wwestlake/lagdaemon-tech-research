# Runs `celc --check <TEST_CEL>` on a program that's expected to fail
# semantic analysis, and checks that its diagnostic output names the
# right CELxxxx code -- the sema-phase counterpart to run_diag_test.cmake
# (which does the same thing for --dump-ast/parse-phase failures).
execute_process(
    COMMAND "${CELC_EXE}" --check "${TEST_CEL}"
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
    RESULT_VARIABLE exit_code
)

if(exit_code EQUAL 0)
    message(FATAL_ERROR "${TEST_CEL} was expected to fail semantic analysis, but celc exited 0.\n--- stdout ---\n${actual_output}")
endif()

file(READ "${EXPECT_CODE_FILE}" expected_code)
string(STRIP "${expected_code}" expected_code)
set(expected_marker "CEL${expected_code}")

string(FIND "${actual_error}" "${expected_marker}" marker_pos)
if(marker_pos EQUAL -1)
    message(FATAL_ERROR "${TEST_CEL}: expected diagnostic ${expected_marker} not found in celc stderr.\n--- stderr ---\n${actual_error}")
endif()
