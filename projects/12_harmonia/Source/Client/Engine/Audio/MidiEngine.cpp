#include "MidiEngine.h"

namespace Harmonia {
MidiEngine::MidiEngine() {}
MidiEngine::~MidiEngine() { closeDevice(); }

juce::StringArray MidiEngine::getAvailableDevices() const {
    juce::StringArray devs;
    for (auto d : juce::MidiOutput::getAvailableDevices()) devs.add(d.name);
    return devs;
}

bool MidiEngine::openDevice(const juce::String& deviceName) {
    for (auto d : juce::MidiOutput::getAvailableDevices()) {
        if (d.name == deviceName) {
            output_ = juce::MidiOutput::openDevice(d.identifier);
            return output_ != nullptr;
        }
    }
    return false;
}

void MidiEngine::closeDevice() {
    juce::ScopedLock lock(lock_);
    output_.reset();
}

bool MidiEngine::isOpen() const { return output_ != nullptr; }

void MidiEngine::sendNoteOn(int midiNote, int velocity, int channel) {
    juce::ScopedLock lock(lock_);
    if (output_) output_->sendMessageNow(juce::MidiMessage::noteOn(channel, midiNote, (uint8_t)velocity));
}

void MidiEngine::sendNoteOff(int midiNote, int channel) {
    juce::ScopedLock lock(lock_);
    if (output_) output_->sendMessageNow(juce::MidiMessage::noteOff(channel, midiNote, (uint8_t)0));
}

void MidiEngine::sendAllNotesOff() {
    juce::ScopedLock lock(lock_);
    if (output_) output_->sendMessageNow(juce::MidiMessage::allNotesOff(1));
}

void MidiEngine::scheduleNoteOff(int midiNote, int channel, double durationMs) {
    // Stub
}
}
