#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace Harmonia {
class ServerBrowser : public juce::Component, private juce::Button::Listener {
public:
    ServerBrowser();
    void resized() override;
    void paint(juce::Graphics&) override;
    
    std::function<void(juce::String host, int port, juce::String playerName, juce::String session)> onConnect;
    std::function<void()> onSolo;
    
private:
    void buttonClicked(juce::Button*) override;
    
    juce::Label titleLabel_;
    juce::TextEditor hostField_, portField_, nameField_, sessionField_;
    juce::TextButton connectBtn_{"Connect"}, soloBtn_{"Solo"};
    juce::Label statusLabel_;
};
}
