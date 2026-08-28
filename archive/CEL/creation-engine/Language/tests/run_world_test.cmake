# Runs `celc --run-world <TEST_CEL> --entry ENTRY --ticks TICKS --dt DT`
# against a real, internally-constructed ce::engine::World, checking the
# LAST tick's printed result against the committed .expected fixture --
# the World-touching-intrinsic counterpart to run_exec_test.cmake. Each
# tests/world/*.cel fixture needs its own entry point name/tick
# count/dt (spawn_grid is one shot, orbit_steps threads an entity across
# 300 real calls), so these are driven by individual add_test() calls in
# CMakeLists.txt rather than a uniform glob+loop.
execute_process(
    COMMAND "${CELC_EXE}" --run-world "${TEST_CEL}" --entry "${ENTRY}" --ticks "${TICKS}" --dt "${DT}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    RESULT_VARIABLE exit_code
)
if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "${TEST_CEL} --run-world failed (exit ${exit_code}).\n--- stderr ---\n${error}")
endif()

string(STRIP "${output}" output)
file(READ "${EXPECTED_FILE}" expected_output)
string(STRIP "${expected_output}" expected_output)

if(NOT output STREQUAL expected_output)
    message(FATAL_ERROR "${TEST_CEL}: result '${output}' does not match expected '${expected_output}'.")
endif()
