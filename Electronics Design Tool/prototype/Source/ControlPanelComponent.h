#pragma once

#include "ControlPanelModel.h"
#include "SystemSimulationSession.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace machine_builder
{
struct MachineDesign;
}

namespace sim_panel
{

struct MachineRecording;

class ControlPanelComponent final : public juce::Component, private juce::Timer
{
public:
    struct TrendPoint
    {
        double time = 0.0;
        double target = 0.0;
        double actual = 0.0;
    };

    ControlPanelComponent();
    explicit ControlPanelComponent(std::shared_ptr<SystemSimulationSession> session);
    ~ControlPanelComponent() override;

    SystemSimulationSession* getSession() const { return session_.get(); }
    void setSession(std::shared_ptr<SystemSimulationSession> session) { session_ = session; }

    bool loadMachine(const machine_builder::MachineDesign& machine, std::string* error = nullptr);
    void setLayout(const ControlPanelLayout& layout);
    const ControlPanelLayout& getLayout() const { return layout_; }
    std::function<void(const ControlPanelLayout&)> onLayoutChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;

    // Subcomponents
    void buildWidgets();
    void rebuildPanelSelector();
    void setActivePanel(const std::string& panelId);
    const OperatorPanelConfig& activePanel() const;
    void notifyLayoutChanged();
    void updateWidgetConfig(const WidgetConfig& config);
    MachineRecording& recordingFor(const std::string& widgetId);
    bool& recordingEnabledFor(const std::string& widgetId);

    // Transport Bar components
    juce::TextButton runButton { "Run" };
    juce::TextButton pauseButton { "Pause" };
    juce::TextButton stepButton { "Step" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton resetButton { "Reset" };
    juce::ComboBox rateBox;
    juce::ComboBox panelBox;
    juce::TextButton newPanelButton { "New Panel" };
    juce::TextButton renamePanelButton { "Rename" };
    juce::TextButton removePanelButton { "Remove" };
    juce::Label statusLabel;

    // Viewports for scrolling input/output columns
    juce::Viewport inputViewport;
    juce::Viewport outputViewport;
    std::unique_ptr<juce::Component> inputContainer;
    std::unique_ptr<juce::Component> outputContainer;

    // Interactive Widget references
    struct WidgetView
    {
        WidgetConfig config;
        std::unique_ptr<juce::Component> component;
    };
    std::vector<WidgetView> inputViews_;
    std::vector<WidgetView> outputViews_;

    std::shared_ptr<SystemSimulationSession> session_;
    ControlPanelLayout layout_;
    int splitterX_ = -1;
    bool draggingSplitter_ = false;
    std::map<std::string, std::unique_ptr<MachineRecording>> plotRecordings_;
    std::map<std::string, bool> plotRecordingEnabled_;

    // Historical data for trend chart
    std::deque<TrendPoint> trendHistory_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlPanelComponent)
};

} // namespace sim_panel
