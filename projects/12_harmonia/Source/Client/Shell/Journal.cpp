#include "Journal.h"
#include "Client/UI/DesignTokens.h"

namespace Harmonia {
Journal::Journal() {
    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&contentComp_, false);
}

void Journal::addEntry(const juce::String& title, const juce::String& body, juce::Colour colour) {
    entries_.push_back({title, body, colour, juce::Time::getCurrentTime()});
    repaint();
}

void Journal::paint(juce::Graphics& g) {
    g.fillAll(UI::kBgMid.withAlpha(0.8f));
}

void Journal::resized() {
    viewport_.setBounds(getLocalBounds());
}

void Journal::save(const juce::File& f) const {}
void Journal::load(const juce::File& f) {}
}
