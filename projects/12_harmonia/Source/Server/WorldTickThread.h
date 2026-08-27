#pragma once

#include <juce_core/juce_core.h>
#include "SessionManager.h"
#include "Shared/World/WorldState.h"

namespace Harmonia { namespace Server {

class WorldTickThread : public juce::Thread {
public:
    WorldTickThread(SessionManager& sessions);
    ~WorldTickThread() override;
    void run() override;
    void stop();
    
private:
    void tickSession(Session& session);
    void stepCA(::Harmonia::WorldState& world);
    void broadcastVoxelDeltas(Session& session);
    
    SessionManager& sessions_;
    int             intervalMs_;
    bool            running_;
};

} // namespace
} // namespace Harmonia

