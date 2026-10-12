#include "ControlPanelComponent.h"
#include "MachineModel.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace sim_panel
{

namespace
{

// Custom Analog Gauge Component
class AnalogGaugeComponent final : public juce::Component
{
public:
    AnalogGaugeComponent(const std::string& title, const std::string& unit, double minVal, double maxVal)
        : title_(title), unit_(unit), minVal_(minVal), maxVal_(maxVal) {}

    void setValue(double val)
    {
        val_ = std::clamp(val, minVal_, maxVal_);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(4.0f);
        const float size = std::min(bounds.getWidth(), bounds.getHeight());
        const auto center = bounds.getCentre();
        const float radius = size * 0.45f;

        // Dial face background
        g.setColour(juce::Colour(0xff181d24));
        g.fillEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(juce::Colour(0xff3a4454));
        g.drawEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f, 2.0f);

        // Dial Arc (-135 deg to +135 deg)
        const float startAngle = -2.35619f; // -135 deg
        const float endAngle = 2.35619f;    // +135 deg
        const float totalAngle = endAngle - startAngle;

        juce::Path arc;
        arc.addCentredArc(center.x, center.y, radius * 0.85f, radius * 0.85f, 0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colour(0xff2a3442));
        g.strokePath(arc, juce::PathStrokeType(4.0f));

        // Graduation tick marks
        const int numTicks = 10;
        for (int i = 0; i <= numTicks; ++i)
        {
            const float frac = (float)i / (float)numTicks;
            const float angle = startAngle + frac * totalAngle;
            const float cosA = std::cos(angle - juce::MathConstants<float>::halfPi);
            const float sinA = std::sin(angle - juce::MathConstants<float>::halfPi);

            const float r1 = radius * 0.85f;
            const float r2 = radius * (i % 2 == 0 ? 0.70f : 0.77f);
            g.setColour(i % 2 == 0 ? juce::Colour(0xffc5d1de) : juce::Colour(0xff6c7d93));
            g.drawLine(center.x + cosA * r1, center.y + sinA * r1,
                       center.x + cosA * r2, center.y + sinA * r2, i % 2 == 0 ? 2.0f : 1.2f);
        }

        // Title and Unit
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xff8b9bb0));
        g.drawText(juce::String(title_), center.x - 50.0f, center.y - radius * 0.45f, 100.0f, 16.0f, juce::Justification::centred);
        g.setFont(juce::Font(10.0f));
        g.drawText(juce::String(unit_), center.x - 50.0f, center.y + radius * 0.25f, 100.0f, 16.0f, juce::Justification::centred);

        // Needle
        const float norm = (float)((val_ - minVal_) / (maxVal_ - minVal_));
        const float needleAngle = startAngle + norm * totalAngle - juce::MathConstants<float>::halfPi;
        const float nCos = std::cos(needleAngle);
        const float nSin = std::sin(needleAngle);

        juce::Path needle;
        needle.startNewSubPath(center.x - nSin * 3.0f, center.y + nCos * 3.0f);
        needle.lineTo(center.x + nCos * (radius * 0.82f), center.y + nSin * (radius * 0.82f));
        needle.lineTo(center.x + nSin * 3.0f, center.y - nCos * 3.0f);
        needle.closeSubPath();

        g.setColour(juce::Colour(0xffff5555));
        g.fillPath(needle);

        // Center boss cap
        g.setColour(juce::Colour(0xffe0e6ed));
        g.fillEllipse(center.x - 5.0f, center.y - 5.0f, 10.0f, 10.0f);
    }

private:
    std::string title_, unit_;
    double minVal_ = 0.0, maxVal_ = 100.0, val_ = 0.0;
};

// Custom Indicator Lamp
class IndicatorLampComponent final : public juce::Component
{
public:
    IndicatorLampComponent(const std::string& label, juce::Colour onColor)
        : label_(label), onColor_(onColor) {}

    void setActive(bool active)
    {
        active_ = active;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        const float lampSize = 18.0f;
        const auto lampRect = juce::Rectangle<float>(12.0f, (bounds.getHeight() - lampSize) * 0.5f, lampSize, lampSize);

        if (active_)
        {
            // Glow
            g.setColour(onColor_.withAlpha(0.35f));
            g.fillEllipse(lampRect.expanded(4.0f));
            g.setColour(onColor_);
            g.fillEllipse(lampRect);
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.fillEllipse(lampRect.getX() + 3.0f, lampRect.getY() + 3.0f, 5.0f, 5.0f);
        }
        else
        {
            g.setColour(juce::Colour(0xff222933));
            g.fillEllipse(lampRect);
            g.setColour(juce::Colour(0xff3a4659));
            g.drawEllipse(lampRect, 1.5f);
        }

        g.setColour(active_ ? juce::Colour(0xffffffff) : juce::Colour(0xff8a9ba8));
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(juce::String(label_), 40, 0, (int)bounds.getWidth() - 45, (int)bounds.getHeight(), juce::Justification::centredLeft);
    }

private:
    std::string label_;
    juce::Colour onColor_;
    bool active_ = false;
};

// Toggle switch drawn as a real operator switch rather than JUCE's checkbox.
class ToggleSwitchComponent final : public juce::ToggleButton
{
public:
    explicit ToggleSwitchComponent(const std::string& label)
        : juce::ToggleButton(juce::String(label))
    {
        setClickingTogglesState(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto b = getLocalBounds().toFloat().reduced(4.0f);
        const float trackW = 58.0f;
        const float trackH = 28.0f;
        const auto track = juce::Rectangle<float>(b.getX(), b.getCentreY() - trackH * 0.5f, trackW, trackH);
        const bool on = getToggleState();

        auto trackColour = on ? juce::Colour(0xff2fcb72) : juce::Colour(0xff343d4a);
        if (shouldDrawButtonAsDown)
            trackColour = trackColour.darker(0.25f);
        else if (shouldDrawButtonAsHighlighted)
            trackColour = trackColour.brighter(0.15f);

        g.setColour(trackColour);
        g.fillRoundedRectangle(track, trackH * 0.5f);
        g.setColour(juce::Colour(0xff0d1118));
        g.drawRoundedRectangle(track, trackH * 0.5f, 1.2f);

        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.setColour(on ? juce::Colour(0xff052411) : juce::Colour(0xffaeb8c5));
        g.drawText(on ? "ON" : "OFF", track.reduced(8.0f, 0.0f).toNearestInt(), juce::Justification::centred);

        const float knobSize = trackH - 6.0f;
        const float knobX = on ? track.getRight() - knobSize - 3.0f : track.getX() + 3.0f;
        const auto knob = juce::Rectangle<float>(knobX, track.getCentreY() - knobSize * 0.5f, knobSize, knobSize);
        g.setColour(juce::Colour(0xffeef4f8));
        g.fillEllipse(knob);
        g.setColour(juce::Colour(0xff677282));
        g.drawEllipse(knob, 1.0f);

        g.setColour(juce::Colour(0xffc5d1de));
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(getButtonText(), (int)(track.getRight() + 12.0f), 0,
                   getWidth() - (int)(track.getRight() + 16.0f), getHeight(),
                   juce::Justification::centredLeft);
    }
};

// Custom Emergency Stop Button
class EmergencyStopComponent final : public juce::Component
{
public:
    std::function<void(bool)> onStateChanged;

    EmergencyStopComponent()
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    bool isLatched() const { return latched_; }

    void setLatched(bool latched)
    {
        latched_ = latched;
        repaint();
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        latched_ = !latched_;
        repaint();
        if (onStateChanged)
            onStateChanged(latched_);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(4.0f);
        const float size = std::min(bounds.getWidth(), bounds.getHeight());
        const auto center = bounds.getCentre();

        // Yellow background caution ring
        g.setColour(juce::Colour(0xfff4c430));
        g.fillEllipse(center.x - size * 0.48f, center.y - size * 0.48f, size * 0.96f, size * 0.96f);
        g.setColour(juce::Colours::black);
        g.drawEllipse(center.x - size * 0.48f, center.y - size * 0.48f, size * 0.96f, size * 0.96f, 2.0f);

        // Big Red Mushroom Head
        const float mushroomRadius = size * (latched_ ? 0.36f : 0.40f);
        g.setColour(latched_ ? juce::Colour(0xffb30000) : juce::Colour(0xffee1111));
        g.fillEllipse(center.x - mushroomRadius, center.y - mushroomRadius, mushroomRadius * 2.0f, mushroomRadius * 2.0f);
        g.setColour(juce::Colour(0xff550000));
        g.drawEllipse(center.x - mushroomRadius, center.y - mushroomRadius, mushroomRadius * 2.0f, mushroomRadius * 2.0f, 2.5f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText(latched_ ? "TRIPPED" : "E-STOP", center.x - 30.0f, center.y - 8.0f, 60.0f, 16.0f, juce::Justification::centred);
    }

private:
    bool latched_ = false;
};

// Custom Trend Chart Component
class TrendChartComponent final : public juce::Component
{
public:
    TrendChartComponent() {}

    void updateData(const std::deque<ControlPanelComponent::TrendPoint>& history)
    {
        history_ = history;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(juce::Colour(0xff12161c));
        g.fillRect(b);
        g.setColour(juce::Colour(0xff2a3442));
        g.drawRect(b, 1.0f);

        // Grid
        g.setColour(juce::Colour(0xff1c2430));
        for (int i = 1; i < 4; ++i)
        {
            float y = b.getY() + b.getHeight() * (float)i / 4.0f;
            g.drawHorizontalLine((int)y, b.getX(), b.getRight());
        }

        // Header / Legend
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xff00d4ff));
        g.drawText("- Target (rad/s)", 10, 4, 110, 16, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffffbb00));
        g.drawText("- Actual (rad/s)", 125, 4, 110, 16, juce::Justification::centredLeft);

        if (history_.size() < 2) return;

        const double tMin = history_.front().time;
        const double tMax = std::max(tMin + 1e-3, history_.back().time);
        const double vMax = 650.0;

        auto toPoint = [&](double t, double v) -> juce::Point<float> {
            float x = b.getX() + (float)((t - tMin) / (tMax - tMin)) * b.getWidth();
            float y = b.getBottom() - (float)(std::clamp(v, 0.0, vMax) / vMax) * (b.getHeight() - 24.0f) - 4.0f;
            return { x, y };
        };

        juce::Path targetPath, actualPath;
        targetPath.startNewSubPath(toPoint(history_[0].time, history_[0].target));
        actualPath.startNewSubPath(toPoint(history_[0].time, history_[0].actual));

        for (size_t i = 1; i < history_.size(); ++i)
        {
            targetPath.lineTo(toPoint(history_[i].time, history_[i].target));
            actualPath.lineTo(toPoint(history_[i].time, history_[i].actual));
        }

        g.setColour(juce::Colour(0xff00d4ff).withAlpha(0.85f));
        g.strokePath(targetPath, juce::PathStrokeType(1.5f));

        g.setColour(juce::Colour(0xffffbb00));
        g.strokePath(actualPath, juce::PathStrokeType(1.8f));
    }

private:
    std::deque<ControlPanelComponent::TrendPoint> history_;
};

// Digital Readout
class DigitalReadoutComponent final : public juce::Component
{
public:
    DigitalReadoutComponent(const std::string& label, const std::string& unit)
        : label_(label), unit_(unit) {}

    void setText(const std::string& valText)
    {
        valText_ = valText;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff14181f));
        g.fillRoundedRectangle(b, 4.0f);
        g.setColour(juce::Colour(0xff2b3747));
        g.drawRoundedRectangle(b, 4.0f, 1.2f);

        g.setFont(juce::Font(11.0f));
        g.setColour(juce::Colour(0xff8294a6));
        g.drawText(juce::String(label_), 8, 4, 150, 16, juce::Justification::centredLeft);

        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xff00ffcc));
        g.drawText(juce::String(valText_) + " " + juce::String(unit_), 8, 16, (int)b.getWidth() - 16, (int)b.getHeight() - 20, juce::Justification::centredRight);
    }

private:
    std::string label_, unit_, valText_ = "0.0";
};

// Composite Labeled Slider & Knob Widget with Clear Title & Engineering Unit
class LabeledSliderComponent final : public juce::Component
{
public:
    LabeledSliderComponent(const std::string& title, const std::string& unit, bool isKnob)
    {
        juce::String labelText = juce::String(title);
        if (!unit.empty())
            labelText << " (" << juce::String(unit) << ")";

        addAndMakeVisible(label);
        label.setFont(juce::Font(12.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffc5d1de));
        label.setText(labelText, juce::dontSendNotification);

        addAndMakeVisible(slider);
        slider.setSliderStyle(isKnob ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 20);
        if (!unit.empty())
            slider.setTextValueSuffix(" " + juce::String(unit));
    }

    void resized() override
    {
        auto b = getLocalBounds();
        label.setBounds(b.removeFromTop(18));
        slider.setBounds(b);
    }

    juce::Label label;
    juce::Slider slider;
};

} // namespace

ControlPanelComponent::ControlPanelComponent()
    : ControlPanelComponent(std::make_shared<SystemSimulationSession>())
{
}

ControlPanelComponent::ControlPanelComponent(std::shared_ptr<SystemSimulationSession> session)
    : session_(session)
{
    // The active machine model must be loaded explicitly via loadMachine().
    // Never silently substitute setupDefaultMotorSystem().
    // Setup Transport Buttons
    auto setupBtn = [this](juce::TextButton& btn, juce::Colour col) {
        addAndMakeVisible(btn);
        btn.setColour(juce::TextButton::buttonColourId, col);
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    };

    setupBtn(runButton, juce::Colour(0xff2d7d46));
    setupBtn(pauseButton, juce::Colour(0xff8a6d1b));
    setupBtn(stepButton, juce::Colour(0xff255c8a));
    setupBtn(stopButton, juce::Colour(0xff8a2828));
    setupBtn(resetButton, juce::Colour(0xff445261));

    runButton.onClick = [this] { if (session_) session_->start(); };
    pauseButton.onClick = [this] { if (session_) session_->pause(); };
    stepButton.onClick = [this] { if (session_) session_->stepSingle(0.005); };
    stopButton.onClick = [this] { if (session_) session_->stop(); };
    resetButton.onClick = [this] { if (session_) session_->reset(); };

    addAndMakeVisible(rateBox);
    rateBox.addItem("0.1x (Slow-mo)", 1);
    rateBox.addItem("0.5x", 2);
    rateBox.addItem("1.0x (Real-time)", 3);
    rateBox.addItem("2.0x (Fast)", 4);
    rateBox.addItem("5.0x (Hyper)", 5);
    rateBox.addItem("Max (Free Run)", 6);
    rateBox.setSelectedId(3, juce::dontSendNotification);
    rateBox.onChange = [this] {
        if (!session_) return;
        const int id = rateBox.getSelectedId();
        if (id == 1) session_->setExecutionRate(0.1);
        else if (id == 2) session_->setExecutionRate(0.5);
        else if (id == 3) session_->setExecutionRate(1.0);
        else if (id == 4) session_->setExecutionRate(2.0);
        else if (id == 5) session_->setExecutionRate(5.0);
        else if (id == 6) session_->setExecutionRate(0.0);
    };

    addAndMakeVisible(statusLabel);
    statusLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff66ff99));
    statusLabel.setText("STOPPED", juce::dontSendNotification);

    addAndMakeVisible(inputViewport);
    addAndMakeVisible(outputViewport);

    ControlPanelLayout blank;
    blank.title = "No Active Machine";
    setLayout(blank);
    startTimerHz(30); // 30 Hz UI refresh
}

ControlPanelComponent::~ControlPanelComponent()
{
    stopTimer();
}

bool ControlPanelComponent::loadMachine(const machine_builder::MachineDesign& machine, std::string* error)
{
    if (session_)
    {
        if (!machine.compileAndLoadIntoSession(*session_, error))
            return false;
    }
    const auto rate = machine.simConfig.defaultRate;
    const int rateId = rate <= 0.0 ? 6
                     : rate <= 0.11 ? 1
                     : rate <= 0.55 ? 2
                     : rate <= 1.1 ? 3
                     : rate <= 2.1 ? 4
                     : 5;
    rateBox.setSelectedId(rateId, juce::dontSendNotification);
    setLayout(machine.controlPanel);
    statusLabel.setText("STOPPED", juce::dontSendNotification);
    return true;
}

void ControlPanelComponent::setLayout(const ControlPanelLayout& layout)
{
    layout_ = layout;
    buildWidgets();
    resized();
}

void ControlPanelComponent::buildWidgets()
{
    inputViews_.clear();
    outputViews_.clear();

    inputContainer = std::make_unique<juce::Component>();
    outputContainer = std::make_unique<juce::Component>();

    for (const auto& w : layout_.widgets)
    {
        if (w.column == 0) // Inputs
        {
            if (w.type == WidgetType::PushButton)
            {
                auto btn = std::make_unique<juce::TextButton>(juce::String(w.title));
                btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d4257));
                const auto sig = w.signalName;
                btn->onStateChange = [this, btnPtr = btn.get(), sig] {
                    if (session_)
                        session_->setBooleanInput(sig, btnPtr->isDown());
                };
                inputContainer->addAndMakeVisible(*btn);
                inputViews_.push_back({ w, std::move(btn) });
            }
            else if (w.type == WidgetType::ToggleSwitch)
            {
                auto tog = std::make_unique<ToggleSwitchComponent>(w.title);
                tog->setToggleState(w.defaultValue > 0.5, juce::dontSendNotification);
                const auto sig = w.signalName;
                if (session_)
                    session_->setBooleanInput(sig, tog->getToggleState());
                tog->onClick = [this, togPtr = tog.get(), sig] {
                    if (session_)
                        session_->setBooleanInput(sig, togPtr->getToggleState());
                };
                inputContainer->addAndMakeVisible(*tog);
                inputViews_.push_back({ w, std::move(tog) });
            }
            else if (w.type == WidgetType::Knob || w.type == WidgetType::Slider)
            {
                auto ctrl = std::make_unique<LabeledSliderComponent>(w.title, w.unit, w.type == WidgetType::Knob);
                ctrl->slider.setRange(w.minValue, w.maxValue, w.stepSize);
                ctrl->slider.setValue(w.defaultValue, juce::dontSendNotification);
                const auto sig = w.signalName;
                if (session_)
                    session_->setNumericInput(sig, ctrl->slider.getValue());
                ctrl->slider.onValueChange = [this, sldPtr = &ctrl->slider, sig] {
                    if (session_)
                        session_->setNumericInput(sig, sldPtr->getValue());
                };
                inputContainer->addAndMakeVisible(*ctrl);
                inputViews_.push_back({ w, std::move(ctrl) });
            }
            else if (w.type == WidgetType::EmergencyStop)
            {
                auto estop = std::make_unique<EmergencyStopComponent>();
                const auto sig = w.signalName;
                if (session_)
                    session_->setBooleanInput(sig, false);
                estop->onStateChanged = [this, sig](bool latched) {
                    if (session_)
                        session_->setBooleanInput(sig, latched);
                };
                inputContainer->addAndMakeVisible(*estop);
                inputViews_.push_back({ w, std::move(estop) });
            }
            else if (w.type == WidgetType::ModeSelector)
            {
                auto cb = std::make_unique<juce::ComboBox>();
                for (size_t i = 0; i < w.options.size(); ++i)
                    cb->addItem(juce::String(w.options[i]), (int)i + 1);
                cb->setSelectedId(1, juce::dontSendNotification);
                const auto sig = w.signalName;
                if (session_)
                    session_->setNumericInput(sig, 0.0);
                cb->onChange = [this, cbPtr = cb.get(), sig] {
                    if (session_)
                        session_->setNumericInput(sig, (double)(cbPtr->getSelectedId() - 1));
                };
                inputContainer->addAndMakeVisible(*cb);
                inputViews_.push_back({ w, std::move(cb) });
            }
        }
        else // Outputs
        {
            if (w.type == WidgetType::AnalogGauge)
            {
                auto gauge = std::make_unique<AnalogGaugeComponent>(w.title, w.unit, w.minValue, w.maxValue);
                outputContainer->addAndMakeVisible(*gauge);
                outputViews_.push_back({ w, std::move(gauge) });
            }
            else if (w.type == WidgetType::DigitalReadout || w.type == WidgetType::ClockDisplay)
            {
                auto num = std::make_unique<DigitalReadoutComponent>(w.title, w.unit);
                outputContainer->addAndMakeVisible(*num);
                outputViews_.push_back({ w, std::move(num) });
            }
            else if (w.type == WidgetType::IndicatorLamp)
            {
                juce::Colour c = w.id.find("fault") != std::string::npos ? juce::Colour(0xffff3333) : juce::Colour(0xff33dd55);
                auto lamp = std::make_unique<IndicatorLampComponent>(w.title, c);
                outputContainer->addAndMakeVisible(*lamp);
                outputViews_.push_back({ w, std::move(lamp) });
            }
            else if (w.type == WidgetType::TrendChart)
            {
                auto chart = std::make_unique<TrendChartComponent>();
                outputContainer->addAndMakeVisible(*chart);
                outputViews_.push_back({ w, std::move(chart) });
            }
        }
    }

    inputViewport.setViewedComponent(inputContainer.get(), false);
    outputViewport.setViewedComponent(outputContainer.get(), false);
}

void ControlPanelComponent::resized()
{
    auto b = getLocalBounds().reduced(8);

    // Top Transport Bar
    auto topBar = b.removeFromTop(36);
    runButton.setBounds(topBar.removeFromLeft(70).reduced(2));
    pauseButton.setBounds(topBar.removeFromLeft(70).reduced(2));
    stepButton.setBounds(topBar.removeFromLeft(70).reduced(2));
    stopButton.setBounds(topBar.removeFromLeft(70).reduced(2));
    resetButton.setBounds(topBar.removeFromLeft(70).reduced(2));
    topBar.removeFromLeft(12);
    rateBox.setBounds(topBar.removeFromLeft(150).reduced(2));
    topBar.removeFromLeft(12);
    statusLabel.setBounds(topBar);

    b.removeFromTop(8);

    // Two equal columns: Left (Inputs), Right (Outputs)
    const int colWidth = (b.getWidth() - 12) / 2;
    inputViewport.setBounds(b.removeFromLeft(colWidth));
    b.removeFromLeft(12);
    outputViewport.setBounds(b);

    // Layout Input Widgets vertically
    int inY = 8;
    for (auto& iv : inputViews_)
    {
        iv.component->setBounds(12, inY, colWidth - 36, iv.config.height);
        inY += iv.config.height + 10;
    }
    inputContainer->setBounds(0, 0, colWidth - 10, std::max(inY, inputViewport.getHeight()));

    // Layout Output Widgets vertically
    int outY = 8;
    for (auto& ov : outputViews_)
    {
        ov.component->setBounds(12, outY, colWidth - 36, ov.config.height);
        outY += ov.config.height + 10;
    }
    outputContainer->setBounds(0, 0, colWidth - 10, std::max(outY, outputViewport.getHeight()));
}

void ControlPanelComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff161a22));
    g.setColour(juce::Colour(0xff2b3442));
    g.drawRect(getLocalBounds(), 1.0f);
}

void ControlPanelComponent::timerCallback()
{
    if (!session_) return;

    const auto snap = session_->getSnapshot();

    // Update status label
    if (snap.faulted)
    {
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff4444));
        statusLabel.setText("FAULT / E-STOP TRIPPED", juce::dontSendNotification);
    }
    else if (snap.running && !snap.paused)
    {
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff44ff88));
        statusLabel.setText("RUNNING", juce::dontSendNotification);
    }
    else if (snap.paused)
    {
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffcc00));
        statusLabel.setText("PAUSED", juce::dontSendNotification);
    }
    else
    {
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8899aa));
        statusLabel.setText("STOPPED", juce::dontSendNotification);
    }

    // Append to trend history
    double targetSpeed = 0.0;
    if (snap.numericSignals.count("motorA.speed"))
        targetSpeed = snap.numericSignals.at("motorA.speed");
    else if (snap.numericSignals.count("controller.setpoint"))
        targetSpeed = snap.numericSignals.at("controller.setpoint");
    else
        targetSpeed = 300.0;

    double actualSpeed = 0.0;
    if (snap.numericSignals.count("motorB.speed"))
        actualSpeed = snap.numericSignals.at("motorB.speed");
    else if (snap.numericSignals.count("motor.speed"))
        actualSpeed = snap.numericSignals.at("motor.speed");

    trendHistory_.push_back({ snap.simTime, targetSpeed, actualSpeed });
    while (trendHistory_.size() > 400 || (trendHistory_.size() > 50 && (trendHistory_.back().time - trendHistory_.front().time) > 10.0))
        trendHistory_.pop_front();

    // Update Output Views
    for (auto& ov : outputViews_)
    {
        if (ov.config.type == WidgetType::AnalogGauge)
        {
            if (auto* g = dynamic_cast<AnalogGaugeComponent*>(ov.component.get()))
            {
                if (snap.numericSignals.count(ov.config.signalName))
                    g->setValue(snap.numericSignals.at(ov.config.signalName));
            }
        }
        else if (ov.config.type == WidgetType::DigitalReadout)
        {
            if (auto* dr = dynamic_cast<DigitalReadoutComponent*>(ov.component.get()))
            {
                if (snap.numericSignals.count(ov.config.signalName))
                {
                    std::ostringstream ss;
                    ss << std::fixed << std::setprecision(ov.config.stepSize < 0.1 ? 2 : 1) << snap.numericSignals.at(ov.config.signalName);
                    dr->setText(ss.str());
                }
            }
        }
        else if (ov.config.type == WidgetType::ClockDisplay)
        {
            if (auto* dr = dynamic_cast<DigitalReadoutComponent*>(ov.component.get()))
            {
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(3) << snap.simTime << " s";
                dr->setText(ss.str());
            }
        }
        else if (ov.config.type == WidgetType::IndicatorLamp)
        {
            if (auto* lamp = dynamic_cast<IndicatorLampComponent*>(ov.component.get()))
            {
                if (snap.booleanSignals.count(ov.config.signalName))
                    lamp->setActive(snap.booleanSignals.at(ov.config.signalName));
            }
        }
        else if (ov.config.type == WidgetType::TrendChart)
        {
            if (auto* tc = dynamic_cast<TrendChartComponent*>(ov.component.get()))
            {
                tc->updateData(trendHistory_);
            }
        }
    }
}

} // namespace sim_panel
