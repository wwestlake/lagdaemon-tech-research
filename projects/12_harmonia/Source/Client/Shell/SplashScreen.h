#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace Harmonia {
class SplashScreen : public juce::Component, private juce::Timer {
public:
    SplashScreen();
    void paint(juce::Graphics&) override;
    void resized() override {}
    
    std::function<void()> onComplete;
    
private:
    void timerCallback() override;
    float time_ = 0.f;
    float fadeOut_ = 0.f;
    int harmonic_ = 1;
    bool completing_ = false;
};
}
