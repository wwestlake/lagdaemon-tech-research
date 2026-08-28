# GS9's flagship verification: runs `celc --run-graph <TEST_CELG>` and
# `celc --run-simulation <TEST_CEL>` for the same tick count/dt and
# asserts their two-line (position + checksum) output is BYTE-IDENTICAL
# -- both a graph and a hand-written .cel describing the same behavior
# go through the identical real production pipeline
# (RunSimulationFromSource, see Language/tools/celc/main.cpp) from the
# point source text exists, so identical output here is direct proof
# the graph and the text really are one language, not two things that
# merely look similar. The checksum (last line of the graph run) is
# additionally checked against a committed EXPECTED_FILE, guarding
# against a regression where both tools somehow drift together.
execute_process(
    COMMAND "${CELC_EXE}" --run-graph "${TEST_CELG}" --ticks "${TICKS}" --dt "${DT}"
    OUTPUT_VARIABLE graph_output
    ERROR_VARIABLE graph_error
    RESULT_VARIABLE graph_exit
)
if(NOT graph_exit EQUAL 0)
    message(FATAL_ERROR "--run-graph ${TEST_CELG} failed (exit ${graph_exit}).\n--- stderr ---\n${graph_error}")
endif()

execute_process(
    COMMAND "${CELC_EXE}" --run-simulation "${TEST_CEL}" --ticks "${TICKS}" --dt "${DT}"
    OUTPUT_VARIABLE cel_output
    ERROR_VARIABLE cel_error
    RESULT_VARIABLE cel_exit
)
if(NOT cel_exit EQUAL 0)
    message(FATAL_ERROR "--run-simulation ${TEST_CEL} failed (exit ${cel_exit}).\n--- stderr ---\n${cel_error}")
endif()

string(STRIP "${graph_output}" graph_output)
string(STRIP "${cel_output}" cel_output)
if(NOT graph_output STREQUAL cel_output)
    message(FATAL_ERROR
        "graph-generated and hand-written source produced DIFFERENT final world state.\n"
        "--- ${TEST_CELG} (via --run-graph) ---\n${graph_output}\n"
        "--- ${TEST_CEL} (via --run-simulation) ---\n${cel_output}\n")
endif()

string(REPLACE "\n" ";" output_lines "${graph_output}")
list(GET output_lines -1 checksum_line)
string(STRIP "${checksum_line}" checksum_line)

file(READ "${EXPECTED_FILE}" expected_output)
string(STRIP "${expected_output}" expected_output)

if(NOT checksum_line STREQUAL expected_output)
    message(FATAL_ERROR "${TEST_CELG}: checksum '${checksum_line}' does not match expected '${expected_output}'.\nFull output:\n${graph_output}")
endif()
