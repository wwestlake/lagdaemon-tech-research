# GS-Interop: runs `celc --check <TEST_CEL> --domains <DOMAINS>` on a
# program that calls an intrinsic outside that capability profile, and
# checks the diagnostic output names the right CELxxxx code -- the
# compile-time half of cross-app intrinsic gating (see
# docs/CROSS_APP_LANGUAGE_DOMAINS.md). Same shape as
# run_sema_diag_test.cmake, with the added --domains flag.
execute_process(
    COMMAND "${CELC_EXE}" --check "${TEST_CEL}" --domains "${DOMAINS}"
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
    RESULT_VARIABLE exit_code
)

if(exit_code EQUAL 0)
    message(FATAL_ERROR "${TEST_CEL} --domains ${DOMAINS} was expected to fail semantic analysis, but celc exited 0.\n--- stdout ---\n${actual_output}")
endif()

file(READ "${EXPECT_CODE_FILE}" expected_code)
string(STRIP "${expected_code}" expected_code)
set(expected_marker "CEL${expected_code}")

string(FIND "${actual_error}" "${expected_marker}" marker_pos)
if(marker_pos EQUAL -1)
    message(FATAL_ERROR "${TEST_CEL} --domains ${DOMAINS}: expected diagnostic ${expected_marker} not found in celc stderr.\n--- stderr ---\n${actual_error}")
endif()
