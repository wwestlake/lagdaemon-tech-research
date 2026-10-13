#include "ControlPanelModel.h"

#include <algorithm>

namespace sim_panel
{

juce::var PlotChannelConfig::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", juce::String(id));
    obj->setProperty("title", juce::String(title));
    obj->setProperty("signalName", juce::String(signalName));
    obj->setProperty("unit", juce::String(unit));
    obj->setProperty("colourArgb", (juce::int64)colourArgb);
    obj->setProperty("scale", scale);
    obj->setProperty("offset", offset);
    obj->setProperty("minValue", minValue);
    obj->setProperty("maxValue", maxValue);
    obj->setProperty("visible", visible);
    obj->setProperty("laneHeight", laneHeight);
    obj->setProperty("collapsed", collapsed);
    return juce::var(obj);
}

PlotChannelConfig PlotChannelConfig::fromJson(const juce::var& json)
{
    PlotChannelConfig c;
    if (const auto* obj = json.getDynamicObject())
    {
        c.id = obj->getProperty("id").toString().toStdString();
        c.title = obj->getProperty("title").toString().toStdString();
        c.signalName = obj->getProperty("signalName").toString().toStdString();
        c.unit = obj->getProperty("unit").toString().toStdString();
        c.colourArgb = obj->hasProperty("colourArgb") ? (std::uint32_t)(juce::int64)obj->getProperty("colourArgb") : c.colourArgb;
        c.scale = obj->hasProperty("scale") ? (double)obj->getProperty("scale") : c.scale;
        c.offset = obj->hasProperty("offset") ? (double)obj->getProperty("offset") : c.offset;
        c.minValue = obj->hasProperty("minValue") ? (double)obj->getProperty("minValue") : c.minValue;
        c.maxValue = obj->hasProperty("maxValue") ? (double)obj->getProperty("maxValue") : c.maxValue;
        c.visible = obj->hasProperty("visible") ? (bool)obj->getProperty("visible") : c.visible;
        c.laneHeight = obj->hasProperty("laneHeight") ? std::clamp((int)obj->getProperty("laneHeight"), 64, 480) : c.laneHeight;
        c.collapsed = obj->hasProperty("collapsed") ? (bool)obj->getProperty("collapsed") : c.collapsed;
    }
    return c;
}

juce::var OperatorPanelConfig::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", juce::String(id));
    obj->setProperty("title", juce::String(title));
    obj->setProperty("controlColumns", controlColumns);
    obj->setProperty("instrumentColumns", instrumentColumns);
    obj->setProperty("splitRatio", splitRatio);
    obj->setProperty("spacing", spacing);
    return juce::var(obj);
}

OperatorPanelConfig OperatorPanelConfig::fromJson(const juce::var& json)
{
    OperatorPanelConfig p;
    if (const auto* obj = json.getDynamicObject())
    {
        p.id = obj->getProperty("id").toString().toStdString();
        p.title = obj->getProperty("title").toString().toStdString();
        p.controlColumns = obj->hasProperty("controlColumns") ? std::max(1, (int)obj->getProperty("controlColumns")) : p.controlColumns;
        p.instrumentColumns = obj->hasProperty("instrumentColumns") ? std::max(1, (int)obj->getProperty("instrumentColumns")) : p.instrumentColumns;
        p.splitRatio = obj->hasProperty("splitRatio") ? juce::jlimit(0.20, 0.80, (double)obj->getProperty("splitRatio")) : p.splitRatio;
        p.spacing = obj->hasProperty("spacing") ? std::max(2, (int)obj->getProperty("spacing")) : p.spacing;
    }
    if (p.id.empty())
        p.id = "main";
    if (p.title.empty())
        p.title = p.id;
    return p;
}

void ControlPanelLayout::ensureDefaultPanel()
{
    if (panels.empty())
        panels.push_back({ "main", title.empty() ? std::string("Main") : title, 1, 1, 0.42, 8 });
    if (activePanelId.empty())
        activePanelId = panels.front().id;
    for (auto& w : widgets)
        if (w.panelId.empty())
            w.panelId = panels.front().id;
}

bool ControlPanelLayout::hasPanel(const std::string& panelId) const
{
    for (const auto& p : panels)
        if (p.id == panelId)
            return true;
    return false;
}

const OperatorPanelConfig* ControlPanelLayout::findPanel(const std::string& panelId) const
{
    const auto id = panelId.empty() ? activePanelId : panelId;
    for (const auto& p : panels)
        if (p.id == id)
            return &p;
    if (panelId.empty() || panelId == activePanelId)
        return panels.empty() ? nullptr : &panels.front();
    return nullptr;
}

OperatorPanelConfig* ControlPanelLayout::findPanel(const std::string& panelId)
{
    const auto id = panelId.empty() ? activePanelId : panelId;
    for (auto& p : panels)
        if (p.id == id)
            return &p;
    if (panelId.empty() || panelId == activePanelId)
        return panels.empty() ? nullptr : &panels.front();
    return nullptr;
}

ControlPanelLayout ControlPanelLayout::createDefaultMotorPanel()
{
    ControlPanelLayout p;
    p.title = "Closed-Loop Motor Control & Automation Panel";
    p.ensureDefaultPanel();

    // --- Left Column: Inputs ---
    // 1. Main Power Switch
    p.widgets.push_back({
        "pwr_sw", WidgetType::ToggleSwitch, "Main Power",
        "power_switch", "", 0.0, 1.0, 0.0, 1.0, 0, 0, 50, {}
    });

    // 2. Start Button
    p.widgets.push_back({
        "btn_start", WidgetType::PushButton, "Start Motor",
        "start_button", "", 0.0, 1.0, 0.0, 1.0, 0, 1, 45, {}
    });

    // 3. Stop Button
    p.widgets.push_back({
        "btn_stop", WidgetType::PushButton, "Stop Motor",
        "stop_button", "", 0.0, 1.0, 0.0, 1.0, 0, 2, 45, {}
    });

    // 4. Speed Setpoint Knob
    p.widgets.push_back({
        "knob_setpoint", WidgetType::Knob, "Speed Setpoint",
        "speed_setpoint", "rad/s", 0.0, 600.0, 300.0, 5.0, 0, 3, 100, {}
    });

    // 5. Mechanical Load Slider
    p.widgets.push_back({
        "slider_load", WidgetType::Slider, "Mechanical Load",
        "load_torque", "N*m", 0.0, 0.05, 0.0, 0.001, 0, 4, 80, {}
    });

    // 6. Mode Selector (Auto / Manual)
    p.widgets.push_back({
        "mode_sel", WidgetType::ModeSelector, "Control Mode",
        "mode_select", "", 0.0, 1.0, 0.0, 1.0, 0, 5, 50, { "Manual", "Automatic" }
    });

    // 7. Emergency Stop (Big Red E-Stop)
    p.widgets.push_back({
        "estop_btn", WidgetType::EmergencyStop, "EMERGENCY STOP",
        "estop_button", "", 0.0, 1.0, 0.0, 1.0, 0, 6, 80, {}
    });

    // 8. Fault Reset
    p.widgets.push_back({
        "btn_reset", WidgetType::PushButton, "Fault Reset",
        "reset_button", "", 0.0, 1.0, 0.0, 1.0, 0, 7, 45, {}
    });

    // --- Right Column: Outputs ---
    // 1. Clock Display
    p.widgets.push_back({
        "disp_clock", WidgetType::ClockDisplay, "Simulation Time",
        "", "s", 0.0, 0.0, 0.0, 0.0, 1, 0, 40, {}
    });

    // 2. Analog Speed Gauge
    p.widgets.push_back({
        "gauge_speed", WidgetType::AnalogGauge, "Motor Speed (Dial)",
        "motor.speed", "rad/s", 0.0, 600.0, 0.0, 1.0, 1, 1, 140, {}
    });

    // 3. Digital Readouts (Speed & Angle)
    p.widgets.push_back({
        "num_speed", WidgetType::DigitalReadout, "Actual Speed",
        "motor.speed", "rad/s", 0.0, 600.0, 0.0, 0.1, 1, 2, 45, {}
    });

    p.widgets.push_back({
        "num_angle", WidgetType::DigitalReadout, "Shaft Position",
        "motor.angle", "rad", -1000.0, 1000.0, 0.0, 0.01, 1, 3, 45, {}
    });

    p.widgets.push_back({
        "num_current", WidgetType::DigitalReadout, "Armature Current",
        "motor.current", "A", -15.0, 15.0, 0.0, 0.01, 1, 4, 45, {}
    });

    // 4. Indicator Lamps
    p.widgets.push_back({
        "lamp_running", WidgetType::IndicatorLamp, "Motor Running",
        "running", "", 0.0, 1.0, 0.0, 1.0, 1, 5, 35, {}
    });

    p.widgets.push_back({
        "lamp_fault", WidgetType::IndicatorLamp, "Fault / E-Stop",
        "fault", "", 0.0, 1.0, 0.0, 1.0, 1, 6, 35, {}
    });

    // 5. Scrolling Trend Chart
    p.widgets.push_back({
        "trend_speed", WidgetType::TrendChart, "Speed Tracking (Target vs Actual)",
        "motor.speed", "rad/s", 0.0, 600.0, 0.0, 1.0, 1, 7, 160, {}
    });

    // 6. Event Log
    p.widgets.push_back({
        "log_events", WidgetType::EventLog, "Live Event Log",
        "", "", 0.0, 0.0, 0.0, 0.0, 1, 8, 120, {}
    });

    p.ensureDefaultPanel();
    return p;
}

ControlPanelLayout ControlPanelLayout::createDualMotorCoordinatedPanel()
{
    ControlPanelLayout p;
    p.title = "Dual-Motor Coordinated System Control Panel";
    p.ensureDefaultPanel();

    // --- Left Column: Inputs ---
    // 1. System Master Power
    p.widgets.push_back({
        "pwr_sw", WidgetType::ToggleSwitch, "Master System Power",
        "system.power", "", 0.0, 1.0, 0.0, 1.0, 0, 0, 50, {}
    });

    // 2. Coordinated System Start Button
    p.widgets.push_back({
        "btn_start", WidgetType::PushButton, "System Start (Coordinated)",
        "system.start", "", 0.0, 1.0, 0.0, 1.0, 0, 1, 45, {}
    });

    // 3. System Stop Button
    p.widgets.push_back({
        "btn_stop", WidgetType::PushButton, "System Stop All",
        "system.stop", "", 0.0, 1.0, 0.0, 1.0, 0, 2, 45, {}
    });

    // 4. Motor A Speed Setpoint Knob
    p.widgets.push_back({
        "knob_setpoint_a", WidgetType::Knob, "Motor A Setpoint",
        "motorA.speed_setpoint", "rad/s", 0.0, 600.0, 300.0, 5.0, 0, 3, 90, {}
    });

    // 5. Motor A Mechanical Load Slider
    p.widgets.push_back({
        "slider_load_a", WidgetType::Slider, "Motor A Load Torque",
        "motorA.load_torque", "N*m", 0.0, 0.05, 0.0, 0.001, 0, 4, 75, {}
    });

    // 6. Motor B Speed Setpoint Knob
    p.widgets.push_back({
        "knob_setpoint_b", WidgetType::Knob, "Motor B Setpoint",
        "motorB.speed_setpoint", "rad/s", 0.0, 600.0, 200.0, 5.0, 0, 5, 90, {}
    });

    // 7. Motor B Mechanical Load Slider
    p.widgets.push_back({
        "slider_load_b", WidgetType::Slider, "Motor B Load Torque",
        "motorB.load_torque", "N*m", 0.0, 0.05, 0.0, 0.001, 0, 6, 75, {}
    });

    // 8. Emergency Stop (Big Red Mushroom)
    p.widgets.push_back({
        "estop_btn", WidgetType::EmergencyStop, "MASTER EMERGENCY STOP",
        "system.estop", "", 0.0, 1.0, 0.0, 1.0, 0, 7, 80, {}
    });

    // 9. Master Fault Reset Button
    p.widgets.push_back({
        "btn_reset", WidgetType::PushButton, "Fault Reset",
        "system.reset", "", 0.0, 1.0, 0.0, 1.0, 0, 8, 45, {}
    });

    // --- Right Column: Outputs ---
    // 1. Simulation Time Clock
    p.widgets.push_back({
        "disp_clock", WidgetType::ClockDisplay, "Simulation Time",
        "", "s", 0.0, 0.0, 0.0, 0.0, 1, 0, 40, {}
    });

    // 2. Analog Speed Gauges
    p.widgets.push_back({
        "gauge_speed_a", WidgetType::AnalogGauge, "Motor A Speed (Dial)",
        "motorA.speed", "rad/s", 0.0, 600.0, 0.0, 1.0, 1, 1, 130, {}
    });

    p.widgets.push_back({
        "gauge_speed_b", WidgetType::AnalogGauge, "Motor B Speed (Dial)",
        "motorB.speed", "rad/s", 0.0, 600.0, 0.0, 1.0, 1, 2, 130, {}
    });

    // 3. Digital Readouts (A & B Speeds and Currents)
    p.widgets.push_back({
        "num_speed_a", WidgetType::DigitalReadout, "Motor A Speed",
        "motorA.speed", "rad/s", 0.0, 600.0, 0.0, 0.1, 1, 3, 40, {}
    });

    p.widgets.push_back({
        "num_current_a", WidgetType::DigitalReadout, "Motor A Current",
        "motorA.current", "A", -15.0, 15.0, 0.0, 0.01, 1, 4, 40, {}
    });

    p.widgets.push_back({
        "num_speed_b", WidgetType::DigitalReadout, "Motor B Speed",
        "motorB.speed", "rad/s", 0.0, 600.0, 0.0, 0.1, 1, 5, 40, {}
    });

    p.widgets.push_back({
        "num_current_b", WidgetType::DigitalReadout, "Motor B Current",
        "motorB.current", "A", -15.0, 15.0, 0.0, 0.01, 1, 6, 40, {}
    });

    // 4. Status Indicator Lamps
    p.widgets.push_back({
        "lamp_ready", WidgetType::IndicatorLamp, "System Ready",
        "system.ready", "", 0.0, 1.0, 0.0, 1.0, 1, 7, 32, {}
    });

    p.widgets.push_back({
        "lamp_running_a", WidgetType::IndicatorLamp, "Motor A Running",
        "controllerA.running", "", 0.0, 1.0, 0.0, 1.0, 1, 8, 32, {}
    });

    p.widgets.push_back({
        "lamp_running_b", WidgetType::IndicatorLamp, "Motor B Running",
        "controllerB.running", "", 0.0, 1.0, 0.0, 1.0, 1, 9, 32, {}
    });

    p.widgets.push_back({
        "lamp_fault", WidgetType::IndicatorLamp, "Fault / E-Stop Tripped",
        "system.fault", "", 0.0, 1.0, 0.0, 1.0, 1, 10, 32, {}
    });

    // 5. Dual-Trace Trend Chart
    p.widgets.push_back({
        "trend_speed", WidgetType::TrendChart, "Speed Tracking (Motor A vs Motor B)",
        "motorA.speed", "rad/s", 0.0, 600.0, 0.0, 1.0, 1, 11, 160, {}
    });

    // 6. Live Event Log
    p.widgets.push_back({
        "log_events", WidgetType::EventLog, "Live Event Log",
        "", "", 0.0, 0.0, 0.0, 0.0, 1, 12, 120, {}
    });

    p.ensureDefaultPanel();
    return p;
}

juce::var ControlPanelLayout::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("title", juce::String(title));
    obj->setProperty("activePanelId", juce::String(activePanelId));

    juce::Array<juce::var> panelArray;
    auto copy = *this;
    copy.ensureDefaultPanel();
    for (const auto& panel : copy.panels)
        panelArray.add(panel.toJson());
    obj->setProperty("panels", panelArray);

    juce::Array<juce::var> wArray;
    for (const auto& w : copy.widgets)
    {
        auto* wo = new juce::DynamicObject();
        wo->setProperty("id", juce::String(w.id));
        wo->setProperty("type", (int)w.type);
        wo->setProperty("title", juce::String(w.title));
        wo->setProperty("signalName", juce::String(w.signalName));
        wo->setProperty("unit", juce::String(w.unit));
        wo->setProperty("minValue", w.minValue);
        wo->setProperty("maxValue", w.maxValue);
        wo->setProperty("defaultValue", w.defaultValue);
        wo->setProperty("stepSize", w.stepSize);
        wo->setProperty("column", w.column);
        wo->setProperty("order", w.order);
        wo->setProperty("height", w.height);

        juce::Array<juce::var> optArr;
        for (const auto& opt : w.options)
            optArr.add(juce::String(opt));
        wo->setProperty("options", optArr);

        wo->setProperty("panelId", juce::String(w.panelId));
        wo->setProperty("plotMode", juce::String(w.plotMode));
        wo->setProperty("xChannel", w.xChannel);
        wo->setProperty("yChannel", w.yChannel);
        wo->setProperty("visibleHistorySeconds", w.visibleHistorySeconds);
        wo->setProperty("maxSamples", w.maxSamples);
        wo->setProperty("autoScroll", w.autoScroll);

        juce::Array<juce::var> channelArray;
        for (const auto& channel : w.plotChannels)
            channelArray.add(channel.toJson());
        wo->setProperty("plotChannels", channelArray);

        wArray.add(juce::var(wo));
    }
    obj->setProperty("widgets", wArray);

    return juce::var(obj);
}

ControlPanelLayout ControlPanelLayout::fromJson(const juce::var& json)
{
    ControlPanelLayout p;
    if (!json.isObject())
        return p;

    p.title = json["title"].toString().toStdString();
    p.activePanelId = json["activePanelId"].toString().toStdString();
    if (const auto* panelArr = json["panels"].getArray())
        for (const auto& panelVar : *panelArr)
            p.panels.push_back(OperatorPanelConfig::fromJson(panelVar));

    if (const auto* arr = json["widgets"].getArray())
    {
        for (const auto& wVar : *arr)
        {
            if (!wVar.isObject()) continue;
            const auto* wo = wVar.getDynamicObject();
            WidgetConfig w;
            w.id = wVar["id"].toString().toStdString();
            w.type = (WidgetType)(int)wVar["type"];
            w.title = wVar["title"].toString().toStdString();
            w.signalName = wVar["signalName"].toString().toStdString();
            w.unit = wVar["unit"].toString().toStdString();
            w.minValue = (double)wVar["minValue"];
            w.maxValue = (double)wVar["maxValue"];
            w.defaultValue = (double)wVar["defaultValue"];
            w.stepSize = (double)wVar["stepSize"];
            w.column = (int)wVar["column"];
            w.order = (int)wVar["order"];
            w.height = (int)wVar["height"];
            if (const auto* optArr = wVar["options"].getArray())
            {
                for (const auto& o : *optArr)
                    w.options.push_back(o.toString().toStdString());
            }
            w.panelId = wVar["panelId"].toString().toStdString();
            if (wo != nullptr && wo->hasProperty("plotMode"))
                w.plotMode = wVar["plotMode"].toString().toStdString();
            if (wo != nullptr && wo->hasProperty("xChannel"))
                w.xChannel = (int)wVar["xChannel"];
            if (wo != nullptr && wo->hasProperty("yChannel"))
                w.yChannel = (int)wVar["yChannel"];
            if (wo != nullptr && wo->hasProperty("visibleHistorySeconds"))
                w.visibleHistorySeconds = (double)wVar["visibleHistorySeconds"];
            if (wo != nullptr && wo->hasProperty("maxSamples"))
                w.maxSamples = (int)wVar["maxSamples"];
            if (wo != nullptr && wo->hasProperty("autoScroll"))
                w.autoScroll = (bool)wVar["autoScroll"];
            if (const auto* channelArr = wVar["plotChannels"].getArray())
                for (const auto& channelVar : *channelArr)
                    w.plotChannels.push_back(PlotChannelConfig::fromJson(channelVar));
            p.widgets.push_back(w);
        }
    }
    p.ensureDefaultPanel();
    return p;
}

} // namespace sim_panel
