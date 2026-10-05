cmake_minimum_required(VERSION 3.22)
project(DjehutiElectronicsLab VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FRUSTLANG_ROOT "D:/FrustLang" CACHE PATH "Path to the FrustLang research repository for shared research libraries")

if(NOT TARGET juce::juce_core)
    if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("${JUCE_PATH}" juce_build)
    elseif(DEFINED ENV{JUCE_PATH} AND EXISTS "$ENV{JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("$ENV{JUCE_PATH}" juce_build)
    elseif(EXISTS "D:/JUCE/CMakeLists.txt")
        add_subdirectory("D:/JUCE" juce_build)
    else()
        message(FATAL_ERROR "JUCE not found. Set JUCE_PATH to a JUCE checkout.")
    endif()
endif()

if(NOT TARGET CreationDock)
    add_subdirectory("${FRUSTLANG_ROOT}/projects/03_creation_dock" creation_dock_build)
endif()
get_target_property(CREATION_DOCK_INTERFACE_DEFS CreationDock INTERFACE_COMPILE_DEFINITIONS)
if(CREATION_DOCK_INTERFACE_DEFS)
    list(REMOVE_ITEM CREATION_DOCK_INTERFACE_DEFS JUCE_WEB_BROWSER=0)
    set_property(TARGET CreationDock PROPERTY INTERFACE_COMPILE_DEFINITIONS "${CREATION_DOCK_INTERFACE_DEFS}")
endif()

if(NOT TARGET ai_provider)
    add_subdirectory("${FRUSTLANG_ROOT}/projects/04_ai_provider" ai_provider_build)
endif()

# The Frust compiler, embedded: the app writes Frust source, compiles it in
# memory (LLVM JIT through frust_plugin_host) and calls the compiled functions.
# Same machine paths as Frust's own windows-vcpkg preset; LLVM is the existing
# install, never rebuilt and never fetched through vcpkg.
set(FRUST_VCPKG_INSTALLED_DIR "D:/000 Creation Suite/apps/CreationEngine/vcpkg_installed/x64-windows" CACHE PATH
    "Existing vcpkg_installed/<triplet> directory containing LLVM; Frust never runs vcpkg.")
set(FLEX_EXECUTABLE "D:/tools/winflexbison/win_flex.exe" CACHE FILEPATH "Flex for the Frust grammar")
set(BISON_EXECUTABLE "D:/tools/winflexbison/win_bison.exe" CACHE FILEPATH "Bison for the Frust grammar")
set(FRUST_WINFLEXBISON_DIR "D:/tools/winflexbison" CACHE PATH "WinFlexBison directory (FlexLexer.h)")
if(NOT TARGET frate_lib)
    add_subdirectory("${FRUSTLANG_ROOT}/projects/05_frate" frust_frate_build)
endif()
if(NOT TARGET frust_lang)
    add_subdirectory("${FRUSTLANG_ROOT}/projects/01_language_paradigms/02_functional" frust_lang_build)
endif()
if(NOT TARGET frust_plugin_host)
    add_subdirectory("${FRUSTLANG_ROOT}/projects/09_frust_plugin_host" frust_plugin_host_build)
endif()
# DjehutiRoute: the MIT PCB router and board outlines (own repository).
set(DJEHUTI_ROUTE_ROOT "D:/DjehutiRoute" CACHE PATH "DjehutiRoute repository")
if(NOT TARGET djehuti_route)
    add_subdirectory("${DJEHUTI_ROUTE_ROOT}" djehuti_route_build)
endif()

if(MSVC)
    # Frust's targets share an archive/PDB directory; one compiler process at a time.
    foreach(frust_target frate_lib frust_lang frust_runtime frust_plugin_host)
        if(TARGET ${frust_target})
            target_compile_options(${frust_target} PRIVATE /FS /MP1)
        endif()
    endforeach()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_CURRENT_SOURCE_DIR}/bin/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin/Release")

set(LIBAVOID_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libavoid")
add_library(libavoid STATIC
    "${LIBAVOID_ROOT}/actioninfo.cpp"
    "${LIBAVOID_ROOT}/connectionpin.cpp"
    "${LIBAVOID_ROOT}/connector.cpp"
    "${LIBAVOID_ROOT}/connend.cpp"
    "${LIBAVOID_ROOT}/geometry.cpp"
    "${LIBAVOID_ROOT}/geomtypes.cpp"
    "${LIBAVOID_ROOT}/graph.cpp"
    "${LIBAVOID_ROOT}/hyperedge.cpp"
    "${LIBAVOID_ROOT}/hyperedgeimprover.cpp"
    "${LIBAVOID_ROOT}/hyperedgetree.cpp"
    "${LIBAVOID_ROOT}/junction.cpp"
    "${LIBAVOID_ROOT}/makepath.cpp"
    "${LIBAVOID_ROOT}/mtst.cpp"
    "${LIBAVOID_ROOT}/obstacle.cpp"
    "${LIBAVOID_ROOT}/orthogonal.cpp"
    "${LIBAVOID_ROOT}/router.cpp"
    "${LIBAVOID_ROOT}/scanline.cpp"
    "${LIBAVOID_ROOT}/shape.cpp"
    "${LIBAVOID_ROOT}/timer.cpp"
    "${LIBAVOID_ROOT}/vertices.cpp"
    "${LIBAVOID_ROOT}/visibility.cpp"
    "${LIBAVOID_ROOT}/viscluster.cpp"
    "${LIBAVOID_ROOT}/vpsc.cpp"
)
target_include_directories(libavoid PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_compile_definitions(libavoid PUBLIC LIBAVOID_NO_DLL)

juce_add_gui_app(DjehutiElectronicsLab
    PRODUCT_NAME "Djehuti Electronics Lab"
    VERSION 0.1.0
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/../assets/DjehutiElectronicsLabLogo.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/../assets/DjehutiElectronicsLabLogo.png"
)

set_target_properties(DjehutiElectronicsLab PROPERTIES OUTPUT_NAME "djehuti_electronics_lab")

target_sources(DjehutiElectronicsLab PRIVATE
    Source/Main.cpp
    Source/ElectronicsWorkbench.h
    Source/ElectronicsWorkbench.cpp
    Source/ElectronicsWorkbenchProjects.cpp
    Source/ProjectStore.h
    Source/ProjectStore.cpp
    Source/PartCatalog.h
    Source/PartCatalog.cpp
    Source/Preferences.h
    Source/Preferences.cpp
    Source/CircuitSolver.h
    Source/CircuitSolver.cpp
    Source/SignalMeasure.h
    Source/SignalMeasure.cpp
    Source/Analytics.h
    Source/Analytics.cpp
    Source/AnalyticsPanel.h
    Source/AnalyticsPanel.cpp
    Source/SchematicSymbols.h
    Source/SchematicSymbols.cpp
    Source/SchematicLayout.h
    Source/SchematicLayout.cpp
    Source/SchematicRouter.h
    Source/SchematicRouter.cpp
    Source/ElectronicsKnowledge.h
    Source/ElectronicsKnowledge.cpp
    Source/LocalAgentApi.h
    Source/LocalAgentApi.cpp
    Source/FrustEngine.h
    Source/FrustEngine.cpp
    Source/FrustPanel.h
    Source/FrustPanel.cpp
    Source/PcbBoard.h
    Source/PcbBoard.cpp
    Source/PcbPanel.h
    Source/PcbPanel.cpp
)

target_compile_definitions(DjehutiElectronicsLab PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    ELECTRONICS_RESEARCH_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    FRUSTLANG_ROOT="${FRUSTLANG_ROOT}"
)

target_link_libraries(DjehutiElectronicsLab PRIVATE
    CreationDock
    ai_provider
    libavoid
    frust_plugin_host
    djehuti_route
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

juce_generate_juce_header(DjehutiElectronicsLab)

# Headless check that the embedded Frust compiler compiles and runs code.
juce_add_console_app(FrustEngineSmoke PRODUCT_NAME "Frust Engine Smoke")
set_target_properties(FrustEngineSmoke PROPERTIES OUTPUT_NAME "frust_engine_smoke")
target_sources(FrustEngineSmoke PRIVATE tools/frust_smoke/FrustSmoke.cpp Source/FrustEngine.cpp)
target_include_directories(FrustEngineSmoke PRIVATE Source)
target_compile_definitions(FrustEngineSmoke PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(FrustEngineSmoke PRIVATE frust_plugin_host juce::juce_core)
juce_generate_juce_header(FrustEngineSmoke)

# Circuit solver checks against hand-calculated results (plain C++, no JUCE).
add_executable(CircuitSolverTests
    tools/solver_tests/SolverTests.cpp
    Source/CircuitSolver.cpp
    Source/SignalMeasure.cpp
)
set_target_properties(CircuitSolverTests PROPERTIES
    OUTPUT_NAME "circuit_solver_tests"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
add_test(NAME CircuitSolverTests COMMAND CircuitSolverTests)

# Headless layout/routing preview: renders reference circuits to PNG and
# prints readability metrics, without opening the app.
juce_add_console_app(SchematicPreview PRODUCT_NAME "Schematic Preview")
set_target_properties(SchematicPreview PROPERTIES OUTPUT_NAME "schematic_preview")
target_sources(SchematicPreview PRIVATE
    tools/schematic_preview/SchematicPreview.cpp
    Source/SchematicSymbols.cpp
    Source/SchematicLayout.cpp
    Source/SchematicRouter.cpp
)
target_compile_definitions(SchematicPreview PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(SchematicPreview PRIVATE
    libavoid
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
)
juce_generate_juce_header(SchematicPreview)

find_program(POWERSHELL_EXECUTABLE
    NAMES pwsh powershell
)

if(POWERSHELL_EXECUTABLE)
    set(COOKBOOK_VALIDATION_REPORT "${CMAKE_CURRENT_BINARY_DIR}/cookbook_validation_report.json")
    add_custom_target(validate_cookbook
        COMMAND "${POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/knowledge/validate_cookbook.ps1"
                -KnowledgeRoot "${CMAKE_CURRENT_SOURCE_DIR}/knowledge"
                -ReportPath "${COOKBOOK_VALIDATION_REPORT}"
        BYPRODUCTS "${COOKBOOK_VALIDATION_REPORT}"
        COMMENT "Validating electronics cookbook cards and taxonomy coverage"
        VERBATIM
    )

    add_test(NAME CookbookValidation
        COMMAND "${POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/knowledge/validate_cookbook.ps1"
                -KnowledgeRoot "${CMAKE_CURRENT_SOURCE_DIR}/knowledge"
                -ReportPath "${COOKBOOK_VALIDATION_REPORT}"
    )
else()
    message(WARNING "PowerShell was not found; cookbook validation target is unavailable.")
endif()
