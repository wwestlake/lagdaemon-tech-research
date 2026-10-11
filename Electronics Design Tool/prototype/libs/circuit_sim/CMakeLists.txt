# circuit_sim: the internal circuit solver (MNA analyses and the resumable
# TransientStepper) as a static library, built from Source/CircuitSolver.cpp
# where it is (decision F2: no file moves, no include changes). The app and
# the test programs that used to compile the file themselves link it.
#
# Build configuration (architecture section 14), as for sim_core: the
# Workbench stays a Debug app; this library may be optimised. In this
# directory the Debug /Od /Ob0 /RTC1 flags are removed so each target states
# its own:
#   optimised:   /O2 /Ob2, symbols kept, /fp:precise, no /RTC
#   unoptimised: /Od /Ob0 /RTC1, /fp:precise (the flags the solver had when
#                each target compiled it)
# CIRCUIT_SIM_OPTIMIZE=OFF builds the linked library unoptimised, reproducing
# the old build.

option(CIRCUIT_SIM_OPTIMIZE "Compile circuit_sim with optimisation inside Debug builds" ON)

foreach(flags CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG)
    string(REGEX REPLACE "/RTC[1csu]+" "" ${flags} "${${flags}}")
    string(REPLACE "/Od" "" ${flags} "${${flags}}")
    string(REPLACE "/Ob0" "" ${flags} "${${flags}}")
endforeach()

set(CIRCUIT_SIM_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../Source")
set(CIRCUIT_SIM_SOURCES
    "${CIRCUIT_SIM_SOURCE_DIR}/CircuitSolver.h"
    "${CIRCUIT_SIM_SOURCE_DIR}/CircuitSolver.cpp"
)

function(circuit_sim_flags target optimise)
    target_include_directories(${target} PUBLIC "${CIRCUIT_SIM_SOURCE_DIR}")
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(MSVC)
        if(optimise)
            target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:/O2 /Ob2> /fp:precise)
        else()
            target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:/Od /Ob0 /RTC1> /fp:precise)
        endif()
    endif()
endfunction()

add_library(circuit_sim STATIC ${CIRCUIT_SIM_SOURCES})
circuit_sim_flags(circuit_sim ${CIRCUIT_SIM_OPTIMIZE})

# The same source, never optimised: the second configuration every P1 test
# must also pass.
add_library(circuit_sim_unopt STATIC ${CIRCUIT_SIM_SOURCES})
circuit_sim_flags(circuit_sim_unopt OFF)
