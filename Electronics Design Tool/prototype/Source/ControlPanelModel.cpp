#include "ControlPanelModel.h"

namespace sim_panel
{

ControlPanelLayout ControlPanelLayout::createDefaultMotorPanel()
{
    ControlPanelLayout p;
    p.title = "Closed-Loop Motor Control & Automation Panel";

    // --- Left Column: Inputs ---
    // 1. Main Power Switch
    p.widgets.push_back({
        "pwr_sw", WidgetType::ToggleSwitch, "Main Power",
        "power_switch", "", 0.0, 1.0, 1.0, 1.0, 0, 0, 50, {}
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

    return p;
}

ControlPanelLayout ControlPanelLayout::createDualMotorCoordinatedPanel()
{
    ControlPanelLayout p;
    p.title = "Dual-Motor Coordinated System Control Panel";

    // --- Left Column: Inputs ---
    // 1. System Master Power
    p.widgets.push_back({
        "pwr_sw", WidgetType::ToggleSwitch, "Master System Power",
        "system.power", "", 0.0, 1.0, 1.0, 1.0, 0, 0, 50, {}
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

    return p;
}

juce::var ControlPanelLayout::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("title", juce::String(title));

    juce::Array<juce::var> wArray;
    for (const auto& w : widgets)
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
    if (const auto* arr = json["widgets"].getArray())
    {
        for (const auto& wVar : *arr)
        {
            if (!wVar.isObject()) continue;
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
            p.widgets.push_back(w);
        }
    }
    return p;
}

} // namespace sim_panel
