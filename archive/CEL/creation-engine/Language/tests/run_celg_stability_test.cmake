# GS8's format-stability regression: `celc --check-graph <TEST_CELG>`
# loads a committed .celg fixture and re-serializes it -- the output
# must match the committed file's own content exactly (module line-ending
# normalization, since git/Windows checkout may convert LF<->CRLF on this
# text file independent of what SerializeGraph itself writes). Any real
# drift here means SerializeGraph/DeserializeGraph stopped round-tripping
# exactly, which would silently corrupt every already-saved .celg file
# once GS10's node editor exists.
execute_process(
    COMMAND "${CELC_EXE}" --check-graph "${TEST_CELG}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    RESULT_VARIABLE exit_code
)
if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "${TEST_CELG} --check-graph failed (exit ${exit_code}).\n--- stderr ---\n${error}")
endif()

file(READ "${TEST_CELG}" expected_content)

string(REPLACE "\r" "" output "${output}")
string(REPLACE "\r" "" expected_content "${expected_content}")
string(STRIP "${output}" output)
string(STRIP "${expected_content}" expected_content)

if(NOT output STREQUAL expected_content)
    message(FATAL_ERROR "${TEST_CELG}: re-serialized output does not match the committed fixture.\n--- expected ---\n${expected_content}\n--- actual ---\n${output}")
endif()
