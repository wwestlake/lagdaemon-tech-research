#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

namespace Harmonia {
class MidiEngine {
public:
    MidiEngine();
    ~MidiEngine();
    
    juce::StringArray getAvailableDevices() const;
    bool openDevice(const juce::String& deviceName);
    void closeDevice();
    bool isOpen() const;
    
    void sendNoteOn(int midiNote, int velocity, int channel);
    void sendNoteOff(int midiNote, int channel);
    void sendAllNotesOff();
    void scheduleNoteOff(int midiNote, int channel, double durationMs);
    
private:
    std::unique_ptr<juce::MidiOutput> output_;
    juce::CriticalSection lock_;
};
}
