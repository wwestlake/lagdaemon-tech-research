#pragma once

#include "DcadMotionClient.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace dcad_motion
{

class CadMotionLinkDialog : public juce::Component,
                            private juce::Timer
{
public:
    explicit CadMotionLinkDialog(std::shared_ptr<DcadMotionClient> client);
    ~CadMotionLinkDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshEndpoints();
    void updateUiFromStatus();

    std::shared_ptr<DcadMotionClient> client_;

    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::Label pipePathLabel;
    juce::TextEditor pipePathEditor;
    juce::ComboBox endpointComboBox;
    juce::TextButton refreshEndpointsButton { "Refresh" };
    juce::TextButton connectButton { "Connect" };
    juce::TextButton disconnectButton { "Disconnect" };

    // Lifecycle Controls
    juce::GroupComponent lifecycleGroup { "lifecycle", "Simulation Lifecycle Controls" };
    juce::TextButton startButton { "Start" };
    juce::TextButton pauseButton { "Pause" };
    juce::TextButton resumeButton { "Resume" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton resetButton { "Reset" };
    juce::TextButton stepButton { "Single Step (1ms)" };

    // Status details
    juce::TextEditor detailsEditor;

    std::vector<EndpointInfo> discoveredEndpoints_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CadMotionLinkDialog)
};

} // namespace dcad_motion
