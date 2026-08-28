# CEL's frontend/JIT/nodegen (ce_lang_frontend, ce_lang_jit, ce_lang_nodegen
# aliases) now come from shared/CEL (add_subdirectory'd by the root
# CMakeLists.txt before this file) -- Engine no longer owns a local copy
# of the grammar, AST, sema, or JIT runtime. What's left here is
# Engine-specific: the full (Core+World) intrinsics.def override, the
# World-domain trampolines/runtime (ce_engine_lang_world), and celc.
#
# BEFORE PRIVATE: makes THIS directory's include/ win over shared/CEL's
# own include/ when compiling shared/CEL's own sources (sema.cpp,
# module_builder.cpp, intrinsic_trampolines.cpp) as part of Engine's
# build -- so those files see Engine's full intrinsics.def (Core+World)
# via the exact same quoted #include "lang/intrinsics.def" shared/CEL
# itself uses, with zero code changes to shared/CEL required.
target_include_directories(creation_suite_cel_frontend BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(creation_suite_cel_jit BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

find_package(LLVM CONFIG REQUIRED)
message(STATUS "Language: found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

llvm_map_components_to_libnames(CE_LLVM_LIBS core orcjit native)

# Engine's own extension: World-domain trampolines (world_intrinsics),
# the free-function RunWorldProgram (world_runtime), and the real
# production script-runtime CelScriptRuntime (script_runtime) -- all of
# it EngineCore/EnTT-coupled, which is exactly why none of it lives in
# shared/CEL. Needs its own LLVM include/definitions since world_runtime.cpp
# and script_runtime.cpp directly include llvm/*.h, and its own private
# include path into shared/CEL's src/jit/ for module_builder.h/optimizer.h/
# abi_registration.h -- private (non-public) headers of creation_suite_cel_jit
# that this target's .cpp files still need direct access to.
add_library(ce_engine_lang_world STATIC
    include/lang/jit/world_intrinsics.h
    src/jit/world_intrinsics.cpp
    include/lang/jit/world_runtime.h
    src/jit/world_runtime.cpp
    include/lang/jit/script_runtime.h
    src/jit/script_runtime.cpp
)
target_include_directories(ce_engine_lang_world
    PUBLIC include
    PRIVATE ${LLVM_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/CEL/src/jit"
)
target_compile_definitions(ce_engine_lang_world PRIVATE ${LLVM_DEFINITIONS} NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(ce_engine_lang_world PUBLIC ce_lang_frontend ce_lang_jit engine_core ${CE_LLVM_LIBS})
target_compile_features(ce_engine_lang_world PUBLIC cxx_std_20)

# Plain add_executable, deliberately NOT juce_add_console_app: this
# guarantees at least one full build path where LLVM and JUCE headers
# never meet in the same translation unit, which is cheap insurance
# against the header-collision risk vcpkg LLVM + JUCE + windows.h
# carries (min/max macros, _ITERATOR_DEBUG_LEVEL mismatches).
add_executable(celc
    tools/celc/main.cpp
    tools/celc/selftest_graph.h
    tools/celc/selftest_graph.cpp
    tools/celc/nodegen_fixtures.h
    tools/celc/nodegen_fixtures.cpp
)
# node_system: PRIVATE -- celc's own GS8 test code (selftest_graph.cpp)
# and --check-graph use NodeSystem directly; ce_lang_jit already brings
# it in transitively via engine_core (both PUBLIC links), so this isn't
# strictly required for the include path to resolve, but it's the
# correct explicit dependency for what celc's OWN sources use, not just
# what happens to ride along. ce_lang_nodegen (GS9): --graph-to-source/
# --run-graph. ce_engine_lang_world: --run-world/--run-simulation.
target_link_libraries(celc PRIVATE ce_lang_frontend ce_lang_jit ce_engine_lang_world ce_lang_nodegen node_system)

add_test(NAME gs1_llvm_selftest COMMAND celc --selftest-jit)
add_test(NAME gs8_selftest_graph COMMAND celc --selftest-graph)

# GS8: a committed .celg fixture must load and re-save unchanged -- see
# run_celg_stability_test.cmake's own comment.
add_test(
    NAME gs8_celg_stability_example
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/graph/example.celg
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_celg_stability_test.cmake"
)

# GS2: every Language/tests/parse/*.cel round-tripped through
# `celc --dump-ast` and diffed against its committed .expected fixture.
# One CTest per fixture (not one big loop test) so a single regression
# names the exact failing program instead of an opaque "some test
# failed."
file(GLOB CE_CEL_PARSE_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/parse/*.cel")
foreach(test_file ${CE_CEL_PARSE_TESTS})
    get_filename_component(test_name "${test_file}" NAME_WE)
    get_filename_component(test_dir "${test_file}" DIRECTORY)
    add_test(
        NAME "gs2_parse_${test_name}"
        COMMAND ${CMAKE_COMMAND}
                -DCELC_EXE=$<TARGET_FILE:celc>
                -DTEST_CEL=${test_file}
                -DEXPECTED_FILE=${test_dir}/${test_name}.expected
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dump_ast_test.cmake"
    )
endforeach()

# Language/tests/diag/*.cel are deliberately-broken programs paired with
# a *.expect_code sidecar naming the CELxxxx diagnostic they must
# produce -- proves the diagnostic engine reports the *right* problem,
# not just that parsing failed for some reason or other.
file(GLOB CE_CEL_DIAG_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/diag/*.cel")
foreach(test_file ${CE_CEL_DIAG_TESTS})
    get_filename_component(test_name "${test_file}" NAME_WE)
    get_filename_component(test_dir "${test_file}" DIRECTORY)
    add_test(
        NAME "gs2_diag_${test_name}"
        COMMAND ${CMAKE_COMMAND}
                -DCELC_EXE=$<TARGET_FILE:celc>
                -DTEST_CEL=${test_file}
                -DEXPECT_CODE_FILE=${test_dir}/${test_name}.expect_code
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_diag_test.cmake"
    )
endforeach()

# GS3: Language/tests/sema/ok/*.cel must type-check cleanly
# (`celc --check` exits 0); Language/tests/sema/fail/*.cel are paired
# with *.expect_code the same way tests/diag/ is, but exercised through
# --check (semantic errors) rather than --dump-ast (syntax errors).
file(GLOB CE_CEL_SEMA_OK_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/sema/ok/*.cel")
foreach(test_file ${CE_CEL_SEMA_OK_TESTS})
    get_filename_component(test_name "${test_file}" NAME_WE)
    add_test(
        NAME "gs3_sema_ok_${test_name}"
        COMMAND ${CMAKE_COMMAND}
                -DCELC_EXE=$<TARGET_FILE:celc>
                -DTEST_CEL=${test_file}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_check_ok_test.cmake"
    )
endforeach()

file(GLOB CE_CEL_SEMA_FAIL_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/sema/fail/*.cel")
foreach(test_file ${CE_CEL_SEMA_FAIL_TESTS})
    get_filename_component(test_name "${test_file}" NAME_WE)
    get_filename_component(test_dir "${test_file}" DIRECTORY)
    add_test(
        NAME "gs3_sema_fail_${test_name}"
        COMMAND ${CMAKE_COMMAND}
                -DCELC_EXE=$<TARGET_FILE:celc>
                -DTEST_CEL=${test_file}
                -DEXPECT_CODE_FILE=${test_dir}/${test_name}.expect_code
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_sema_diag_test.cmake"
    )
endforeach()

# GS4: Language/tests/exec/*.cel are real, hand-computed programs run
# through `celc --run` at both -O0 and -O2 -- see run_exec_test.cmake
# for why both, not just one.
file(GLOB CE_CEL_EXEC_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/exec/*.cel")
foreach(test_file ${CE_CEL_EXEC_TESTS})
    get_filename_component(test_name "${test_file}" NAME_WE)
    get_filename_component(test_dir "${test_file}" DIRECTORY)
    add_test(
        NAME "gs4_exec_${test_name}"
        COMMAND ${CMAKE_COMMAND}
                -DCELC_EXE=$<TARGET_FILE:celc>
                -DTEST_CEL=${test_file}
                -DEXPECTED_FILE=${test_dir}/${test_name}.expected
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_exec_test.cmake"
    )
endforeach()

# GS5: Language/tests/world/*.cel exercise the real host-ABI intrinsics
# (World/Entity/Transform/Debug) against a genuine, internally-
# constructed ce::engine::World via `celc --run-world` -- unlike
# tests/exec/, each fixture needs its own entry point/tick count/dt (see
# run_world_test.cmake's own comment), so these are explicit add_test()
# calls rather than a glob+loop.
add_test(
    NAME gs5_world_spawn_grid
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/world/spawn_grid.cel
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/world/spawn_grid.expected
            -DENTRY=main
            -DTICKS=1
            -DDT=0.016666667
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_world_test.cmake"
)
add_test(
    NAME gs_world_color_roundtrip
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/world/color_roundtrip.cel
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/world/color_roundtrip.expected
            -DENTRY=main
            -DTICKS=1
            -DDT=0.016666667
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_world_test.cmake"
)
add_test(
    NAME gs5_world_orbit_steps
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/world/orbit_steps.cel
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/world/orbit_steps.expected
            -DENTRY=tick
            -DTICKS=300
            -DDT=0.016666667
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_world_test.cmake"
)

# GS5: a genuinely infinite loop must be stopped by the runaway-script
# watchdog (CEL9001), not hang the test suite -- see
# tests/watchdog/infinite_loop.cel and run_watchdog_test.cmake.
add_test(
    NAME gs5_watchdog_infinite_loop
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/watchdog/infinite_loop.cel
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_watchdog_test.cmake"
)

# GS6: `celc --run-simulation` exercises the REAL production pipeline --
# ce::engine::Simulation::Step + ScriptComponent + CelScriptRuntime --
# unlike GS5's tests/world/ (which drive Runtime::RunWorldProgram
# directly). See run_simulation_test.cmake for the two-line output
# format; the cross-tool (celc vs CreationEngineServer) parity test
# lives in the root CMakeLists.txt, since it needs both executables.
add_test(
    NAME gs6_simulation_orbit
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/simulation/orbit.cel
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/simulation/orbit.expected
            -DTICKS=300
            -DDT=0.016666667
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_simulation_test.cmake"
)

# GS7: the hard "<1s compile time" requirement, measured (not assumed) --
# see run_compile_perf_test.cmake.
add_test(
    NAME gs7_compile_perf_orbit
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/simulation/orbit.cel
            -DMAX_MS=1000
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_compile_perf_test.cmake"
)

# GS-Interop: cross-app intrinsic capability gating -- the same script
# (calls a World-domain intrinsic) must be REJECTED under a narrower
# capability profile and must still SUCCEED under the real default
# (IntrinsicDomainSet::All()) -- see docs/CROSS_APP_LANGUAGE_DOMAINS.md
# and run_domain_diag_test.cmake/run_domain_ok_test.cmake's own
# comments for why both halves of this pair matter.
add_test(
    NAME gs_interop_domain_blocked
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/domain/blocked_world_call.cel
            -DEXPECT_CODE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/domain/blocked_world_call.expect_code
            -DDOMAINS=core
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_domain_diag_test.cmake"
)
add_test(
    NAME gs_interop_domain_allowed
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/domain/blocked_world_call.cel
            -DDOMAINS=core,world
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_domain_ok_test.cmake"
)

# GS9: the flagship verification for graph -> CEL source generation --
# Language/tests/nodegen/bounce.celg (built programmatically, see
# Language/tools/celc/nodegen_fixtures.cpp) and bounce.cel (hand-written)
# describe the SAME behavior; run through --run-graph/--run-simulation
# respectively for 300 ticks, their output must be byte-identical. See
# run_nodegen_parity_test.cmake's own comment for why this is the
# single strongest proof in this milestone.
add_test(
    NAME gs9_nodegen_parity_bounce
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/bounce.celg
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/bounce.cel
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/bounce.expected
            -DTICKS=300
            -DDT=0.016666667
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_nodegen_parity_test.cmake"
)

# GS9: a text-level determinism/coverage check for the catalog types
# gs9_nodegen_parity_bounce doesn't reach -- see
# run_graph_to_source_test.cmake's own comment.
add_test(
    NAME gs9_graph_to_source_operator_coverage
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/operator_coverage.celg
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/operator_coverage.expected
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_graph_to_source_test.cmake"
)

# GS9: a graph with no OnStart/OnTick entry node must be REJECTED by
# GenerateSource with a clear error, not silently produce empty/partial
# source -- see run_graph_to_source_error_test.cmake's own comment.
add_test(
    NAME gs9_graph_to_source_no_entry
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/no_entry.celg
            -DEXPECT_SUBSTRING=no OnStart, OnTick, or SubgraphEntry
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_graph_to_source_error_test.cmake"
)

# GS11: subgraph.celg (SubgraphEntry("Helper") called twice via two
# CallSubgraph nodes, built via BuildSubgraphGraph) and subgraph.cel
# (hand-written) describe the identical behavior -- run for real via
# --run-graph/--run-simulation, checked against a real final position
# (2,0,0) that's only correct if Helper genuinely ran exactly twice.
add_test(
    NAME gs11_subgraph_parity
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/subgraph.celg
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/subgraph.cel
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/subgraph.expected
            -DTICKS=1
            -DDT=0.016666667
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_nodegen_parity_test.cmake"
)

# GS11: the flagship "a subgraph called twice emits one func + two call
# sites" claim, checked as generated TEXT (the parity test above checks
# it as real behavior).
add_test(
    NAME gs11_subgraph_source_shape
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/subgraph.celg
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/subgraph_source.expected
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_graph_to_source_test.cmake"
)

# GS11: a CallFunction node naming an undefined function generates
# cleanly but fails sema -- CheckGeneratedSource must map that failure
# back to the specific CallFunction node, not just an opaque source
# line. See run_graph_diagnostics_test.cmake's own comment.
add_test(
    NAME gs11_graph_diagnostics_mapping
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/diagnostic_mapping.celg
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/diagnostic_mapping.expected
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_graph_diagnostics_test.cmake"
)

# GS11: --trace emits a genuine runtime execution trace (one real
# log_int call per node actually reached) -- see
# run_graph_trace_test.cmake's own comment.
add_test(
    NAME gs11_graph_trace
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/trace_demo.celg
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/trace_demo.expected
            -DTICKS=1
            -DDT=0.016666667
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_graph_trace_test.cmake"
)
