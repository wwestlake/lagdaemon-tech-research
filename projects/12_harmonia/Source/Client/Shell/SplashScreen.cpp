#include "SplashScreen.h"
#include "Client/UI/DesignTokens.h"

namespace Harmonia {
SplashScreen::SplashScreen() {
    startTimerHz(60);
}

void SplashScreen::paint(juce::Graphics& g) {
    g.fillAll(UI::kBgDeep);
    g.setColour(UI::kTextPrimary.withAlpha(1.0f - fadeOut_));
    g.setFont(UI::primaryFont(48.0f));
    g.drawText("H A R M O N I A", getLocalBounds(), juce::Justification::centred);
}

void SplashScreen::timerCallback() {
    time_ += 0.016f;
    if (time_ > 2.0f && !completing_) {
        completing_ = true;
    }
    if (completing_) {
        fadeOut_ += 0.02f;
        if (fadeOut_ >= 1.0f) {
            stopTimer();
            if (onComplete) onComplete();
        }
    }
    repaint();
}
}
