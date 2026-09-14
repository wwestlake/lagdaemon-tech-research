cmake_minimum_required(VERSION 3.22)
project(MusicLivingSystem VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── JUCE ─────────────────────────────────────────────────────────────────────
set(JUCE_ROOT "D:/JUCE" CACHE PATH "Path to JUCE root")
add_subdirectory(${JUCE_ROOT} ${CMAKE_BINARY_DIR}/juce_build EXCLUDE_FROM_ALL)

# ── GLM (header-only, fetched if not present) ────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
FetchContent_MakeAvailable(glm)

# ── Source files ─────────────────────────────────────────────────────────────
set(SOURCES
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/VoxelWorld/VoxelGrid.cpp
    Source/VoxelWorld/CAEngine.cpp
    Source/VoxelWorld/MusicMapper.cpp
    Source/Rendering/OpenGLView.cpp
    Source/Rendering/VoxelRenderer.cpp
    Source/Rendering/ParticleSystem.cpp
    Source/Rendering/SweepPlane.cpp
    Source/Audio/MidiEngine.cpp
    Source/UI/ControlPanel.cpp
)

set(HEADERS
    Source/MainComponent.h
    Source/VoxelWorld/VoxelGrid.h
    Source/VoxelWorld/CAEngine.h
    Source/VoxelWorld/MusicMapper.h
    Source/Rendering/OpenGLView.h
    Source/Rendering/VoxelRenderer.h
    Source/Rendering/ParticleSystem.h
    Source/Rendering/SweepPlane.h
    Source/Audio/MidiEngine.h
    Source/UI/ControlPanel.h
)

# ── JUCE standalone target ────────────────────────────────────────────────────
juce_add_gui_app(MusicLivingSystem
    PRODUCT_NAME    "Music Living System"
    COMPANY_NAME    "LagDaemon"
    BUNDLE_ID       "com.lagdaemon.musiclivingsystem"
)

target_sources(MusicLivingSystem PRIVATE ${SOURCES} ${HEADERS})

target_compile_definitions(MusicLivingSystem PRIVATE
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MusicLivingSystem,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MusicLivingSystem,JUCE_VERSION>"
    JUCE_OPENGL3=1
)

target_link_libraries(MusicLivingSystem PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_opengl
    glm::glm
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_include_directories(MusicLivingSystem PRIVATE Source)
