#include "HudOverlay.h"
#include "DesignTokens.h"

namespace Harmonia {
HudOverlay::HudOverlay() {}

void HudOverlay::paint(juce::Graphics& g) {
    g.setFont(UI::primaryFont(16.f));
    g.setColour(UI::kTextPrimary);
    g.drawText(regionName_, 10, 10, 200, 20, juce::Justification::topLeft);
}

void HudOverlay::setCurrentRegion(const juce::String& regionName) { regionName_ = regionName; repaint(); }
void HudOverlay::setConnectionStatus(bool connected, int pingMs, int playerCount) {}
void HudOverlay::setCurrentNote(int midiNote, float velocity) {}
void HudOverlay::showHint(const juce::String& text, int displayMs) {}
}
