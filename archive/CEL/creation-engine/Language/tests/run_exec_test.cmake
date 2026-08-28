# Runs `celc --run <TEST_CEL>` at both -O0 and -O2, checking (a) both
# match the committed .expected fixture, and (b) both match EACH OTHER
# -- the actual optimizer-correctness check the GS scripting plan calls
# for: if O0 and O2 ever disagree, something in the optimization
# pipeline changed program *behavior*, not just performance, which is a
# much more serious bug than a wrong hand-computed expected value.
execute_process(
    COMMAND "${CELC_EXE}" --run "${TEST_CEL}" --opt 0
    OUTPUT_VARIABLE output_o0
    ERROR_VARIABLE error_o0
    RESULT_VARIABLE exit_o0
)
execute_process(
    COMMAND "${CELC_EXE}" --run "${TEST_CEL}" --opt 2
    OUTPUT_VARIABLE output_o2
    ERROR_VARIABLE error_o2
    RESULT_VARIABLE exit_o2
)

if(NOT exit_o0 EQUAL 0)
    message(FATAL_ERROR "${TEST_CEL} --opt 0 failed (exit ${exit_o0}).\n--- stderr ---\n${error_o0}")
endif()
if(NOT exit_o2 EQUAL 0)
    message(FATAL_ERROR "${TEST_CEL} --opt 2 failed (exit ${exit_o2}).\n--- stderr ---\n${error_o2}")
endif()

string(STRIP "${output_o0}" output_o0)
string(STRIP "${output_o2}" output_o2)

if(NOT output_o0 STREQUAL output_o2)
    message(FATAL_ERROR "${TEST_CEL}: -O0 and -O2 produced DIFFERENT results -- optimizer changed program behavior.\n-O0: ${output_o0}\n-O2: ${output_o2}")
endif()

file(READ "${EXPECTED_FILE}" expected_output)
string(STRIP "${expected_output}" expected_output)

if(NOT output_o0 STREQUAL expected_output)
    message(FATAL_ERROR "${TEST_CEL}: result '${output_o0}' does not match expected '${expected_output}'.")
endif()
