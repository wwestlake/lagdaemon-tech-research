#include "WorldTickThread.h"
#include "Logger.h"
#include "Shared/Network/HarpSerializer.h"

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
    Logger::info("World tick thread started (20 Hz)");

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
        if (sleepTime > 0)
            wait((int)sleepTime);
    }

    Logger::info("World tick thread stopped");
}

void WorldTickThread::tickSession(Session& session) {
    juce::ScopedLock sl(session.lock);
    if (!session.world.livingGrid) return;

    stepCA(session.world);
    session.world.generation++;
    broadcastVoxelDeltas(session);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3D Cellular Automaton — Musical Game of Life variant
//
// Rules: a voxel "fires" (activates) based on its 26 neighbours' states.
//   Live   cell survives  if it has 4–6 active neighbours
//   Dead   cell is born   if it has exactly 5 active neighbours
//   Activation decays by 0.1 per tick so notes fade naturally
// ─────────────────────────────────────────────────────────────────────────────
void WorldTickThread::stepCA(::Harmonia::WorldState& world) {
    auto& grid = *world.livingGrid;
    const int W = grid.width();
    const int H = grid.height();
    const int D = grid.depth();

    // Snapshot current states so we compute from a consistent frame
    std::vector<float> prev(static_cast<size_t>(W * H * D));
    for (int z = 0; z < D; ++z)
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                prev[static_cast<size_t>(x + W * (y + H * z))] = grid.at(x, y, z).state;

    auto idx = [&](int x, int y, int z) -> size_t {
        return static_cast<size_t>(x + W * (y + H * z));
    };

    for (int z = 0; z < D; ++z) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                // Count 26-connected active neighbours
                int activeNeighbours = 0;
                float neighbourSum = 0.f;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0 && dz == 0) continue;
                            int nx = x + dx, ny = y + dy, nz = z + dz;
                            if (!grid.inBounds(nx, ny, nz)) continue;
                            float s = prev[idx(nx, ny, nz)];
                            if (s > 0.1f) {
                                ++activeNeighbours;
                                neighbourSum += s;
                            }
                        }
                    }
                }

                float curState = prev[idx(x, y, z)];
                float newState = 0.f;

                if (curState > 0.1f) {
                    // Alive: survive if 4–6 neighbours
                    if (activeNeighbours >= 4 && activeNeighbours <= 6) {
                        // Sustain with slight decay
                        newState = juce::jmax(0.f, curState - 0.05f);
                    } else {
                        // Die — fast decay
                        newState = juce::jmax(0.f, curState - 0.2f);
                    }
                } else {
                    // Dead: born if exactly 5 neighbours
                    if (activeNeighbours == 5) {
                        // Birth strength is average of neighbours
                        newState = juce::jlimit(0.f, 1.f,
                                                neighbourSum / (float)activeNeighbours);
                    }
                }

                auto& voxel = grid.at(x, y, z);
                bool fired = (newState > 0.5f && curState <= 0.5f);
                voxel.justFired = fired;

                if (std::abs(newState - curState) > 0.001f) {
                    grid.setVoxel(x, y, z, newState);
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Collect dirty voxels and send as VoxelDelta message to all clients
// ─────────────────────────────────────────────────────────────────────────────
void WorldTickThread::broadcastVoxelDeltas(Session& session) {
    if (!session.world.livingGrid) return;

    auto deltas = session.world.livingGrid->collectDelta();
    session.world.livingGrid->clearDirty();

    if (deltas.empty()) return;

    // Cap delta size — never send more than 1024 voxels per tick
    const size_t maxDelta = 1024;
    if (deltas.size() > maxDelta)
        deltas.resize(maxDelta);

    // Serialize: uint16 count, then for each: u8 x, u8 y, u8 z, f32 state
    Net::HarpWriter writer;
    writer.writeU16(static_cast<uint16_t>(deltas.size()));
    for (const auto& d : deltas) {
        writer.writeU8(d.x);
        writer.writeU8(d.y);
        writer.writeU8(d.z);
        writer.writeF32(d.state);
    }

    juce::MemoryBlock payload;
    // Extract payload from writer — we need an intermediate trick:
    // send to all clients by building the packet manually
    // Build the packet and broadcast via session
    session.broadcastToAll(::Harmonia::Net::MsgType::VoxelDelta,
                           writer.getPayload());
}

} // namespace Server
} // namespace Harmonia
