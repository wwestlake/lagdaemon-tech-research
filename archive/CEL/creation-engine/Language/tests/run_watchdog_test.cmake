# Runs `celc --run <TEST_CEL>`, expecting the runaway-script watchdog
# (module_builder.cpp's EmitWatchdogCheck / intrinsic_trampolines.cpp's
# ce_watchdog_tick) to stop a genuinely infinite loop rather than let it
# hang the test suite forever -- a real, deliberately-broken program
# fault-tested the same way tests/diag and tests/sema/fail assert a
# SPECIFIC diagnostic, not just "something went wrong".
execute_process(
    COMMAND "${CELC_EXE}" --run "${TEST_CEL}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    RESULT_VARIABLE exit_code
    TIMEOUT 30
)
if(exit_code EQUAL 0)
    message(FATAL_ERROR "${TEST_CEL}: expected the watchdog to fault this runaway script, but --run exited 0.\nstdout: ${output}")
endif()
if(NOT error MATCHES "CEL9001")
    message(FATAL_ERROR "${TEST_CEL}: expected a CEL9001 watchdog fault message on stderr, got:\n${error}")
endif()
