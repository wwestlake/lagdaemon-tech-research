#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <deque>

namespace Harmonia {
class ChatBox : public juce::Component, private juce::Timer {
public:
    ChatBox();
    void paint(juce::Graphics&) override;
    void resized() override;
    void keyPressed(const juce::KeyPress&);
    
    void addMessage(const juce::String& playerName, const juce::String& msg, juce::Colour nameColor);
    std::function<void(juce::String)> onSend;
    
private:
    void timerCallback() override;
    struct ChatLine { juce::String name, text; juce::Colour color; juce::Time time; float alpha = 1.f; };
    std::deque<ChatLine> lines_;
    juce::TextEditor input_;
    bool inputVisible_ = false;
};
}
