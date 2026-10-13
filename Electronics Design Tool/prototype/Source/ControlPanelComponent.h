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

    struct StripChartLaneSnapshot
    {
        std::string channelId;
        std::string title;
        std::string signalName;
        std::string unit;
        bool visible = true;
        bool collapsed = false;
        bool signalAvailable = false;
        int finiteSampleCount = 0;
        double latestValue = 0.0;
        double yMin = 0.0;
        double yMax = 0.0;
        juce::Rectangle<int> boundsInRecorder;
        juce::Rectangle<int> waveformBounds;
    };

    struct StripChartRecorderSnapshot
    {
        std::string widgetId;
        std::string mode;
        bool recordEnabled = true;
        bool liveFollow = true;
        bool viewPaused = false;
        double secondsPerDivision = 1.0;
        double visibleWindowSeconds = 10.0;
        double viewStartTime = 0.0;
        double viewEndTime = 10.0;
        int totalSamples = 0;
        juce::Rectangle<int> recorderBounds;
        std::vector<StripChartLaneSnapshot> lanes;
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
    std::function<bool()> onEnsureCompiled;

    // Panel management
    std::string createNewPanel(const std::string& title = {});
    bool renameActivePanel(const std::string& newTitle);
    bool removeActivePanel();
    void setActivePanel(const std::string& panelId);
    int getPanelCount() const;
    std::string getActivePanelId() const;

    // Strip-chart recorder inspection & control
    void sampleLatestNow();
    std::vector<StripChartRecorderSnapshot> getStripChartRecorderSnapshots() const;
    bool setRecorderTimebaseSecondsPerDiv(const std::string& widgetId, double secondsPerDiv);
    bool scrollRecorderHistory(const std::string& widgetId, double centerTimeSeconds);
    bool setRecorderLiveFollow(const std::string& widgetId, bool liveFollow);
    bool setRecorderViewPaused(const std::string& widgetId, bool paused);
    bool setRecorderRecordingEnabled(const std::string& widgetId, bool enabled);
    bool setRecorderLaneCollapsed(const std::string& widgetId, int laneIndex, bool collapsed);
    bool setRecorderLaneHeight(const std::string& widgetId, int laneIndex, int heightPx);
    bool exportRecorderCsv(const std::string& widgetId, const juce::File& file, juce::String& error) const;

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
    bool notifyingLayout_ = false;
    std::map<std::string, std::unique_ptr<MachineRecording>> plotRecordings_;
    std::map<std::string, bool> plotRecordingEnabled_;
    std::map<std::string, double> activeNumericControlValues_;
    std::map<std::string, bool> activeBooleanControlValues_;

    // Historical data for trend chart
    std::deque<TrendPoint> trendHistory_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlPanelComponent)
};

} // namespace sim_panel
