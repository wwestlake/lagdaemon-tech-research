# Runs `celc --graph-to-source <TEST_CELG>` on a graph expected to be
# REJECTED by GenerateSource, and asserts it exits non-zero with
# EXPECT_SUBSTRING present in stderr -- the codegen-error counterpart to
# run_graph_to_source_test.cmake's success path (a graph with no
# OnStart/OnTick entry node has nothing to generate; see
# graph_to_source.cpp's own comment).
execute_process(
    COMMAND "${CELC_EXE}" --graph-to-source "${TEST_CELG}"
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
    RESULT_VARIABLE exit_code
)

if(exit_code EQUAL 0)
    message(FATAL_ERROR "${TEST_CELG} was expected to fail codegen, but celc exited 0.\n--- stdout ---\n${actual_output}")
endif()

string(FIND "${actual_error}" "${EXPECT_SUBSTRING}" match_pos)
if(match_pos EQUAL -1)
    message(FATAL_ERROR "${TEST_CELG}: expected stderr to contain '${EXPECT_SUBSTRING}', got:\n${actual_error}")
endif()
