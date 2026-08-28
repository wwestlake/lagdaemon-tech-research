# Runs `celc --check <TEST_CEL>` on a program that's expected to be
# well-typed and asserts it exits 0 -- the positive-case counterpart to
# run_sema_diag_test.cmake's "must fail with a specific code."
execute_process(
    COMMAND "${CELC_EXE}" --check "${TEST_CEL}"
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
    RESULT_VARIABLE exit_code
)

if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "${TEST_CEL} was expected to type-check cleanly, but celc exited ${exit_code}.\n--- stderr ---\n${actual_error}")
endif()
