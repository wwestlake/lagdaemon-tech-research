include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../../../shared/CMake/CreationSuiteBuildSettings.cmake")
creation_suite_init_build_settings()

# Wraps WinFlexBison (win_bison.exe/win_flex.exe) as CMake custom
# commands that regenerate CEL's lexer/parser from grammar/cel.l and
# grammar/cel.y whenever those files change. Not vendored into the repo
# as prebuilt binaries -- like vcpkg/LLVM, this is a machine-level tool
# dependency, discovered via CE_WINFLEXBISON_DIR (or PATH, if bison/flex
# already happen to be on it) with a clear FATAL_ERROR pointing at where
# to get it if neither is found.

if(DEFINED CE_WINFLEXBISON_DIR)
    find_program(CE_BISON_EXE NAMES win_bison bison PATHS "${CE_WINFLEXBISON_DIR}" NO_DEFAULT_PATH)
    find_program(CE_FLEX_EXE NAMES win_flex flex PATHS "${CE_WINFLEXBISON_DIR}" NO_DEFAULT_PATH)
else()
    find_program(CE_BISON_EXE NAMES win_bison bison)
    find_program(CE_FLEX_EXE NAMES win_flex flex)
endif()

if(NOT CE_BISON_EXE OR NOT CE_FLEX_EXE)
    message(FATAL_ERROR
        "CEL grammar tooling not found (bison=${CE_BISON_EXE} flex=${CE_FLEX_EXE}). "
        "Set -DCE_WINFLEXBISON_DIR=<directory containing win_bison.exe/win_flex.exe> "
        "(get WinFlexBison from https://github.com/lexxmark/winflexbison/releases), "
        "or install bison/flex on PATH.")
endif()

message(STATUS "CEL grammar: bison=${CE_BISON_EXE} flex=${CE_FLEX_EXE}")

# FlexLexer.h (the yyFlexLexer base class Language/include/lang/lexer.h
# derives from) ships alongside win_flex.exe in the WinFlexBison
# distribution rather than being a system header -- find it next to
# whichever flex executable was actually resolved above.
get_filename_component(CE_FLEX_TOOL_DIR "${CE_FLEX_EXE}" DIRECTORY)
find_path(CE_FLEXLEXER_INCLUDE_DIR NAMES FlexLexer.h PATHS "${CE_FLEX_TOOL_DIR}" NO_DEFAULT_PATH)
if(NOT CE_FLEXLEXER_INCLUDE_DIR)
    message(FATAL_ERROR "FlexLexer.h not found next to ${CE_FLEX_EXE} (expected alongside win_flex.exe in the WinFlexBison distribution).")
endif()

# Generates parser.{hpp,cpp}, location.hh, and lexer.cpp from
# grammar/cel.y / grammar/cel.l (relative to the calling directory) into
# ${CMAKE_CURRENT_BINARY_DIR}/generated, and sets ${out_var} in the
# caller's scope to the .cpp sources a target needs to compile. The
# generated directory is added as an include path by the caller (it
# needs parser.hpp/location.hh visible to lexer.h and any hand-written
# .cpp that includes them).
function(ce_add_cel_grammar out_var)
    set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${gen_dir}")

    set(parser_cpp "${gen_dir}/parser.cpp")
    set(parser_hpp "${gen_dir}/parser.hpp")
    set(location_hh "${gen_dir}/location.hh")
    set(lexer_cpp "${gen_dir}/lexer.cpp")

    # %expect 0 (in cel.y) plus these two flags together are the actual
    # mechanical proof this grammar is LALR(1): %expect 0 hard-fails on
    # any unexpected shift/reduce conflict count on its own, but reduce/
    # reduce conflicts are only a warning by default -- -Werror=conflicts-rr
    # upgrades that to a build failure too.
    add_custom_command(
        OUTPUT "${parser_cpp}" "${parser_hpp}" "${location_hh}"
        COMMAND "${CE_BISON_EXE}"
                "-Werror=conflicts-sr" "-Werror=conflicts-rr"
                "--defines=${parser_hpp}"
                "--output=${parser_cpp}"
                "${CMAKE_CURRENT_SOURCE_DIR}/grammar/cel.y"
        WORKING_DIRECTORY "${gen_dir}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/grammar/cel.y"
        COMMENT "CEL: generating parser from cel.y (bison)"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${lexer_cpp}"
        COMMAND "${CE_FLEX_EXE}"
                "--outfile=${lexer_cpp}"
                "${CMAKE_CURRENT_SOURCE_DIR}/grammar/cel.l"
        WORKING_DIRECTORY "${gen_dir}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/grammar/cel.l" "${parser_hpp}"
        COMMENT "CEL: generating lexer from cel.l (flex)"
        VERBATIM
    )

    set(${out_var} "${parser_cpp}" "${lexer_cpp}" PARENT_SCOPE)
    set(CE_CEL_GRAMMAR_GEN_DIR "${gen_dir}" PARENT_SCOPE)
endfunction()
