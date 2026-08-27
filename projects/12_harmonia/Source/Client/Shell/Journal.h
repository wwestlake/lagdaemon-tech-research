#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace Harmonia {
struct JournalEntry {
    juce::String title;
    juce::String body;
    juce::Colour colour;
    juce::Time discoveredAt;
};

class Journal : public juce::Component {
public:
    Journal();
    void addEntry(const juce::String& title, const juce::String& body, juce::Colour colour);
    void paint(juce::Graphics&) override;
    void resized() override;
    
    void save(const juce::File& f) const;
    void load(const juce::File& f);
    
private:
    std::vector<JournalEntry> entries_;
    juce::Viewport viewport_;
    juce::Component contentComp_;
};
}
