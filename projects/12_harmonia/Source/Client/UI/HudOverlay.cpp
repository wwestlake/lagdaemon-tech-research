#include "HudOverlay.h"
#include "DesignTokens.h"

namespace Harmonia {
HudOverlay::HudOverlay() {
    setInterceptsMouseClicks(false, false);
}

void HudOverlay::paint(juce::Graphics& g) {
    g.setFont(UI::primaryFont(16.f));
    g.setColour(UI::kTextPrimary);
    g.drawText(regionName_, 10, 10, 200, 20, juce::Justification::topLeft);

    // Draw center crosshair
    auto bounds = getLocalBounds();
    int cx = bounds.getWidth() / 2;
    int cy = bounds.getHeight() / 2;
    
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.drawLine(cx - 5.0f, cy, cx + 6.0f, cy, 2.0f);
    g.drawLine(cx, cy - 5.0f, cx, cy + 6.0f, 2.0f);
}

void HudOverlay::setCurrentRegion(const juce::String& regionName) { regionName_ = regionName; repaint(); }
void HudOverlay::setConnectionStatus(bool connected, int pingMs, int playerCount) {}
void HudOverlay::setCurrentNote(int midiNote, float velocity) {}
void HudOverlay::showHint(const juce::String& text, int displayMs) {}
}
