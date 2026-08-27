#include "ServerBrowser.h"
#include "Client/UI/DesignTokens.h"

namespace Harmonia {
ServerBrowser::ServerBrowser() {
    addAndMakeVisible(connectBtn_);
    addAndMakeVisible(soloBtn_);
    connectBtn_.addListener(this);
    soloBtn_.addListener(this);
}

void ServerBrowser::resized() {
    auto b = getLocalBounds().withSizeKeepingCentre(300, 400);
    connectBtn_.setBounds(b.removeFromBottom(40));
    soloBtn_.setBounds(b.removeFromBottom(40).withTrimmedTop(10));
}

void ServerBrowser::paint(juce::Graphics& g) {
    g.fillAll(UI::kBgDeep);
}

void ServerBrowser::buttonClicked(juce::Button* b) {
    if (b == &connectBtn_ && onConnect) {
        onConnect("127.0.0.1", 4440, "Player", "");
    } else if (b == &soloBtn_ && onSolo) {
        onSolo();
    }
}
}
