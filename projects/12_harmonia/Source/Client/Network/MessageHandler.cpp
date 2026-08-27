#include "MessageHandler.h"
#include "Shared/Network/HarpSerializer.h"

namespace Harmonia {
MessageHandler::MessageHandler(WorldState* ws, AudioEngine* audio, OpenWorld* world)
    : worldState_(ws), audio_(audio), world_(world) {}

void MessageHandler::onConnected(uint32_t playerID, const juce::String& serverName) {}
void MessageHandler::onDisconnected(const juce::String& reason) {}

void MessageHandler::onMessage(Net::MsgType type, const juce::MemoryBlock& payload) {
    Net::HarpReader reader(payload.getData(), payload.getSize());
    switch (type) {
        case Net::MsgType::PlayerJoined: {
            uint32_t id = reader.readU32();
            juce::String name = reader.readString();
            float hue = reader.readFloat();
            glm::vec3 pos(reader.readFloat(), reader.readFloat(), reader.readFloat());
            world_->onPlayerJoined(id, name, hue, pos);
            break;
        }
        case Net::MsgType::PlayerLeft: {
            uint32_t id = reader.readU32();
            world_->onPlayerLeft(id);
            break;
        }
        case Net::MsgType::PlayerPosition: {
            uint32_t id = reader.readU32();
            glm::vec3 pos(reader.readFloat(), reader.readFloat(), reader.readFloat());
            float yaw = reader.readFloat();
            world_->onPlayerPosition(id, pos, yaw);
            break;
        }
        case Net::MsgType::NoteOn: {
            uint32_t id = reader.readU32();
            int note = reader.readU8();
            float vel = reader.readFloat();
            world_->onNoteOn(id, note, vel);
            audio_->noteOn(note, vel, 1);
            break;
        }
        case Net::MsgType::NoteOff: {
            uint32_t id = reader.readU32();
            int note = reader.readU8();
            world_->onNoteOff(id, note);
            audio_->noteOff(note, 1);
            break;
        }
        default: break;
    }
}
}
