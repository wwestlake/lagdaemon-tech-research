# Runs `celc --run-simulation <TEST_CEL> --ticks TICKS --dt DT` -- GS6's
# real-production-path counterpart to run_world_test.cmake, exercising
# Simulation::Step + ScriptComponent + CelScriptRuntime rather than
# Runtime::RunWorldProgram directly. celc prints two lines: the entity's
# raw final position (used only by run_simulation_parity_test.cmake, to
# diff against CreationEngineServer's identical output) and a rounded
# checksum -- only the checksum (the LAST line) is checked here against
# the committed .expected fixture.
execute_process(
    COMMAND "${CELC_EXE}" --run-simulation "${TEST_CEL}" --ticks "${TICKS}" --dt "${DT}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    RESULT_VARIABLE exit_code
)
if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "${TEST_CEL} --run-simulation failed (exit ${exit_code}).\n--- stderr ---\n${error}")
endif()

string(STRIP "${output}" output)
string(REPLACE "\n" ";" output_lines "${output}")
list(GET output_lines -1 checksum_line)
string(STRIP "${checksum_line}" checksum_line)

file(READ "${EXPECTED_FILE}" expected_output)
string(STRIP "${expected_output}" expected_output)

if(NOT checksum_line STREQUAL expected_output)
    message(FATAL_ERROR "${TEST_CEL}: checksum '${checksum_line}' does not match expected '${expected_output}'.\nFull output:\n${output}")
endif()
