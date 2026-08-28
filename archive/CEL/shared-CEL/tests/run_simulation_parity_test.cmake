# The concrete "same rule module runs identically client-side or
# server-side" proof: runs the SAME .cel script through celc
# --run-simulation and CreationEngineServer --script/--ticks/--dt, and
# asserts their two-line stdout (raw position, then a rounded checksum
# -- see run_simulation_test.cmake) is byte-for-byte identical. Both
# tools go through ce::engine::Simulation::Step + ScriptComponent + a
# real CelScriptRuntime, so any divergence here means the editor/server
# genuinely don't execute scripts the same way, not just a cosmetic
# difference.
execute_process(
    COMMAND "${CELC_EXE}" --run-simulation "${TEST_CEL}" --ticks "${TICKS}" --dt "${DT}"
    OUTPUT_VARIABLE celc_output
    ERROR_VARIABLE celc_error
    RESULT_VARIABLE celc_exit
)
if(NOT celc_exit EQUAL 0)
    message(FATAL_ERROR "celc --run-simulation failed (exit ${celc_exit}).\n--- stderr ---\n${celc_error}")
endif()

execute_process(
    COMMAND "${SERVER_EXE}" --script "${TEST_CEL}" --ticks "${TICKS}" --dt "${DT}"
    OUTPUT_VARIABLE server_output
    ERROR_VARIABLE server_error
    RESULT_VARIABLE server_exit
)
if(NOT server_exit EQUAL 0)
    message(FATAL_ERROR "CreationEngineServer --script failed (exit ${server_exit}).\n--- stderr ---\n${server_error}")
endif()

string(STRIP "${celc_output}" celc_output)
string(STRIP "${server_output}" server_output)

if(NOT celc_output STREQUAL server_output)
    message(FATAL_ERROR "celc and CreationEngineServer produced DIFFERENT output for the same script -- \"same logic client-side or server-side\" is broken.\n--- celc ---\n${celc_output}\n--- server ---\n${server_output}")
endif()
