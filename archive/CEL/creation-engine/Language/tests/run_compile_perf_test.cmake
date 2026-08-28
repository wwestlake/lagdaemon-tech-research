# GS7's hard compile-time requirement, measured rather than assumed:
# `celc --measure-compile <TEST_CEL>` times the exact same
# ce::engine::IScriptRuntime::Compile call ScriptPanel's background
# CompileJob makes (parse+sema+IR+optimize+JIT end to end) and prints
# milliseconds -- this asserts that's under MAX_MS.
execute_process(
    COMMAND "${CELC_EXE}" --measure-compile "${TEST_CEL}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    RESULT_VARIABLE exit_code
)
if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "${TEST_CEL} --measure-compile failed (exit ${exit_code}).\n--- stderr ---\n${error}")
endif()

string(STRIP "${output}" elapsed_ms)

if(elapsed_ms GREATER_EQUAL ${MAX_MS})
    message(FATAL_ERROR "${TEST_CEL}: compile took ${elapsed_ms}ms, expected under ${MAX_MS}ms.")
endif()

message(STATUS "${TEST_CEL} compiled in ${elapsed_ms}ms (limit ${MAX_MS}ms)")
