#include "MessageHandler.h"
#include "Shared/Network/HarpSerializer.h"

namespace Harmonia {

MessageHandler::MessageHandler(WorldState* ws, AudioEngine* audio, OpenWorld* world)
    : worldState_(ws), audio_(audio), world_(world) {}

void MessageHandler::onConnected(uint32_t /*playerID*/, const juce::String& /*serverName*/) {}
void MessageHandler::onDisconnected(const juce::String& /*reason*/) {}

void MessageHandler::onMessage(Net::MsgType type, const juce::MemoryBlock& payload) {
    // HarpReader takes const juce::MemoryBlock&
    Net::HarpReader reader(payload);

    switch (type) {
        case Net::MsgType::PlayerJoined: {
            uint32_t id       = reader.readU32();
            juce::String name = reader.readString();
            float hue         = reader.readF32();
            glm::vec3 pos(reader.readF32(), reader.readF32(), reader.readF32());
            if (world_) world_->onPlayerJoined(id, name, hue, pos);
            break;
        }
        case Net::MsgType::PlayerLeft: {
            uint32_t id = reader.readU32();
            if (world_) world_->onPlayerLeft(id);
            break;
        }
        case Net::MsgType::PlayerPosition: {
            uint32_t id = reader.readU32();
            glm::vec3 pos(reader.readF32(), reader.readF32(), reader.readF32());
            float yaw = reader.readF32();
            if (world_) world_->onPlayerPosition(id, pos, yaw);
            break;
        }
        case Net::MsgType::NoteOn: {
            uint32_t id = reader.readU32();
            int   note  = reader.readU8();
            float vel   = reader.readF32();
            if (world_) world_->onNoteOn(id, note, vel);
            if (audio_) audio_->noteOn(note, vel, 1);
            break;
        }
        case Net::MsgType::NoteOff: {
            uint32_t id = reader.readU32();
            int note    = reader.readU8();
            if (world_) world_->onNoteOff(id, note);
            if (audio_) audio_->noteOff(note, 1);
            break;
        }
        case Net::MsgType::VoxelDelta: {
            uint16_t count = reader.readU16();
            if (worldState_) {
                if (!worldState_->livingGrid) worldState_->livingGrid = std::make_shared<VoxelGrid>(24, 8, 16);
                for (int i = 0; i < (int)count; ++i) {
                    uint8_t x = reader.readU8();
                    uint8_t y = reader.readU8();
                    uint8_t z = reader.readU8();
                    float   s = reader.readF32();
                    worldState_->livingGrid->setVoxel(x, y, z, s);
                }
            }
            break;
        }
        case Net::MsgType::VoxelFullSync: {
            if (worldState_) {
                if (!worldState_->livingGrid) worldState_->livingGrid = std::make_shared<VoxelGrid>(24, 8, 16);
                worldState_->livingGrid->deserialise(payload);
            }
            break;
        }

        default:
            break;
    }
}

} // namespace Harmonia
