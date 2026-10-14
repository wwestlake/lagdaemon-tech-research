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
class WidgetPropertiesDialog;

// Custom Analog Gauge Component with runtime configuration and needle scaling
class AnalogGaugeComponent final : public juce::Component
{
public:
    AnalogGaugeComponent(const std::string& title, const std::string& unit, double minVal, double maxVal,
                         int precision = 1,
                         bool hasWarn = false, double warnMin = 0.0, double warnMax = 0.0,
                         bool hasCrit = false, double critMin = 0.0, double critMax = 0.0);
    ~AnalogGaugeComponent() override = default;

    void setValue(double val);
    void resetValue();
    void updateConfig(const WidgetConfig& cfg, bool resetVal = true);

    double getValue() const { return actualVal_; }
    double getClampedValue() const { return val_; }
    double getMinVal() const { return minVal_; }
    double getMaxVal() const { return maxVal_; }
    const std::string& getUnit() const { return unit_; }
    const std::string& getTitle() const { return title_; }
    int getPrecision() const { return precision_; }
    bool hasWarnRange() const { return hasWarn_; }
    double getWarnMin() const { return warnMin_; }
    double getWarnMax() const { return warnMax_; }
    bool hasCritRange() const { return hasCrit_; }
    double getCritMin() const { return critMin_; }
    double getCritMax() const { return critMax_; }

    void setUnit(const std::string& unit);
    void setTitle(const std::string& title);
    void setRange(double minVal, double maxVal);

    void paint(juce::Graphics& g) override;

private:
    std::string title_, unit_;
    double minVal_ = 0.0, maxVal_ = 100.0, val_ = 0.0, actualVal_ = 0.0;
    int precision_ = 1;
    bool hasWarn_ = false;
    double warnMin_ = 0.0, warnMax_ = 0.0;
    bool hasCrit_ = false;
    double critMin_ = 0.0, critMax_ = 0.0;
};

class WidgetPropertiesDialog final : public juce::Component
{
public:
    WidgetPropertiesDialog(WidgetConfig config, std::vector<SignalDef> availableSignals,
                           std::vector<OperatorPanelConfig> panels,
                           std::function<void(const WidgetConfig&)> onApplyCallback);
    ~WidgetPropertiesDialog() override = default;

    void setTitleText(const juce::String& text);
    void setUnitText(const juce::String& text);
    void setMinText(const juce::String& text);
    void setMaxText(const juce::String& text);
    void setSignalText(const juce::String& text);
    void selectSignalByName(const std::string& sigName);
    void setPrecisionIndex(int index);
    void setWarningZone(bool enabled, double minVal, double maxVal);
    void setCriticalZone(bool enabled, double minVal, double maxVal);
    void clickApply();
    const WidgetConfig& getWorkingConfig() const { return cfg_; }

    void resized() override;

private:
    void updateCompatibilityLabel(const SignalDef& s);

    WidgetConfig cfg_;
    std::vector<SignalDef> signals_;
    std::vector<OperatorPanelConfig> panels_;
    std::function<void(const WidgetConfig&)> onApply_;

    juce::Label lblTitle_, lblPanelPicker_, lblUnit_, lblMin_, lblMax_, lblDef_, lblStep_, lblPrec_;
    juce::Label lblSigPicker_, lblSig_, lblSecSig_;
    juce::Label compatLabel_;

    juce::TextEditor titleEd_, unitEd_, minEd_, maxEd_, defEd_, stepEd_;
    juce::ComboBox panelPickerBox_;
    juce::ComboBox precBox_;
    juce::ComboBox sigPickerBox_;
    juce::TextEditor sigEd_, secSigEd_;

    juce::ToggleButton momentaryBtn_;
    juce::ToggleButton verticalBtn_;
    juce::ToggleButton hasWarnBtn_;
    juce::TextEditor warnMinEd_, warnMaxEd_;
    juce::ToggleButton hasCritBtn_;
    juce::TextEditor critMinEd_, critMaxEd_;

    juce::TextButton applyBtn_ { "Apply" };
    juce::TextButton cancelBtn_ { "Cancel" };
};

std::string cleanSignalName(std::string name);
bool resolveNumericSignal(const StateSnapshot& snap, const std::string& rawSignal, double& outVal);
bool resolveBooleanSignal(const StateSnapshot& snap, const std::string& rawSignal, bool& outVal);

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
    std::function<void()> onSaveRequested;
    void setStatusText(const juce::String& text, juce::Colour colour = juce::Colour(0xff66ff99), int durationTicks = 60);

    // Operator control synchronization
    void publishAllControlInputs();

    // Mode Control
    void setDesignMode(bool isDesign);
    bool isDesignMode() const { return designMode_; }

    // Dynamic widget management & freeform placement
    bool addWidget(const WidgetConfig& config);
    bool removeWidget(const std::string& widgetId);
    bool configureWidget(const std::string& widgetId, const WidgetConfig& newConfig);
    const WidgetConfig* getWidgetConfig(const std::string& widgetId) const;
    bool setWidgetBounds(const std::string& widgetId, int x, int y, int width, int height);
    juce::Rectangle<int> getWidgetBounds(const std::string& widgetId) const;

    // Selection, Alignment, and Clipboard
    void selectWidget(const std::string& widgetId, bool addToSelection = false);
    void deselectAll();
    std::vector<std::string> getSelectedWidgetIds() const;
    bool alignSelectedWidgets(const std::string& alignment); // "left", "top", "right", "bottom", "sameWidth", "sameHeight"
    bool duplicateSelectedWidgets();
    bool deleteSelectedWidgets();

    // Signal Connection Discovery
    std::vector<SignalDef> getAvailableSignals() const;

    // Canvas scrolling & inspection
    juce::Point<int> getCanvasScrollPosition() const;
    void setCanvasScrollPosition(int x, int y);
    juce::Rectangle<int> getCanvasVisibleArea() const;
    juce::Rectangle<int> getCanvasTotalArea() const;

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
    bool zoomRecorderAtScreenX(const std::string& widgetId, int screenX, double factor);
    bool fitRecorderRecording(const std::string& widgetId);
    bool exportRecorderCsv(const std::string& widgetId, const juce::File& file, juce::String& error) const;

    // Splitter & Split Ratio
    double getSplitRatio() const;
    bool setSplitRatio(double ratio);
    int getSplitterX() const;
    juce::Component* getSplitterComponent() const;
    juce::Component* getPlotContainerComponent() const;
    bool hasPlotPanel() const;
    juce::Rectangle<int> getCanvasViewportBounds() const;
    juce::Rectangle<int> getPlotContainerBounds() const;

    // Dialog and Menu creation
    void showWidgetPropertiesDialog(const std::string& widgetId);
    std::unique_ptr<WidgetPropertiesDialog> createWidgetPropertiesDialog(const std::string& widgetId);
    void showAddWidgetMenu(juce::Point<int> canvasPos, juce::Component* targetComponent = nullptr);
    juce::Point<int> getNewWidgetSpawnPosition(int widgetW = 160, int widgetH = 40) const;
    void showAlignMenu();
    juce::Component* getWidgetComponent(const std::string& widgetId) const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void timerCallback() override;

    void handleSplitterDragStart();
    void handleSplitterDrag(int deltaX);
    void handleSplitterDragEnd();

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
    juce::TextButton saveButton { "Save" };
    juce::TextButton modeButton { "Design Mode" };
    juce::TextButton addWidgetButton { "+ Add Element" };
    juce::TextButton alignButton { "Align" };
    juce::Label statusLabel;

    // Freeform scrollable canvas viewport
    class ControlPanelCanvas;
    juce::Viewport canvasViewport;
    std::unique_ptr<ControlPanelCanvas> canvasComponent;

    // Dedicated splitter and right-hand plot container
    class ControlPanelSplitter;
    class PlotContainerComponent;
    std::unique_ptr<ControlPanelSplitter> splitter_;
    std::unique_ptr<PlotContainerComponent> plotContainer_;
    int dragStartControlW_ = 0;
    int dragStartAvailableW_ = 0;

    // Interactive Widget references
    struct WidgetView
    {
        WidgetConfig config;
        std::unique_ptr<juce::Component> component;
    };
    std::vector<WidgetView> outputViews_;

    std::shared_ptr<SystemSimulationSession> session_;
    ControlPanelLayout layout_;
    bool designMode_ = false;
    bool notifyingLayout_ = false;
    std::map<std::string, std::unique_ptr<MachineRecording>> plotRecordings_;
    std::map<std::string, bool> plotRecordingEnabled_;
    std::map<std::string, double> activeNumericControlValues_;
    std::map<std::string, bool> activeBooleanControlValues_;
    std::vector<WidgetConfig> clipboard_;

    // Historical data for trend chart
    std::deque<TrendPoint> trendHistory_;

    // Transient status display
    int transientStatusTicks_ = 0;
    juce::String transientStatusText_;
    juce::Colour transientStatusColour_ { 0xff66ff99 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlPanelComponent)
};

} // namespace sim_panel
