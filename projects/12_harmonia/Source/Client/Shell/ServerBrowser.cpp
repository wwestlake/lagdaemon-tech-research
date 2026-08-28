#include "ServerBrowser.h"
#include "Client/UI/DesignTokens.h"

namespace Harmonia {

// ── Dark-themed look-and-feel for the browser ────────────────────────────────
struct HarmoniaLookAndFeel : public juce::LookAndFeel_V4 {
    HarmoniaLookAndFeel() {
        setColour(juce::TextEditor::backgroundColourId,    juce::Colour(0xff0d0d2a));
        setColour(juce::TextEditor::textColourId,          juce::Colour(0xfff0f0ff));
        setColour(juce::TextEditor::outlineColourId,       juce::Colour(0xff00e5ff).withAlpha(0.5f));
        setColour(juce::TextEditor::focusedOutlineColourId,juce::Colour(0xff00e5ff));
        setColour(juce::Label::textColourId,               juce::Colour(0xff8888aa));
        setColour(juce::TextButton::buttonColourId,        juce::Colour(0xff001a2a));
        setColour(juce::TextButton::buttonOnColourId,      juce::Colour(0xff003040));
        setColour(juce::TextButton::textColourOffId,       juce::Colour(0xff00e5ff));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& btn,
                              const juce::Colour&, bool over, bool down) override {
        auto r = btn.getLocalBounds().toFloat().reduced(1.f);
        juce::Colour base = btn.findColour(juce::TextButton::buttonColourId);
        if (down)       base = base.brighter(0.3f);
        else if (over)  base = base.brighter(0.15f);
        g.setColour(base);
        g.fillRoundedRectangle(r, 4.f);
        g.setColour(juce::Colour(0xff00e5ff).withAlpha(over ? 0.8f : 0.4f));
        g.drawRoundedRectangle(r, 4.f, 1.f);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override {
        float size = (float)juce::jmin(15, buttonHeight - 6);
        return juce::Font("Segoe UI", juce::jmax(1.0f, size), juce::Font::plain);
    }
};

// ─────────────────────────────────────────────────────────────────────────────

ServerBrowser::ServerBrowser() {
    laf_ = std::make_unique<HarmoniaLookAndFeel>();
    setLookAndFeel(laf_.get());

    // Title
    titleLabel_.setText("HARMONIA", juce::dontSendNotification);
    titleLabel_.setFont(UI::primaryFont(36.f));
    titleLabel_.setColour(juce::Label::textColourId, UI::kAccentCyan);
    titleLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel_);

    subtitleLabel_.setText("connect to a server", juce::dontSendNotification);
    subtitleLabel_.setFont(UI::primaryFont(13.f));
    subtitleLabel_.setColour(juce::Label::textColourId, UI::kTextSecondary);
    subtitleLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(subtitleLabel_);

    // ── Form fields ──────────────────────────────────────────────────────────
    auto setupField = [&](juce::TextEditor& e, juce::Label& lbl,
                          const juce::String& labelText,
                          const juce::String& placeholder) {
        lbl.setText(labelText, juce::dontSendNotification);
        lbl.setFont(UI::primaryFont(12.f));
        addAndMakeVisible(lbl);
        e.setTextToShowWhenEmpty(placeholder, UI::kTextSecondary);
        e.setFont(UI::primaryFont(14.f));
        addAndMakeVisible(e);
    };

    setupField(hostField_,    hostLabel_,    "Server",      "127.0.0.1");
    setupField(portField_,    portLabel_,    "Port",        "4440");
    setupField(nameField_,    nameLabel_,    "Your Name",   "Traveller");
    setupField(sessionField_, sessionLabel_, "Session",     "main");

    hostField_.setText("127.0.0.1",  juce::dontSendNotification);
    portField_.setText("4440",       juce::dontSendNotification);
    nameField_.setText("Traveller",  juce::dontSendNotification);
    sessionField_.setText("main",    juce::dontSendNotification);

    // ── Buttons ──────────────────────────────────────────────────────────────
    connectBtn_.setButtonText("CONNECT");
    connectBtn_.addListener(this);
    addAndMakeVisible(connectBtn_);

    soloBtn_.setButtonText("SOLO (local)");
    soloBtn_.addListener(this);
    addAndMakeVisible(soloBtn_);

    statusLabel_.setFont(UI::monoFont(12.f));
    statusLabel_.setColour(juce::Label::textColourId, UI::kAccentAmber);
    statusLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(statusLabel_);
}

ServerBrowser::~ServerBrowser() {
    setLookAndFeel(nullptr);
}

void ServerBrowser::resized() {
    const int panelW = 320;
    const int panelH = 450;
    auto panel = getLocalBounds().withSizeKeepingCentre(panelW, panelH);

    titleLabel_.setBounds(panel.removeFromTop(50));
    subtitleLabel_.setBounds(panel.removeFromTop(22));
    panel.removeFromTop(16);

    auto row = [&](juce::Label& lbl, juce::TextEditor& field) {
        lbl.setBounds(panel.removeFromTop(16));
        field.setBounds(panel.removeFromTop(30));
        panel.removeFromTop(10);
    };

    row(hostLabel_,    hostField_);
    row(portLabel_,    portField_);
    row(nameLabel_,    nameField_);
    row(sessionLabel_, sessionField_);
    panel.removeFromTop(6);

    connectBtn_.setBounds(panel.removeFromTop(38));
    panel.removeFromTop(8);
    soloBtn_.setBounds(panel.removeFromTop(30));
    panel.removeFromTop(8);
    statusLabel_.setBounds(panel.removeFromTop(20));
}

void ServerBrowser::paint(juce::Graphics& g) {
    // Background
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff08081a), (float)getWidth() * 0.5f, 0.f,
        juce::Colour(0xff020208), (float)getWidth() * 0.5f, (float)getHeight(),
        false));
    g.fillAll();

    // Panel card
    auto panel = getLocalBounds().withSizeKeepingCentre(340, 400).toFloat();
    g.setColour(juce::Colour(0xff0d0d2a).withAlpha(0.95f));
    g.fillRoundedRectangle(panel, 8.f);
    g.setColour(UI::kAccentCyan.withAlpha(0.2f));
    g.drawRoundedRectangle(panel, 8.f, 1.f);
}

void ServerBrowser::buttonClicked(juce::Button* b) {
    if (b == &connectBtn_) {
        juce::String host    = hostField_.getText().trim();
        int          port    = portField_.getText().getIntValue();
        juce::String name    = nameField_.getText().trim();
        juce::String session = sessionField_.getText().trim();

        if (host.isEmpty())    host    = "127.0.0.1";
        if (port <= 0)         port    = 4440;
        if (name.isEmpty())    name    = "Traveller";
        if (session.isEmpty()) session = "main";

        statusLabel_.setText("Connecting to " + host + ":" + juce::String(port) + "...",
                             juce::dontSendNotification);
        if (onConnect) onConnect(host, port, name, session);

    } else if (b == &soloBtn_ && onSolo) {
        statusLabel_.setText("Starting local server...", juce::dontSendNotification);
        onSolo();
    }
}

void ServerBrowser::setStatus(const juce::String& msg, bool isError) {
    statusLabel_.setColour(juce::Label::textColourId,
                           isError ? juce::Colours::tomato : UI::kAccentAmber);
    statusLabel_.setText(msg, juce::dontSendNotification);
}

} // namespace Harmonia
