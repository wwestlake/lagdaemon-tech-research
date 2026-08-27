#include "WorldTickThread.h"
#include "Logger.h"

namespace Harmonia { namespace Server {

WorldTickThread::WorldTickThread(SessionManager& sessions) 
    : juce::Thread("WorldTick"), sessions_(sessions), intervalMs_(50), running_(false) 
{
}

WorldTickThread::~WorldTickThread() {
    stop();
}

void WorldTickThread::stop() {
    running_ = false;
    stopThread(2000);
}

void WorldTickThread::run() {
    running_ = true;
    while (!threadShouldExit() && running_) {
        int64_t start = juce::Time::currentTimeMillis();

        {
            juce::ScopedLock sl(sessions_.getLock());
            for (auto& pair : sessions_.getSessions()) {
                tickSession(*pair.second);
            }
        }

        int64_t elapsed = juce::Time::currentTimeMillis() - start;
        int64_t sleepTime = intervalMs_ - elapsed;
        if (sleepTime > 0) {
            wait((int)sleepTime);
        }
    }
}

void WorldTickThread::tickSession(Session& session) {
    juce::ScopedLock sl(session.lock);
    stepCA(session.world);
    broadcastVoxelDeltas(session);
}

void WorldTickThread::stepCA(::Harmonia::WorldState& world) {
    // Basic 3D Game of Life placeholder logic here
}

void WorldTickThread::broadcastVoxelDeltas(Session& session) {
    // Send deltas here when CA is updated
}

}
} // namespace Harmonia

