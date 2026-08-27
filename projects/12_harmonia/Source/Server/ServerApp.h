#pragma once

#include <juce_core/juce_core.h>
#include "SessionManager.h"
#include "MessageRouter.h"
#include "ListenerThread.h"
#include "WorldTickThread.h"

namespace Harmonia { namespace Server {

class ServerApp {
public:
    struct Config {
        juce::String serverName = "Harmonia Server";
        int          port       = 4440;
        int          maxPlayers = 16;
        juce::String defaultSession = "main";
        juce::String logFilePath;
    };
    
    static Config loadConfig(const juce::File& cfgFile);
    
    ServerApp();
    ~ServerApp();
    
    bool start(const Config& cfg);
    void stop();
    void run();
    void printStatus();
    
private:
    Config                         config_;
    std::unique_ptr<SessionManager>  sessions_;
    std::unique_ptr<MessageRouter>   router_;
    std::unique_ptr<ListenerThread>  listener_;
    std::unique_ptr<WorldTickThread> ticker_;
    
    std::atomic<bool> running_;
};

} // namespace
} // namespace Harmonia
