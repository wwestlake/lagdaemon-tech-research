#pragma once

#include <juce_core/juce_core.h>

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
    EventLog
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

    // Relative column and row inside the panel layout
    int column = 0; // 0 = Left (Inputs), 1 = Right (Outputs)
    int order = 0;
    int height = 80;

    std::vector<std::string> options; // for ModeSelector
};

struct ControlPanelLayout
{
    std::string title = "Operator Control Panel";
    std::vector<WidgetConfig> widgets;

    static ControlPanelLayout createDefaultMotorPanel();
    juce::var toJson() const;
    static ControlPanelLayout fromJson(const juce::var& json);
};

} // namespace sim_panel
