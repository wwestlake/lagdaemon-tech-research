cmake_minimum_required(VERSION 3.22)
project(Harmonia VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── JUCE ─────────────────────────────────────────────────────────────────────
set(JUCE_ROOT "D:/JUCE" CACHE PATH "Path to JUCE root")
add_subdirectory(${JUCE_ROOT} ${CMAKE_BINARY_DIR}/juce_build EXCLUDE_FROM_ALL)

# ── GLM (header-only math library) ───────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
FetchContent_MakeAvailable(glm)

# ─────────────────────────────────────────────────────────────────────────────
# HarmoniaShared — static lib compiled into BOTH Harmonia and HarmoniaServer
# Contains: Network protocol, Music theory, Voxel grid (no graphics, no audio)
# ─────────────────────────────────────────────────────────────────────────────
add_library(HarmoniaShared STATIC
    Source/Shared/Network/HarpSerializer.cpp
    Source/Shared/Music/MusicTheory.cpp
    Source/Shared/Music/CircleOfFifths.cpp
    Source/Shared/World/VoxelGrid.cpp
)

target_include_directories(HarmoniaShared PUBLIC Source)

target_link_libraries(HarmoniaShared PUBLIC
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    glm::glm
)

target_compile_definitions(HarmoniaShared PUBLIC
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_STANDALONE_APPLICATION=0
)

# ─────────────────────────────────────────────────────────────────────────────
# HarmoniaServer — lightweight console app, no graphics
# ─────────────────────────────────────────────────────────────────────────────
juce_add_console_app(HarmoniaServer
    PRODUCT_NAME    "Harmonia Server"
    COMPANY_NAME    "LagDaemon"
)



target_sources(HarmoniaServer PRIVATE
    Source/Server/ServerMain.cpp
    Source/Server/ServerApp.cpp
    Source/Server/Logger.cpp
    Source/Server/ListenerThread.cpp
    Source/Server/ClientConnection.cpp
    Source/Server/SessionManager.cpp
    Source/Server/MessageRouter.cpp
    Source/Server/WorldTickThread.cpp
)

target_compile_definitions(HarmoniaServer PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    HARMONIA_SERVER=1
    HARMONIA_PORT=4440
)

target_link_libraries(HarmoniaServer PRIVATE
    HarmoniaShared
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# Copy HarmoniaServer to bin folder
add_custom_command(TARGET HarmoniaServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/../../harmonia_bin"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:HarmoniaServer>" "${CMAKE_SOURCE_DIR}/../../harmonia_bin/HarmoniaServer.exe"
)

target_include_directories(HarmoniaServer PRIVATE Source)

# ─────────────────────────────────────────────────────────────────────────────
# Harmonia — full client: OpenGL + Audio + MIDI + Networking
# ─────────────────────────────────────────────────────────────────────────────
juce_add_gui_app(Harmonia
    PRODUCT_NAME    "Harmonia"
    COMPANY_NAME    "LagDaemon"
    BUNDLE_ID       "com.lagdaemon.harmonia"
)

target_sources(Harmonia PRIVATE
    # App entry
    Source/Client/App/Main.cpp
    Source/Client/App/HarmoniaApp.cpp

    # Networking
    Source/Client/Network/HarpFramer.cpp
    Source/Client/Network/NetworkClient.cpp
    Source/Client/Network/MessageHandler.cpp
    Source/Client/Network/StateSyncer.cpp

    # World
    Source/Client/World/OpenWorld.cpp
    Source/Client/World/PlayerController.cpp
    Source/Client/World/RemotePlayer.cpp
    Source/Client/World/Regions/LivingGridRegion.cpp

    # Rendering engine
    Source/Client/Engine/Rendering/OpenGLContext.cpp
    Source/Client/Engine/Rendering/Camera.cpp
    Source/Client/Engine/Rendering/ShaderLibrary.cpp
    Source/Client/Engine/Rendering/VoxelRenderer.cpp
    Source/Client/Engine/Rendering/ParticleSystem.cpp
    Source/Client/Engine/Rendering/AvatarRenderer.cpp
    Source/Client/Engine/Rendering/StarField.cpp
    Source/Client/Engine/Rendering/PostProcess.cpp

    # Audio engine
    Source/Client/Engine/Audio/AudioEngine.cpp
    Source/Client/Engine/Audio/MidiEngine.cpp
    Source/Client/Engine/Audio/AdditiveVoice.cpp
    Source/Client/Engine/Audio/SpatialAudio.cpp

    # Shell / UI
    Source/Client/Shell/SplashScreen.cpp
    Source/Client/Shell/ServerBrowser.cpp
    Source/Client/Shell/Journal.cpp
    Source/Client/UI/HudOverlay.cpp
    Source/Client/UI/ChatBox.cpp
)

target_compile_definitions(Harmonia PRIVATE
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_OPENGL3=1
    HARMONIA_PORT=4440
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Harmonia,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Harmonia,JUCE_VERSION>"
)

target_link_libraries(Harmonia PRIVATE
    HarmoniaShared
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

# Copy Harmonia to bin folder
add_custom_command(TARGET Harmonia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/../../harmonia_bin"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Harmonia>" "${CMAKE_SOURCE_DIR}/../../harmonia_bin/Harmonia.exe"
)

target_include_directories(Harmonia PRIVATE Source)
