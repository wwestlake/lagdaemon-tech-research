cmake_minimum_required(VERSION 3.22)
project(DjehutiElectronicsLab VERSION 0.1.0 LANGUAGES C CXX)

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

if(NOT TARGET ai_provider)
    add_subdirectory("${FRUSTLANG_ROOT}/projects/04_ai_provider" ai_provider_build)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_CURRENT_SOURCE_DIR}/bin/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin/Release")

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
    Source/ElectronicsKnowledge.h
    Source/ElectronicsKnowledge.cpp
    Source/LocalAgentApi.h
    Source/LocalAgentApi.cpp
)

target_compile_definitions(DjehutiElectronicsLab PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    ELECTRONICS_RESEARCH_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    FRUSTLANG_ROOT="${FRUSTLANG_ROOT}"
)

target_link_libraries(DjehutiElectronicsLab PRIVATE
    CreationDock
    ai_provider
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

juce_generate_juce_header(DjehutiElectronicsLab)
