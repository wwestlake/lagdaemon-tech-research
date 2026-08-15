cmake_minimum_required(VERSION 3.22)
project(juce_docking VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Locate JUCE Framework
# ------------------------------------------------------------------------------
if(NOT TARGET juce::juce_core)
    if(EXISTS "D:/JUCE/CMakeLists.txt")
        add_subdirectory("D:/JUCE" juce_build)
    elseif(EXISTS "$ENV{JUCE_PATH}")
        add_subdirectory("$ENV{JUCE_PATH}" juce_build)
    endif()
endif()

# ------------------------------------------------------------------------------
# Route Build Output to the Top-Level /bin and /lib Folders (Debug & Release)
# ------------------------------------------------------------------------------
get_filename_component(REPO_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/bin/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/bin/Release")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/lib/Debug")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/lib/Release")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/lib/Debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/lib/Release")

# ------------------------------------------------------------------------------
# Add juce_docking Static Library Target
# ------------------------------------------------------------------------------
add_library(juce_docking STATIC
    src/DockPanel.cpp
    src/DockTab.cpp
    src/DockTabStrip.cpp
    src/DockZone.cpp
    src/DockOverlay.cpp
    src/DockSplitter.cpp
    src/FloatingDockWindow.cpp
    src/DockContainer.cpp
    src/DockManager.cpp
)

target_include_directories(juce_docking PUBLIC
    include
)

target_link_libraries(juce_docking PUBLIC
    juce::juce_core
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)
