#include "ServerApp.h"
#include "Logger.h"

namespace Harmonia { namespace Server {

ServerApp::Config ServerApp::loadConfig(const juce::File& cfgFile) {
    Config cfg;
    return cfg;
}

ServerApp::ServerApp() : running_(false) {
    sessions_ = std::make_unique<SessionManager>();
    router_ = std::make_unique<MessageRouter>(*sessions_);
}

ServerApp::~ServerApp() {
    stop();
}

bool ServerApp::start(const Config& cfg) {
    config_ = cfg;
    if (config_.logFilePath.isNotEmpty()) {
        Logger::setLogFile(config_.logFilePath);
    }
    
    listener_ = std::make_unique<ListenerThread>(config_.port, *sessions_, *router_);
    ticker_ = std::make_unique<WorldTickThread>(*sessions_);
    
    listener_->startThread();
    ticker_->startThread();
    
    running_ = true;
    Logger::info("Server started successfully.");
    return true;
}

void ServerApp::stop() {
    if (listener_) listener_->stop();
    if (ticker_) ticker_->stop();
    running_ = false;
}

void ServerApp::run() {
    while (running_) {
        juce::Thread::sleep(5000);
        printStatus();
    }
}

void ServerApp::printStatus() {
    Logger::info("Status: Players=" + juce::String(sessions_->totalPlayerCount()));
}

}
} // namespace Harmonia
