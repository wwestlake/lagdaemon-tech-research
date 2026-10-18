#include "CadMotionLinkDialog.h"

namespace dcad_motion
{

CadMotionLinkDialog::CadMotionLinkDialog(std::shared_ptr<DcadMotionClient> client)
    : client_(client)
{
    setSize(640, 520);

    titleLabel.setText("CAD Motion Link (DCAD Interface v1)", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(18.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff0f4f8));
    addAndMakeVisible(titleLabel);

    statusLabel.setText("Status: Disconnected", juce::dontSendNotification);
    statusLabel.setFont(juce::Font(14.0f, juce::Font::plain));
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9ab0c4));
    addAndMakeVisible(statusLabel);

    pipePathLabel.setText("Discovered CAD Endpoints:", juce::dontSendNotification);
    pipePathLabel.setFont(juce::Font(13.0f, juce::Font::plain));
    addAndMakeVisible(pipePathLabel);

    endpointComboBox.onChange = [this] {
        int idx = endpointComboBox.getSelectedItemIndex();
        if (idx >= 0 && idx < (int)discoveredEndpoints_.size())
        {
            pipePathEditor.setText(discoveredEndpoints_[idx].pipePath);
        }
    };
    addAndMakeVisible(endpointComboBox);

    refreshEndpointsButton.onClick = [this] { refreshEndpoints(); };
    addAndMakeVisible(refreshEndpointsButton);

    pipePathEditor.setText("\\\\.\\pipe\\DjehutiCAD.Motion", false);
    pipePathEditor.setFont(juce::Font(13.0f, juce::Font::plain));
    addAndMakeVisible(pipePathEditor);

    connectButton.onClick = [this] {
        if (client_)
        {
            juce::String path = pipePathEditor.getText().trim();
            if (path.isNotEmpty())
                client_->connectToPipe(path.toStdString());
        }
    };
    addAndMakeVisible(connectButton);

    disconnectButton.onClick = [this] {
        if (client_)
            client_->disconnect(true);
    };
    addAndMakeVisible(disconnectButton);

    addAndMakeVisible(lifecycleGroup);

    startButton.onClick = [this] {
        if (client_) client_->executeLifecycleCommand("start");
    };
    addAndMakeVisible(startButton);

    pauseButton.onClick = [this] {
        if (client_) client_->executeLifecycleCommand("pause");
    };
    addAndMakeVisible(pauseButton);

    resumeButton.onClick = [this] {
        if (client_) client_->executeLifecycleCommand("resume");
    };
    addAndMakeVisible(resumeButton);

    stopButton.onClick = [this] {
        if (client_) client_->executeLifecycleCommand("stop");
    };
    addAndMakeVisible(stopButton);

    resetButton.onClick = [this] {
        if (client_) client_->executeLifecycleCommand("reset");
    };
    addAndMakeVisible(resetButton);

    stepButton.onClick = [this] {
        if (client_) client_->executeLifecycleCommand("step", 1000000000LL); // 1 ms in ps
    };
    addAndMakeVisible(stepButton);

    detailsEditor.setMultiLine(true);
    detailsEditor.setReadOnly(true);
    detailsEditor.setFont(juce::Font("Consolas", 12.0f, juce::Font::plain));
    detailsEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff121519));
    detailsEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xffd0e0f0));
    addAndMakeVisible(detailsEditor);

    refreshEndpoints();
    startTimer(200); // 5 Hz refresh
}

CadMotionLinkDialog::~CadMotionLinkDialog()
{
    stopTimer();
}

void CadMotionLinkDialog::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1b2026));
}

void CadMotionLinkDialog::resized()
{
    auto area = getLocalBounds().reduced(16);

    titleLabel.setBounds(area.removeFromTop(28));
    statusLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);

    auto discRow = area.removeFromTop(28);
    pipePathLabel.setBounds(discRow.removeFromLeft(160));
    refreshEndpointsButton.setBounds(discRow.removeFromRight(80));
    discRow.removeFromRight(8);
    endpointComboBox.setBounds(discRow);

    area.removeFromTop(8);
    auto pipeRow = area.removeFromTop(28);
    pipeRow.removeFromLeft(160);
    disconnectButton.setBounds(pipeRow.removeFromRight(90));
    pipeRow.removeFromRight(8);
    connectButton.setBounds(pipeRow.removeFromRight(90));
    pipeRow.removeFromRight(8);
    pipePathEditor.setBounds(pipeRow);

    area.removeFromTop(16);
    auto lifeBounds = area.removeFromTop(80);
    lifecycleGroup.setBounds(lifeBounds);
    auto btnRow = lifeBounds.reduced(10, 24);
    int btnW = (btnRow.getWidth() - 5 * 8) / 6;
    startButton.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(8);
    pauseButton.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(8);
    resumeButton.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(8);
    stopButton.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(8);
    resetButton.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(8);
    stepButton.setBounds(btnRow);

    area.removeFromTop(12);
    detailsEditor.setBounds(area);
}

void CadMotionLinkDialog::refreshEndpoints()
{
    discoveredEndpoints_ = DcadMotionClient::discoverEndpoints();
    endpointComboBox.clear();

    if (discoveredEndpoints_.empty())
    {
        endpointComboBox.addItem("No active CAD endpoints found (%LOCALAPPDATA%\\DjehutiCAD\\MotionEndpoints)", 1);
        endpointComboBox.setSelectedId(1, juce::dontSendNotification);
    }
    else
    {
        for (size_t i = 0; i < discoveredEndpoints_.size(); ++i)
        {
            const auto& ep = discoveredEndpoints_[i];
            juce::String label = juce::String(ep.documentName) + " [" + ep.pipeName + "] (PID " + juce::String((int)ep.processId) + ")";
            endpointComboBox.addItem(label, (int)(i + 1));
        }
        endpointComboBox.setSelectedId(1, juce::dontSendNotification);
        pipePathEditor.setText(discoveredEndpoints_[0].pipePath);
    }
}

void CadMotionLinkDialog::timerCallback()
{
    updateUiFromStatus();
}

void CadMotionLinkDialog::updateUiFromStatus()
{
    if (!client_) return;
    auto s = client_->getStatus();

    juce::String stateStr;
    switch (s.state)
    {
        case LinkState::Disconnected: stateStr = "Disconnected"; break;
        case LinkState::Connecting:   stateStr = "Connecting..."; break;
        case LinkState::Handshaking:  stateStr = "Handshaking"; break;
        case LinkState::Advertising:  stateStr = "Advertising Signals"; break;
        case LinkState::Binding:      stateStr = "Binding"; break;
        case LinkState::Synchronizing:stateStr = "Synchronizing State"; break;
        case LinkState::Streaming:    stateStr = "Streaming Motion (" + juce::String(s.lifecycle) + ")"; break;
        case LinkState::Error:        stateStr = "Error: " + juce::String(s.lastError); break;
    }

    statusLabel.setText("Status: " + stateStr, juce::dontSendNotification);
    if (s.state == LinkState::Streaming)
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff3ddc84));
    else if (s.state == LinkState::Error)
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff5555));
    else
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9ab0c4));

    connectButton.setEnabled(s.state == LinkState::Disconnected || s.state == LinkState::Error);
    disconnectButton.setEnabled(s.state != LinkState::Disconnected);

    // Update details editor
    juce::String details;
    details << "=== Connection & Model ===\n";
    details << "Pipe:        " << s.pipePath << "\n";
    details << "Model ID:    " << s.modelId << "\n";
    details << "Document:    " << s.documentName << "\n";
    details << "Interface:   rev " << s.interfaceRevision << " | Profile: rev " << s.profileRevision << "\n";
    details << "Active Mappings: " << (int)s.mappingCount << "\n\n";

    details << "=== Simulation State ===\n";
    details << "Simulation ID: " << s.simulationId << "\n";
    details << "Lifecycle:     " << s.lifecycle << "\n";
    details << "Timeline Gen:  " << s.timelineGeneration << "\n";
    details << "Sequence:      " << (int64_t)s.lastSequence << "\n";
    details << "Sim Time (ps): " << (int64_t)s.simTimePs << " (" << juce::String((double)s.simTimePs / 1e12, 4) << " s)\n";
    details << "DCAD Controls: " << (s.dcadLifecycleControl ? "Supported" : "Not advertised") << "\n\n";

    details << "=== Advertised Signals ===\n";
    const auto signals = client_->getSignalCatalog();
    for (const auto& sig : signals)
    {
        details << "  - " << sig.id << " (" << sig.quantity << ", " << sig.unit << ", " << sig.kind << ")\n";
    }

    detailsEditor.setText(details, false);
}

} // namespace dcad_motion
