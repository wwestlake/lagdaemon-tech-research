# GS-Interop: runs `celc --check <TEST_CEL> --domains <DOMAINS>` on a
# program expected to type-check cleanly under that capability profile,
# and asserts it exits 0 -- the "the default (or an explicit
# all-domains profile) is truly unchanged behavior" half of the
# gating regression pair (see run_domain_diag_test.cmake for the other
# half: the SAME script rejected under a narrower profile).
execute_process(
    COMMAND "${CELC_EXE}" --check "${TEST_CEL}" --domains "${DOMAINS}"
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
    RESULT_VARIABLE exit_code
)

if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "${TEST_CEL} --domains ${DOMAINS} was expected to type-check cleanly, but celc exited ${exit_code}.\n--- stderr ---\n${actual_error}")
endif()
