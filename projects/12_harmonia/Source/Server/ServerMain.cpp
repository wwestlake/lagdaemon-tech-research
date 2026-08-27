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
    
    juce::File cfgFile = juce::File::getCurrentWorkingDirectory().getChildFile("server.cfg");
    if (argc > 1) cfgFile = juce::File(argv[1]);
    
    auto config = ServerApp::loadConfig(cfgFile);
    
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
