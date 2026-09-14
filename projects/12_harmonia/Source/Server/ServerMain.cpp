#include <juce_core/juce_core.h>
#include "Logger.h"
#include "ServerApp.h"
#include <csignal>

using namespace Harmonia; using namespace Harmonia::Server;

ServerApp* g_app = nullptr;

void handleSignal(int sig) {
    if (g_app) {
        g_app->stop();
    }
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    
    // Relative to the server's OWN executable, not the current working
    // directory - a process spawned via juce::ChildProcess inherits
    // whatever CWD the launching client happened to have at that moment
    // (which varies by how/where the client itself was started from),
    // so logs/config were landing in a different, unpredictable place
    // on different launches instead of one reliable location.
    juce::File exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    juce::File cfgFile = exeDir.getChildFile("server.cfg");
    if (argc > 1) cfgFile = juce::File(argv[1]);

    auto config = ServerApp::loadConfig(cfgFile);

    juce::File logDir = exeDir.getChildFile("logs");
    logDir.createDirectory();
    config.logFilePath = logDir.getChildFile("harmonia_server.log").getFullPathName();
    
    Logger::info("=== Harmonia Server v0.1.0 ===");
    Logger::info("Port: " + juce::String(config.port) + " (A440)");
    Logger::info("Session: " + config.defaultSession);
    
    ServerApp app;
    g_app = &app;
    
    if (!app.start(config)) {
        Logger::error("Failed to start server.");
        return 1;
    }
    
    app.run();
    return 0;
}
