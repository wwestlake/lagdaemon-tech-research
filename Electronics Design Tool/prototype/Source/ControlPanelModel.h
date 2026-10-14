#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sim_panel
{

enum class WidgetType
{
    // Inputs (Left region)
    PushButton,
    ToggleSwitch,
    Knob,
    Slider,
    ModeSelector,
    EmergencyStop,

    // Outputs (Right region)
    AnalogGauge,
    DigitalReadout,
    IndicatorLamp,
    StatusBadge,
    TrendChart,
    ClockDisplay,
    EventLog,
    PlotPanel,
    BarGauge,
    LatchingButton,
    IndicatorButton
};

struct PlotChannelConfig
{
    std::string id;
    std::string title;
    std::string signalName;
    std::string unit;
    std::uint32_t colourArgb = 0xff4cc9f0;
    double scale = 1.0;
    double offset = 0.0;
    double minValue = 0.0;
    double maxValue = 0.0;
    bool visible = true;
    int laneHeight = 165;
    bool collapsed = false;

    juce::var toJson() const;
    static PlotChannelConfig fromJson(const juce::var& json);
};

struct WidgetConfig
{
    std::string id;
    WidgetType type = WidgetType::PushButton;
    std::string title;
    std::string signalName;
    std::string unit;

    double minValue = 0.0;
    double maxValue = 100.0;
    double defaultValue = 0.0;
    double stepSize = 1.0;

    // Relative column and row inside the panel layout (legacy)
    int column = 0; // 0 = Left (Inputs), 1 = Right (Outputs)
    int order = 0;
    int height = 32;

    std::vector<std::string> options; // for ModeSelector

    // Extended operator-console metadata. Old machine files omit these and
    // therefore continue to load into the default "main" panel.
    std::string panelId;
    std::vector<PlotChannelConfig> plotChannels;
    std::string plotMode = "Strip Chart";
    int xChannel = 0;
    int yChannel = 1;
    double visibleHistorySeconds = 10.0;
    int maxSamples = 20000;
    bool autoScroll = true;

    // Freeform layout geometry (0 or negative width triggers legacy auto-layout migration)
    int x = 0;
    int y = 0;
    int width = 0;
    int zOrder = 0;

    // Extended styling & precision
    int precision = 1;
    bool vertical = false;         // for BarGauge and Slider (horizontal vs vertical)
    bool momentary = true;         // for PushButton (momentary vs latching)

    // Warning and critical ranges for gauges & readouts
    double warnMin = 0.0;
    double warnMax = 0.0;
    bool hasWarnRange = false;
    double critMin = 0.0;
    double critMax = 0.0;
    bool hasCritRange = false;

    // Secondary signal (e.g. status indicator for IndicatorButton)
    std::string secondarySignal;
};

struct OperatorPanelConfig
{
    std::string id = "main";
    std::string title = "Main";
    int controlColumns = 1;
    int instrumentColumns = 1;
    double splitRatio = 0.22;
    int spacing = 6;
    int canvasWidth = 1600;
    int canvasHeight = 1000;
    bool designMode = false;

    juce::var toJson() const;
    static OperatorPanelConfig fromJson(const juce::var& json);
};

struct ControlPanelLayout
{
    std::string title = "Operator Control Panel";
    std::string activePanelId = "main";
    std::vector<OperatorPanelConfig> panels;
    std::vector<WidgetConfig> widgets;

    static ControlPanelLayout createDefaultMotorPanel();
    static ControlPanelLayout createDualMotorCoordinatedPanel();
    void ensureDefaultPanel();
    void ensureWidgetBounds();
    bool hasPanel(const std::string& panelId) const;
    const OperatorPanelConfig* findPanel(const std::string& panelId) const;
    OperatorPanelConfig* findPanel(const std::string& panelId);
    WidgetConfig* findWidget(const std::string& widgetId);
    const WidgetConfig* findWidget(const std::string& widgetId) const;
    juce::var toJson() const;
    static ControlPanelLayout fromJson(const juce::var& json);
};

} // namespace sim_panel
