#pragma once

#include "ControlPanelModel.h"
#include "SystemSimulationSession.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace machine_builder
{
struct MachineDesign;
}

namespace sim_panel
{

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

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    // Subcomponents
    void buildWidgets();

    // Transport Bar components
    juce::TextButton runButton { "Run" };
    juce::TextButton pauseButton { "Pause" };
    juce::TextButton stepButton { "Step" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton resetButton { "Reset" };
    juce::ComboBox rateBox;
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

    // Historical data for trend chart
    std::deque<TrendPoint> trendHistory_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlPanelComponent)
};

} // namespace sim_panel
