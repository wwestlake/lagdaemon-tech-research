#include "ChatBox.h"

namespace Harmonia {
ChatBox::ChatBox() {
    addAndMakeVisible(input_);
    input_.setVisible(false);
    startTimerHz(10);
}

void ChatBox::paint(juce::Graphics& g) {}

void ChatBox::resized() {
    input_.setBounds(0, getHeight() - 30, getWidth(), 30);
}

bool ChatBox::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::returnKey && inputVisible_) {
        if (onSend) onSend(input_.getText());
        input_.clear();
        return true;
    }
    return false;
}
void ChatBox::addMessage(const juce::String& playerName, const juce::String& msg, juce::Colour nameColor) {}
void ChatBox::timerCallback() {}
}
