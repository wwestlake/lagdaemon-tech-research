# sim_core: the system simulator's foundation (clock, scheduler, participant
# lifecycle, signal/event bus with units, diagnostics, recorder, replay,
# snapshots, runner). Plain C++20, no JUCE, no Workbench knowledge.
#
# Build configuration (architecture section 14): the Workbench stays a Debug
# app; simulation libraries may be optimised per target. In this directory the
# Debug /Od /Ob0 /RTC1 flags are removed so each target states its own:
#   optimised:   /O2 /Ob2, symbols kept, /fp:precise, no /RTC (incompatible with /O2)
#   unoptimised: /Od /Ob0 /RTC1, /fp:precise
# Both keep the app's runtime library (/MDd) and iterator debug level, which
# the linker requires to match.

option(SIM_CORE_OPTIMIZE "Compile sim_core with optimisation inside Debug builds" ON)

foreach(flags CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG)
    string(REGEX REPLACE "/RTC[1csu]+" "" ${flags} "${${flags}}")
    string(REPLACE "/Od" "" ${flags} "${${flags}}")
    string(REPLACE "/Ob0" "" ${flags} "${${flags}}")
endforeach()

set(SIM_CORE_SOURCES
    src/Units.cpp
    src/Json.cpp
    src/Simulation.cpp
    src/Recorder.cpp
    src/Runner.cpp
)

function(sim_core_flags target optimise)
    target_include_directories(${target} PUBLIC include)
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(MSVC)
        if(optimise)
            target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:/O2 /Ob2> /fp:precise)
            target_compile_definitions(${target} PRIVATE SIM_CORE_BUILD_OPTIMIZED=1)
        else()
            target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:/Od /Ob0 /RTC1> /fp:precise)
            target_compile_definitions(${target} PRIVATE SIM_CORE_BUILD_OPTIMIZED=0)
        endif()
    endif()
endfunction()

add_library(sim_core STATIC ${SIM_CORE_SOURCES})
sim_core_flags(sim_core ${SIM_CORE_OPTIMIZE})

# The same sources, never optimised: the second configuration the P0 suite
# must also pass (architecture section 14, "one test suite, two configurations").
add_library(sim_core_unopt STATIC ${SIM_CORE_SOURCES})
sim_core_flags(sim_core_unopt OFF)
