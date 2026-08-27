#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace Harmonia {
class HudOverlay : public juce::Component {
public:
    HudOverlay();
    void paint(juce::Graphics&) override;
    
    void setCurrentRegion(const juce::String& regionName);
    void setConnectionStatus(bool connected, int pingMs, int playerCount);
    void setCurrentNote(int midiNote, float velocity);
    void showHint(const juce::String& text, int displayMs = 3000);
    
private:
    juce::String regionName_;
    bool connected_ = false;
    int pingMs_ = 0, playerCount_ = 0;
    int currentNote_ = -1;
    juce::String hintText_;
    juce::Time hintExpiry_;
};
}
