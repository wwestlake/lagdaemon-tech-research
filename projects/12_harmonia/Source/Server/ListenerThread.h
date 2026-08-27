#pragma once

#include <juce_core/juce_core.h>
#include "SessionManager.h"
#include "MessageRouter.h"

namespace Harmonia { namespace Server {

class ListenerThread : public juce::Thread {
public:
    ListenerThread(int port, SessionManager& sessions, MessageRouter& router);
    ~ListenerThread() override;
    void run() override;
    void stop();
    
private:
    void acceptConnection(juce::StreamingSocket* newSocket);
    void performHandshake(ClientConnection& conn, Session& session);
    
    int               port_;
    SessionManager&   sessions_;
    MessageRouter&    router_;
    uint32_t          nextPlayerID_ = 1;
    juce::CriticalSection idLock_;
    
    juce::StreamingSocket listener_;
    std::atomic<bool>     running_;
};

} // namespace
} // namespace Harmonia
