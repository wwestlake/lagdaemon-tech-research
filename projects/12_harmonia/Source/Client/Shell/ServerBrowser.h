#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace Harmonia {

class ServerBrowser : public juce::Component, private juce::Button::Listener {
public:
    ServerBrowser();
    ~ServerBrowser() override;

    void resized() override;
    void paint(juce::Graphics&) override;

    // Called by HarmoniaApp when connection result comes back
    void setStatus(const juce::String& msg, bool isError = false);

    std::function<void(juce::String host, int port,
                       juce::String playerName, juce::String session)> onConnect;
    std::function<void()> onSolo;

private:
    void buttonClicked(juce::Button*) override;

    std::unique_ptr<juce::LookAndFeel_V4> laf_;

    juce::Label     titleLabel_, subtitleLabel_;
    juce::Label     hostLabel_, portLabel_, nameLabel_, sessionLabel_;
    juce::TextEditor hostField_, portField_, nameField_, sessionField_;
    juce::TextButton connectBtn_{"Connect"}, soloBtn_{"Solo"};
    juce::Label     statusLabel_;
};

} // namespace Harmonia
