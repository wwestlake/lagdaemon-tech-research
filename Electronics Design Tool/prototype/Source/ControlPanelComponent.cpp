#include "ControlPanelComponent.h"
#include "MachineModel.h"
#include "PlotInstrumentView.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

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
        setTooltip(juce::String(label) + " (Click to toggle)");
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto b = getLocalBounds().toFloat().reduced(2.0f);
        const float trackW = 44.0f;
        const float trackH = 20.0f;
        const auto track = juce::Rectangle<float>(b.getX() + 2.0f, b.getCentreY() - trackH * 0.5f, trackW, trackH);
        const bool on = getToggleState();

        auto trackColour = on ? juce::Colour(0xff2fcb72) : juce::Colour(0xff2d3744);
        if (shouldDrawButtonAsDown)
            trackColour = trackColour.darker(0.25f);
        else if (shouldDrawButtonAsHighlighted)
            trackColour = trackColour.brighter(0.15f);

        g.setColour(trackColour);
        g.fillRoundedRectangle(track, trackH * 0.5f);
        g.setColour(juce::Colour(0xff0d1118));
        g.drawRoundedRectangle(track, trackH * 0.5f, 1.2f);

        g.setFont(juce::Font(8.5f, juce::Font::bold));
        g.setColour(on ? juce::Colour(0xff052411) : juce::Colour(0xffaeb8c5));
        g.drawText(on ? "ON" : "OFF", track.reduced(4.0f, 0.0f).toNearestInt(), juce::Justification::centred);

        const float knobSize = trackH - 4.0f;
        const float knobX = on ? track.getRight() - knobSize - 2.0f : track.getX() + 2.0f;
        const auto knob = juce::Rectangle<float>(knobX, track.getCentreY() - knobSize * 0.5f, knobSize, knobSize);
        g.setColour(juce::Colour(0xffeef4f8));
        g.fillEllipse(knob);
        g.setColour(juce::Colour(0xff677282));
        g.drawEllipse(knob, 1.0f);

        g.setColour(juce::Colour(0xffdbe5ee));
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(getButtonText(), (int)(track.getRight() + 10.0f), 0,
                   getWidth() - (int)(track.getRight() + 14.0f), getHeight(),
                   juce::Justification::centredLeft);
    }
};

// Custom Emergency Stop Button
class EmergencyStopComponent final : public juce::Component,
                                     public juce::SettableTooltipClient
{
public:
    std::function<void(bool)> onStateChanged;

    EmergencyStopComponent()
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip("EMERGENCY STOP (Click to trip / click again to release)");
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
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);

        // Industrial safety bezel
        g.setColour(latched_ ? juce::Colour(0xff2d1215) : juce::Colour(0xff182028));
        g.fillRoundedRectangle(bounds, 4.0f);

        // Prominent caution yellow / warning border
        g.setColour(latched_ ? juce::Colour(0xffff4444) : juce::Colour(0xfff4c430));
        g.drawRoundedRectangle(bounds, 4.0f, latched_ ? 2.0f : 1.5f);

        // Circular mushroom push button on the left
        const float btnDiameter = std::min(bounds.getHeight() - 8.0f, 38.0f);
        const float btnX = bounds.getX() + 6.0f;
        const float btnY = bounds.getCentreY() - btnDiameter * 0.5f;
        const auto btnRect = juce::Rectangle<float>(btnX, btnY, btnDiameter, btnDiameter);

        // Yellow caution collar behind button
        g.setColour(juce::Colour(0xfff4c430));
        g.fillEllipse(btnRect.expanded(2.0f));
        g.setColour(juce::Colours::black);
        g.drawEllipse(btnRect.expanded(2.0f), 1.0f);

        // Mushroom head: deep crimson when latched, bright red when armed
        const auto headRect = latched_ ? btnRect.reduced(2.0f) : btnRect;
        g.setColour(latched_ ? juce::Colour(0xff990000) : juce::Colour(0xffe51c23));
        g.fillEllipse(headRect);
        g.setColour(juce::Colour(0xff550000));
        g.drawEllipse(headRect, 1.5f);

        // Highlight sheen on head
        g.setColour(juce::Colours::white.withAlpha(latched_ ? 0.2f : 0.45f));
        g.fillEllipse(headRect.getX() + 4.0f, headRect.getY() + 3.0f, headRect.getWidth() * 0.4f, headRect.getHeight() * 0.25f);

        // Centered button label inside head
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText("STOP", headRect.toNearestInt(), juce::Justification::centred);

        // Descriptive label text on the right side of bezel
        const int textX = (int)(btnRect.getRight() + 10.0f);
        const int textW = std::max(20, getWidth() - textX - 8);

        g.setColour(latched_ ? juce::Colour(0xffff6666) : juce::Colour(0xfff4c430));
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText("EMERGENCY STOP", textX, (int)bounds.getY() + 6, textW, 16, juce::Justification::centredLeft);

        g.setColour(latched_ ? juce::Colour(0xffff9999) : juce::Colour(0xffa8b8c8));
        g.setFont(juce::Font(10.0f));
        g.drawText(latched_ ? "LATCHED / TRIPPED" : "PULL / CLICK TO TRIP", textX, (int)bounds.getY() + 24, textW, 14, juce::Justification::centredLeft);
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
        : isKnob_(isKnob)
    {
        juce::String labelText = juce::String(title);
        if (!unit.empty())
            labelText << " [" << juce::String(unit) << "]";

        addAndMakeVisible(label);
        label.setFont(juce::Font(11.5f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffc5d1de));
        label.setText(labelText, juce::dontSendNotification);

        addAndMakeVisible(slider);
        slider.setSliderStyle(isKnob ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 18);
        if (!unit.empty())
            slider.setTextValueSuffix(" " + juce::String(unit));
    }

    void resized() override
    {
        auto b = getLocalBounds();
        if (isKnob_)
        {
            label.setBounds(b.removeFromTop(15));
            slider.setBounds(b);
        }
        else
        {
            // Compact horizontal arrangement:
            // Top: title label (15 px)
            // Bottom: sleek horizontal slider track with right numeric readout (20 px)
            label.setBounds(b.removeFromTop(15));
            slider.setBounds(b.reduced(0, 1));
        }
    }

    juce::Label label;
    juce::Slider slider;

private:
    bool isKnob_ = false;
};

double signalValue(const StateSnapshot& snap, const std::string& signal, bool* found = nullptr)
{
    if (const auto it = snap.numericSignals.find(signal); it != snap.numericSignals.end())
    {
        if (found != nullptr) *found = true;
        return it->second;
    }
    if (const auto it = snap.booleanSignals.find(signal); it != snap.booleanSignals.end())
    {
        if (found != nullptr) *found = true;
        return it->second ? 1.0 : 0.0;
    }
    if (found != nullptr) *found = false;
    return std::numeric_limits<double>::quiet_NaN();
}

juce::String csvCell(juce::String text)
{
    text = text.replace("\"", "\"\"");
    return "\"" + text + "\"";
}

} // namespace

struct MachineRecording
{
    std::vector<double> time;
    std::vector<std::vector<double>> values;
    double lastTime = -std::numeric_limits<double>::infinity();

    void ensureChannels(size_t channels)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        if (values.size() < channels)
            values.resize(channels, std::vector<double>(time.size(), nan));
        else if (values.size() > channels)
            values.resize(channels);
        for (auto& ch : values)
            if (ch.size() < time.size())
                ch.resize(time.size(), nan);
    }

    void clear()
    {
        time.clear();
        values.clear();
        lastTime = -std::numeric_limits<double>::infinity();
    }

    void append(const StateSnapshot& snap, const WidgetConfig& config)
    {
        if (config.plotChannels.empty())
            return;

        const bool hasSignals = !snap.numericSignals.empty() || !snap.booleanSignals.empty();
        const bool activeOrStepped = snap.running || snap.paused || snap.simTime > 0.0;
        if (!activeOrStepped || !hasSignals)
            return;

        if ((snap.running || snap.paused) && snap.simTime + 1.0e-12 < lastTime)
            clear();
        if (!(snap.simTime > lastTime))
            return;

        ensureChannels(config.plotChannels.size());
        time.push_back(snap.simTime);
        for (size_t i = 0; i < config.plotChannels.size(); ++i)
        {
            const auto& ch = config.plotChannels[i];
            bool ok = false;
            const auto raw = ch.signalName.empty() ? std::numeric_limits<double>::quiet_NaN()
                                                   : signalValue(snap, ch.signalName, &ok);
            values[i].push_back(ok ? raw * ch.scale + ch.offset : std::numeric_limits<double>::quiet_NaN());
        }
        lastTime = snap.simTime;

        const size_t maxSamples = (size_t)std::max(1000, config.maxSamples);
        while (time.size() > maxSamples)
            popFront();
    }

    void popFront()
    {
        time.erase(time.begin());
        for (auto& v : values)
            if (!v.empty())
                v.erase(v.begin());
    }

    std::shared_ptr<plot_instrument::Acquisition> acquisition(const WidgetConfig& config) const
    {
        auto acq = std::make_shared<plot_instrument::Acquisition>();
        acq->ok = true;
        acq->time = time;
        acq->values = values;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        if (acq->values.size() < config.plotChannels.size())
            acq->values.resize(config.plotChannels.size(), std::vector<double>(acq->time.size(), nan));
        for (size_t i = 0; i < config.plotChannels.size(); ++i)
        {
            if (acq->values[i].size() < acq->time.size())
                acq->values[i].resize(acq->time.size(), nan);
            const auto& ch = config.plotChannels[i];
            acq->labels.push_back(ch.title.empty() ? juce::String("Channel ") + juce::String((int)i + 1) : juce::String(ch.title));
            acq->units.push_back(juce::String(ch.unit));
        }
        return acq;
    }

    bool writeCsv(const juce::File& file, const WidgetConfig& config, juce::String& error) const
    {
        juce::String text;
        text << "time_s";
        for (const auto& ch : config.plotChannels)
            text << "," << csvCell(ch.title.empty() ? juce::String(ch.id) : juce::String(ch.title));
        text << "\n";
        for (size_t i = 0; i < time.size(); ++i)
        {
            text << juce::String(time[i], 9);
            for (size_t c = 0; c < config.plotChannels.size(); ++c)
            {
                text << ",";
                if (c < values.size() && i < values[c].size() && std::isfinite(values[c][i]))
                    text << juce::String(values[c][i], 12);
            }
            text << "\n";
        }
        if (!file.replaceWithText(text))
        {
            error = "Could not write " + file.getFullPathName();
            return false;
        }
        return true;
    }

    bool saveJson(const juce::File& file, const WidgetConfig& config, juce::String& error) const
    {
        auto* root = new juce::DynamicObject();
        root->setProperty("kind", "machine_plot_recording");
        root->setProperty("title", juce::String(config.title));
        root->setProperty("mode", juce::String(config.plotMode));

        juce::Array<juce::var> channels;
        for (const auto& ch : config.plotChannels)
            channels.add(ch.toJson());
        root->setProperty("channels", channels);

        juce::Array<juce::var> samples;
        for (size_t i = 0; i < time.size(); ++i)
        {
            auto* sample = new juce::DynamicObject();
            sample->setProperty("time", time[i]);
            juce::Array<juce::var> vals;
            for (size_t c = 0; c < config.plotChannels.size(); ++c)
                vals.add(c < values.size() && i < values[c].size() && std::isfinite(values[c][i]) ? juce::var(values[c][i]) : juce::var());
            sample->setProperty("values", vals);
            samples.add(juce::var(sample));
        }
        root->setProperty("samples", samples);

        if (!file.replaceWithText(juce::JSON::toString(juce::var(root), true)))
        {
            error = "Could not write " + file.getFullPathName();
            return false;
        }
        return true;
    }
};

namespace
{

constexpr int kTimeDivisions = 10;
constexpr int kLaneHeaderWidth = 156;
constexpr int kCollapsedLaneHeight = 28;
constexpr int kMinExpandedLaneHeight = 84;
constexpr int kMaxExpandedLaneHeight = 420;
constexpr int kDefaultExpandedLaneHeight = 165;

struct TimebasePreset
{
    double secPerDiv;
    const char* label;
};

constexpr TimebasePreset kTimebasePresets[] = {
    { 0.005, "5 ms/div" },
    { 0.010, "10 ms/div" },
    { 0.020, "20 ms/div" },
    { 0.050, "50 ms/div" },
    { 0.100, "100 ms/div" },
    { 0.200, "200 ms/div" },
    { 0.500, "500 ms/div" },
    { 1.000, "1 s/div" },
    { 2.000, "2 s/div" },
    { 5.000, "5 s/div" },
    { 10.00, "10 s/div" }
};

plot_instrument::Range computeLaneVerticalRange(const std::vector<double>& time,
                                                const std::vector<double>& values,
                                                const PlotChannelConfig& ch,
                                                double viewStart,
                                                double viewEnd,
                                                int* outFiniteCount = nullptr,
                                                double* outLatestValue = nullptr)
{
    double lo = std::numeric_limits<double>::infinity();
    double hi = -lo;
    double allLo = lo;
    double allHi = hi;
    int finiteCount = 0;
    int windowFiniteCount = 0;
    double latestVal = 0.0;
    double latestWindowVal = 0.0;

    const size_t n = std::min(time.size(), values.size());
    for (size_t i = 0; i < n; ++i)
    {
        const double v = values[i];
        if (!std::isfinite(v))
            continue;
        ++finiteCount;
        latestVal = v;
        allLo = std::min(allLo, v);
        allHi = std::max(allHi, v);
        if (time[i] >= viewStart - 1.0e-9 && time[i] <= viewEnd + 1.0e-9)
        {
            ++windowFiniteCount;
            latestWindowVal = v;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }

    if (outFiniteCount != nullptr)
        *outFiniteCount = finiteCount;
    if (outLatestValue != nullptr)
        *outLatestValue = (windowFiniteCount > 0) ? latestWindowVal : latestVal;

    if (windowFiniteCount == 0 && finiteCount > 0)
    {
        lo = allLo;
        hi = allHi;
    }

    // Legacy files sometimes saved default [0.0, 1.0] on channels; only treat
    // [minValue, maxValue] as a strict manual override if it wasn't the legacy
    // [0, 1] default or if data fits inside it.
    const bool hasExplicitManual = (ch.minValue < ch.maxValue)
                                && !(std::abs(ch.minValue) < 1.0e-12 && std::abs(ch.maxValue - 1.0) < 1.0e-12);
    if (hasExplicitManual)
        return { ch.minValue, ch.maxValue, true };

    if (finiteCount == 0)
        return { -1.0, 1.0, false };

    const double span = hi - lo;
    if (span <= 1.0e-9 * std::max(1.0, std::abs(hi)))
    {
        const double pad = (std::abs(lo) < 1.0e-9) ? 1.0 : std::max(0.5, std::abs(lo) * 0.15);
        return { lo - pad, hi + pad, true };
    }

    const double margin = span * 0.12;
    double yMin = lo - margin;
    double yMax = hi + margin;
    if (lo >= 0.0 && lo <= span * 0.25)
        yMin = std::min(yMin, -margin * 0.5);
    return { yMin, yMax, true };
}

plot_instrument::Range channelRange(const plot_instrument::Acquisition& acq, const WidgetConfig& config, int channel)
{
    if (channel < 0 || channel >= (int)acq.values.size())
        return { -1.0, 1.0, false };
    const PlotChannelConfig fallback;
    const auto& ch = (channel < (int)config.plotChannels.size()) ? config.plotChannels[(size_t)channel] : fallback;
    const double t0 = acq.time.empty() ? 0.0 : acq.time.front();
    const double t1 = acq.time.empty() ? 10.0 : acq.time.back();
    return computeLaneVerticalRange(acq.time, acq.values[(size_t)channel], ch, t0, t1);
}

class PlotPanelWidget final : public juce::Component
{
public:
    PlotPanelWidget(WidgetConfig configIn, MachineRecording& recordingIn, bool& recordEnabledIn,
                    std::function<void(const WidgetConfig&)> configChangedIn)
        : config(std::move(configIn)), recording(recordingIn), recordEnabled(recordEnabledIn),
          configChanged(std::move(configChangedIn)),
          lanesContainer(*this),
          timelineBar(*this)
    {
        ensureDefaultChannels();
        liveFollow_ = config.autoScroll;
        secondsPerDiv_ = std::clamp(config.visibleHistorySeconds / (double)kTimeDivisions, 0.001, 100.0);
        config.visibleHistorySeconds = secondsPerDiv_ * (double)kTimeDivisions;

        addAndMakeVisible(title);
        title.setText(juce::String(config.title.empty() ? "Strip Chart Recorder" : config.title), juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
        title.setFont(juce::Font(13.0f, juce::Font::bold));

        addAndMakeVisible(modeBox);
        modeBox.addItem("Strip Chart", 1);
        modeBox.addItem("XY Plot", 2);
        modeBox.setSelectedId(config.plotMode == "XY Plot" ? 2 : 1, juce::dontSendNotification);
        modeBox.onChange = [this] {
            config.plotMode = modeBox.getSelectedId() == 2 ? "XY Plot" : "Strip Chart";
            if (configChanged)
                configChanged(config);
            resized();
            updateSurface();
        };

        addAndMakeVisible(timebaseLabel);
        timebaseLabel.setText("Scale:", juce::dontSendNotification);
        timebaseLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9eb3c4));
        timebaseLabel.setFont(juce::Font(11.5f, juce::Font::bold));

        addAndMakeVisible(timebaseBox);
        int bestPresetId = 8; // 1 s/div default
        double bestDiff = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < std::size(kTimebasePresets); ++i)
        {
            timebaseBox.addItem(kTimebasePresets[i].label, (int)i + 1);
            const double d = std::abs(kTimebasePresets[i].secPerDiv - secondsPerDiv_);
            if (d < bestDiff)
            {
                bestDiff = d;
                bestPresetId = (int)i + 1;
            }
        }
        timebaseBox.setSelectedId(bestPresetId, juce::dontSendNotification);
        timebaseBox.onChange = [this] {
            const int idx = timebaseBox.getSelectedId() - 1;
            if (idx >= 0 && idx < (int)std::size(kTimebasePresets))
                applyTimebaseSecondsPerDiv(kTimebasePresets[idx].secPerDiv, false);
        };

        addAndMakeVisible(recordToggle);
        recordToggle.setButtonText("Record");
        recordToggle.setToggleState(recordEnabled, juce::dontSendNotification);
        recordToggle.onClick = [this] {
            recordEnabled = recordToggle.getToggleState();
            updateSurface();
        };

        addAndMakeVisible(liveFollowToggle);
        liveFollowToggle.setButtonText("Live / Follow");
        liveFollowToggle.setToggleState(liveFollow_, juce::dontSendNotification);
        liveFollowToggle.onClick = [this] {
            setLiveFollow(liveFollowToggle.getToggleState());
        };

        addAndMakeVisible(pauseDisplayToggle);
        pauseDisplayToggle.setButtonText("Pause View");
        pauseDisplayToggle.onClick = [this] {
            setViewPaused(pauseDisplayToggle.getToggleState());
        };

        addAndMakeVisible(markersToggle);
        markersToggle.setButtonText("Markers");
        markersToggle.onClick = [this] {
            showMarkers_ = markersToggle.getToggleState();
            xySurface.setMarkers(showMarkers_);
            lanesContainer.repaint();
        };

        auto prepButton = [this](juce::TextButton& b) {
            b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff22313d));
            b.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
            addAndMakeVisible(b);
        };
        prepButton(fitButton);
        prepButton(clearButton);
        prepButton(exportCsvButton);
        prepButton(saveSessionButton);

        fitButton.setTooltip("Fit entire recording across visible plot width (no empty future time)");
        clearButton.setTooltip("Clear recorded history");
        exportCsvButton.setTooltip("Export recorded data to CSV");
        saveSessionButton.setTooltip("Save plot recording session");

        fitButton.onClick = [this] { fitRecording(); };
        clearButton.onClick = [this] {
            recording.clear();
            liveFollow_ = true;
            liveFollowToggle.setToggleState(true, juce::dontSendNotification);
            viewStartTime_ = 0.0;
            viewCenterTime_ = visibleWindowSeconds() * 0.5;
            updateSurface();
        };
        exportCsvButton.onClick = [this] { chooseCsvExport(); };
        saveSessionButton.onClick = [this] { chooseSessionSave(); };

        addAndMakeVisible(info);
        info.setColour(juce::Label::textColourId, juce::Colour(0xff9eb3c4));
        info.setFont(juce::Font(11.0f));

        lanesViewport.setViewedComponent(&lanesContainer, false);
        lanesViewport.setScrollBarsShown(true, false);
        addAndMakeVisible(lanesViewport);

        addAndMakeVisible(timelineBar);

        addChildComponent(xySurface);
        xySurface.setMessage("No recorded samples yet.");

        rebuildLaneControls();
        updateSurface();
    }

    const std::string& widgetId() const { return config.id; }
    const WidgetConfig& widgetConfig() const { return config; }

    int preferredRecorderHeight() const
    {
        if (config.plotMode == "XY Plot")
            return std::max(360, config.height);
        int totalLanesH = 0;
        for (const auto& ch : config.plotChannels)
        {
            if (!ch.visible)
                continue;
            totalLanesH += (ch.collapsed ? kCollapsedLaneHeight : std::clamp(ch.laneHeight, kMinExpandedLaneHeight, kMaxExpandedLaneHeight)) + 4;
        }
        return std::max(380, 36 + totalLanesH + 46 + 16);
    }

    void updateDisplay()
    {
        if (!viewPaused_)
            updateSurface();
        else
            refreshStatusSummary();
    }

    void setTimebaseSecondsPerDiv(double secPerDiv)
    {
        applyTimebaseSecondsPerDiv(secPerDiv, true);
    }

    void scrollHistoryToStartTime(double startTime)
    {
        if (liveFollow_)
        {
            liveFollow_ = false;
            config.autoScroll = false;
            liveFollowToggle.setToggleState(false, juce::dontSendNotification);
        }

        const double win = visibleWindowSeconds();
        const double t0 = recording.time.empty() ? 0.0 : recording.time.front();
        const double tLatest = recording.time.empty() ? win : std::max(t0 + win, recording.time.back());

        const double minStart = t0 - win * 0.8;
        const double maxStart = tLatest - win * 0.2;
        viewStartTime_ = std::clamp(startTime, minStart, std::max(minStart, maxStart));
        viewCenterTime_ = viewStartTime_ + win * 0.5;
        frozenViewStartTime_ = viewStartTime_;

        if (configChanged)
            configChanged(config);
        updateSurface();
    }

    void scrollHistoryToCenter(double centerTimeSec)
    {
        scrollHistoryToStartTime(centerTimeSec - visibleWindowSeconds() * 0.5);
    }

    void panHistoryByDelta(double deltaSeconds)
    {
        const double currentCenter = viewStartTime_ + visibleWindowSeconds() * 0.5;
        scrollHistoryToCenter(currentCenter + deltaSeconds);
    }

    void zoomAtScreenX(int screenX, double factor, int waveformLeft = -1, int waveformWidth = -1)
    {
        if (waveformLeft < 0 || waveformWidth <= 0)
        {
            for (const auto& st : laneStates_)
            {
                if (!st.waveformBounds.isEmpty())
                {
                    waveformLeft = st.waveformBounds.getX();
                    waveformWidth = st.waveformBounds.getWidth();
                    break;
                }
            }
        }
        if (waveformWidth <= 10)
            return;

        const double currentWin = visibleWindowSeconds();
        const double clampedX = (double)std::clamp(screenX, waveformLeft, waveformLeft + waveformWidth);
        const double frac = (clampedX - (double)waveformLeft) / (double)waveformWidth;
        const double tCursor = viewStartTime_ + frac * currentWin;

        const double newWin = std::clamp(currentWin * factor, 0.01, 1000.0);
        const double newSecPerDiv = newWin / (double)kTimeDivisions;

        // Keep tCursor stationary at the exact same screen pixel
        const double newStartTime = tCursor - frac * newWin;

        if (liveFollow_)
        {
            liveFollow_ = false;
            config.autoScroll = false;
            liveFollowToggle.setToggleState(false, juce::dontSendNotification);
        }

        secondsPerDiv_ = newSecPerDiv;
        config.visibleHistorySeconds = newWin;
        viewStartTime_ = newStartTime;
        viewCenterTime_ = newStartTime + newWin * 0.5;
        frozenViewStartTime_ = viewStartTime_;

        syncTimebaseCombo();
        if (configChanged)
            configChanged(config);
        updateSurface();
    }

    void fitRecording()
    {
        if (recording.time.empty())
            return;

        const double t0 = recording.time.front();
        const double tLatest = recording.time.back();
        const double duration = std::max(0.01, tLatest - t0);

        if (liveFollow_)
        {
            liveFollow_ = false;
            config.autoScroll = false;
            liveFollowToggle.setToggleState(false, juce::dontSendNotification);
        }

        secondsPerDiv_ = duration / (double)kTimeDivisions;
        config.visibleHistorySeconds = duration;
        viewStartTime_ = t0;
        viewCenterTime_ = t0 + duration * 0.5;
        frozenViewStartTime_ = viewStartTime_;

        syncTimebaseCombo();
        if (configChanged)
            configChanged(config);
        updateSurface();
    }

    void setLiveFollow(bool follow)
    {
        liveFollow_ = follow;
        config.autoScroll = follow;
        liveFollowToggle.setToggleState(follow, juce::dontSendNotification);
        if (follow && viewPaused_)
        {
            viewPaused_ = false;
            pauseDisplayToggle.setToggleState(false, juce::dontSendNotification);
        }
        if (configChanged)
            configChanged(config);
        updateSurface();
    }

    void setViewPaused(bool paused)
    {
        viewPaused_ = paused;
        pauseDisplayToggle.setToggleState(paused, juce::dontSendNotification);
        if (paused)
        {
            frozenViewStartTime_ = viewStartTime_;
            frozenSampleCount_ = (int)recording.time.size();
        }
        updateSurface();
    }

    void setRecordingEnabled(bool enabled)
    {
        recordEnabled = enabled;
        recordToggle.setToggleState(enabled, juce::dontSendNotification);
        updateSurface();
    }

    bool setLaneCollapsed(int laneIndex, bool collapsed)
    {
        if (laneIndex < 0 || laneIndex >= (int)config.plotChannels.size())
            return false;
        config.plotChannels[(size_t)laneIndex].collapsed = collapsed;
        if (configChanged)
            configChanged(config);
        layoutLanes();
        updateSurface();
        return true;
    }

    bool setLaneHeight(int laneIndex, int heightPx)
    {
        if (laneIndex < 0 || laneIndex >= (int)config.plotChannels.size())
            return false;
        config.plotChannels[(size_t)laneIndex].laneHeight = std::clamp(heightPx, kMinExpandedLaneHeight, kMaxExpandedLaneHeight);
        config.plotChannels[(size_t)laneIndex].collapsed = false;
        if (configChanged)
            configChanged(config);
        layoutLanes();
        updateSurface();
        return true;
    }

    bool exportCsvToFile(const juce::File& file, juce::String& error) const
    {
        return recording.writeCsv(file, config, error);
    }

    ControlPanelComponent::StripChartRecorderSnapshot getSnapshot() const
    {
        ControlPanelComponent::StripChartRecorderSnapshot snap;
        snap.widgetId = config.id;
        snap.mode = config.plotMode;
        snap.recordEnabled = recordEnabled;
        snap.liveFollow = liveFollow_;
        snap.viewPaused = viewPaused_;
        snap.secondsPerDivision = secondsPerDiv_;
        snap.visibleWindowSeconds = visibleWindowSeconds();
        snap.viewStartTime = viewStartTime_;
        snap.viewEndTime = viewStartTime_ + visibleWindowSeconds();
        snap.totalSamples = (int)recording.time.size();
        snap.recorderBounds = getBounds();

        for (size_t i = 0; i < config.plotChannels.size(); ++i)
        {
            const auto& ch = config.plotChannels[i];
            ControlPanelComponent::StripChartLaneSnapshot lane;
            lane.channelId = ch.id;
            lane.title = ch.title;
            lane.signalName = ch.signalName;
            lane.unit = ch.unit;
            lane.visible = ch.visible;
            lane.collapsed = ch.collapsed;

            if (i < laneStates_.size())
            {
                lane.signalAvailable = laneStates_[i].signalAvailable;
                lane.finiteSampleCount = laneStates_[i].finiteSampleCount;
                lane.latestValue = laneStates_[i].latestValue;
                lane.yMin = laneStates_[i].yRange.min;
                lane.yMax = laneStates_[i].yRange.max;
                lane.boundsInRecorder = laneStates_[i].laneBounds;
                lane.waveformBounds = laneStates_[i].waveformBounds;
            }
            snap.lanes.push_back(std::move(lane));
        }
        return snap;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff10161d));
        g.setColour(juce::Colour(0xff2b3747));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 1.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(8);
        auto top = b.removeFromTop(28);
        title.setBounds(top.removeFromLeft(std::clamp(top.getWidth() / 6, 95, 160)));
        modeBox.setBounds(top.removeFromLeft(102).reduced(2));
        timebaseLabel.setBounds(top.removeFromLeft(42));
        timebaseBox.setBounds(top.removeFromLeft(94).reduced(2));
        recordToggle.setBounds(top.removeFromLeft(68));
        liveFollowToggle.setBounds(top.removeFromLeft(96));
        pauseDisplayToggle.setBounds(top.removeFromLeft(88));
        markersToggle.setBounds(top.removeFromLeft(72));

        saveSessionButton.setBounds(top.removeFromRight(48).reduced(2));
        exportCsvButton.setBounds(top.removeFromRight(46).reduced(2));
        clearButton.setBounds(top.removeFromRight(46).reduced(2));
        fitButton.setBounds(top.removeFromRight(44).reduced(2));

        b.removeFromTop(4);

        const bool xyMode = (modeBox.getSelectedId() == 2);
        lanesViewport.setVisible(!xyMode);
        timelineBar.setVisible(!xyMode);
        xySurface.setVisible(xyMode);

        if (xyMode)
        {
            info.setBounds(b.removeFromBottom(18));
            b.removeFromBottom(4);
            xySurface.setBounds(b);
        }
        else
        {
            auto bottomArea = b.removeFromBottom(44);
            timelineBar.setBounds(bottomArea.removeFromTop(26));
            info.setBounds(bottomArea);
            b.removeFromBottom(2);
            lanesViewport.setBounds(b);
            layoutLanes();
        }
    }

private:
    struct LaneRuntimeState
    {
        juce::Rectangle<int> laneBounds;
        juce::Rectangle<int> headerBounds;
        juce::Rectangle<int> waveformBounds;
        juce::Rectangle<int> resizeBarBounds;
        plot_instrument::Range yRange { -1.0, 1.0, false };
        int finiteSampleCount = 0;
        double latestValue = 0.0;
        bool signalAvailable = false;
    };

    class LanesContainerComponent final : public juce::Component
    {
    public:
        explicit LanesContainerComponent(PlotPanelWidget& ownerIn) : owner(ownerIn) {}

        void paint(juce::Graphics& g) override
        {
            owner.paintLanes(g);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            owner.handleLanesMouseDown(e);
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            owner.handleLanesMouseDrag(e);
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            owner.handleLanesMouseUp(e);
        }

        void mouseMove(const juce::MouseEvent& e) override
        {
            owner.handleLanesMouseMove(e);
        }

        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            owner.handleLanesMouseWheel(e, wheel);
        }

    private:
        PlotPanelWidget& owner;
    };

    class TimelineNavigatorComponent final : public juce::Component
    {
    public:
        explicit TimelineNavigatorComponent(PlotPanelWidget& ownerIn) : owner(ownerIn)
        {
            auto prep = [this](juce::TextButton& b) {
                b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1c2833));
                b.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc9dceb));
                addAndMakeVisible(b);
            };
            prep(stepBackBtn);
            prep(stepFwdBtn);
            prep(fitBtn);
            prep(toLiveBtn);

            stepBackBtn.setTooltip("Pan view backward in time");
            stepFwdBtn.setTooltip("Pan view forward in time");
            fitBtn.setTooltip("Fit entire recording across visible plot width (no empty future time)");
            toLiveBtn.setTooltip("Follow newest data at live edge");

            stepBackBtn.onClick = [this] { owner.panHistoryByDelta(-owner.visibleWindowSeconds() * 0.25); };
            stepFwdBtn.onClick = [this] { owner.panHistoryByDelta(owner.visibleWindowSeconds() * 0.25); };
            fitBtn.onClick = [this] { owner.fitRecording(); };
            toLiveBtn.onClick = [this] { owner.setLiveFollow(true); };
        }

        void resized() override
        {
            auto b = getLocalBounds();
            auto leftControls = b.removeFromLeft(kLaneHeaderWidth);
            stepBackBtn.setBounds(leftControls.removeFromLeft(30).reduced(1, 3));
            stepFwdBtn.setBounds(leftControls.removeFromLeft(30).reduced(1, 3));
            fitBtn.setBounds(leftControls.removeFromLeft(48).reduced(1, 3));
            toLiveBtn.setBounds(leftControls.reduced(2, 3));
            scrubberBounds = b.reduced(6, 3);
        }

        void paint(juce::Graphics& g) override
        {
            owner.paintTimelineNavigator(g, scrubberBounds);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (scrubberBounds.contains(e.getPosition()))
                scrubToX(e.position.x);
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (scrubberBounds.getWidth() > 10)
                scrubToX(e.position.x);
        }

    private:
        void scrubToX(float x)
        {
            if (scrubberBounds.getWidth() <= 4)
                return;
            const double t0 = owner.recording.time.empty() ? 0.0 : owner.recording.time.front();
            const double win = owner.visibleWindowSeconds();
            const double tLatest = owner.recording.time.empty() ? win : std::max(t0 + win, owner.recording.time.back());
            const double frac = std::clamp((double)(x - (float)scrubberBounds.getX()) / (double)scrubberBounds.getWidth(), 0.0, 1.0);
            const double targetCenter = t0 + frac * (tLatest - t0);
            owner.scrollHistoryToCenter(targetCenter);
        }

        PlotPanelWidget& owner;
        juce::TextButton stepBackBtn { "<" }, stepFwdBtn { ">" }, fitBtn { "Fit" }, toLiveBtn { "Live" };
        juce::Rectangle<int> scrubberBounds;
    };

    void ensureDefaultChannels()
    {
        if (config.plotChannels.empty())
        {
            config.plotChannels.push_back({ "ch1", config.title.empty() ? "Channel 1" : config.title,
                                            config.signalName, config.unit, 0xff35c6ff, 1.0, 0.0, 0.0, 0.0, true, kDefaultExpandedLaneHeight, false });
        }
        for (auto& ch : config.plotChannels)
        {
            if (ch.laneHeight < kMinExpandedLaneHeight || ch.laneHeight > kMaxExpandedLaneHeight)
                ch.laneHeight = kDefaultExpandedLaneHeight;
        }
    }

    double visibleWindowSeconds() const
    {
        return std::max(0.01, secondsPerDiv_ * (double)kTimeDivisions);
    }

    void syncTimebaseCombo()
    {
        int bestId = 0;
        double bestDiff = 1.0e9;
        for (size_t i = 0; i < std::size(kTimebasePresets); ++i)
        {
            const double d = std::abs(kTimebasePresets[i].secPerDiv - secondsPerDiv_);
            if (d < bestDiff)
            {
                bestDiff = d;
                bestId = (int)i + 1;
            }
        }
        if (bestId > 0 && bestDiff <= kTimebasePresets[(size_t)(bestId - 1)].secPerDiv * 0.05)
        {
            timebaseBox.setSelectedId(bestId, juce::dontSendNotification);
        }
        else
        {
            timebaseBox.setSelectedId(0, juce::dontSendNotification);
            timebaseBox.setText(juce::String(secondsPerDiv_, secondsPerDiv_ < 0.1 ? 3 : 2) + " s/div", juce::dontSendNotification);
        }
    }

    void applyTimebaseSecondsPerDiv(double secPerDiv, bool syncCombo)
    {
        const double clamped = std::clamp(secPerDiv, 0.001, 100.0);
        const double oldWin = visibleWindowSeconds();
        const double currentCenter = viewStartTime_ + oldWin * 0.5;
        const double newWin = clamped * (double)kTimeDivisions;

        secondsPerDiv_ = clamped;
        config.visibleHistorySeconds = newWin;

        if (syncCombo)
            syncTimebaseCombo();

        const double t0 = recording.time.empty() ? 0.0 : recording.time.front();
        const double tLatest = recording.time.empty() ? 0.0 : recording.time.back();

        if (liveFollow_ && !viewPaused_)
        {
            if (recording.time.empty() || (tLatest - t0) <= newWin)
                viewStartTime_ = t0;
            else
                viewStartTime_ = tLatest - newWin;
            viewCenterTime_ = viewStartTime_ + newWin * 0.5;
            frozenViewStartTime_ = viewStartTime_;
        }
        else
        {
            viewStartTime_ = currentCenter - newWin * 0.5;
            viewCenterTime_ = currentCenter;
            frozenViewStartTime_ = viewStartTime_;
        }

        if (configChanged)
            configChanged(config);
        updateSurface();
    }

    void rebuildLaneControls()
    {
        laneCollapseButtons_.clear();
        laneTallerButtons_.clear();
        laneShorterButtons_.clear();
        laneStates_.resize(config.plotChannels.size());

        for (size_t i = 0; i < config.plotChannels.size(); ++i)
        {
            auto collapseBtn = std::make_unique<juce::TextButton>(config.plotChannels[i].collapsed ? ">" : "v");
            collapseBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e2c38));
            collapseBtn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
            collapseBtn->setTooltip("Collapse / Expand channel lane");
            collapseBtn->onClick = [this, idx = (int)i] {
                const bool next = !config.plotChannels[(size_t)idx].collapsed;
                setLaneCollapsed(idx, next);
            };
            lanesContainer.addAndMakeVisible(*collapseBtn);
            laneCollapseButtons_.push_back(std::move(collapseBtn));

            auto shorterBtn = std::make_unique<juce::TextButton>("-");
            shorterBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e2c38));
            shorterBtn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
            shorterBtn->setTooltip("Decrease channel lane height");
            shorterBtn->onClick = [this, idx = (int)i] {
                setLaneHeight(idx, config.plotChannels[(size_t)idx].laneHeight - 24);
            };
            lanesContainer.addAndMakeVisible(*shorterBtn);
            laneShorterButtons_.push_back(std::move(shorterBtn));

            auto tallerBtn = std::make_unique<juce::TextButton>("+");
            tallerBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e2c38));
            tallerBtn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
            tallerBtn->setTooltip("Increase channel lane height");
            tallerBtn->onClick = [this, idx = (int)i] {
                setLaneHeight(idx, config.plotChannels[(size_t)idx].laneHeight + 24);
            };
            lanesContainer.addAndMakeVisible(*tallerBtn);
            laneTallerButtons_.push_back(std::move(tallerBtn));
        }
    }

    void layoutLanes()
    {
        if (laneStates_.size() != config.plotChannels.size())
            rebuildLaneControls();

        const int viewportW = std::max(240, lanesViewport.getWidth() - (lanesViewport.isVerticalScrollBarShown() ? 12 : 2));
        int y = 0;
        for (size_t i = 0; i < config.plotChannels.size(); ++i)
        {
            auto& ch = config.plotChannels[i];
            auto& st = laneStates_[i];
            if (!ch.visible)
            {
                st.laneBounds = {};
                st.headerBounds = {};
                st.waveformBounds = {};
                st.resizeBarBounds = {};
                if (i < laneCollapseButtons_.size()) laneCollapseButtons_[i]->setVisible(false);
                if (i < laneShorterButtons_.size()) laneShorterButtons_[i]->setVisible(false);
                if (i < laneTallerButtons_.size()) laneTallerButtons_[i]->setVisible(false);
                continue;
            }

            const int laneH = ch.collapsed
                            ? kCollapsedLaneHeight
                            : std::clamp(ch.laneHeight, kMinExpandedLaneHeight, kMaxExpandedLaneHeight);
            st.laneBounds = { 0, y, viewportW, laneH };
            st.headerBounds = { 0, y, kLaneHeaderWidth, laneH };
            st.waveformBounds = ch.collapsed
                              ? juce::Rectangle<int>(kLaneHeaderWidth + 4, y + 2, std::max(40, viewportW - kLaneHeaderWidth - 10), laneH - 4)
                              : juce::Rectangle<int>(kLaneHeaderWidth + 4, y + 6, std::max(40, viewportW - kLaneHeaderWidth - 10), laneH - 14);
            st.resizeBarBounds = ch.collapsed
                               ? juce::Rectangle<int>()
                               : juce::Rectangle<int>(0, y + laneH - 5, viewportW, 5);

            if (i < laneCollapseButtons_.size())
            {
                laneCollapseButtons_[i]->setVisible(true);
                laneCollapseButtons_[i]->setButtonText(ch.collapsed ? ">" : "v");
                laneCollapseButtons_[i]->setBounds(4, y + 4, 20, 20);
            }
            if (i < laneShorterButtons_.size() && i < laneTallerButtons_.size())
            {
                const bool showSizeBtns = !ch.collapsed && laneH >= 64;
                laneShorterButtons_[i]->setVisible(showSizeBtns);
                laneTallerButtons_[i]->setVisible(showSizeBtns);
                if (showSizeBtns)
                {
                    laneShorterButtons_[i]->setBounds(kLaneHeaderWidth - 44, y + 4, 18, 18);
                    laneTallerButtons_[i]->setBounds(kLaneHeaderWidth - 24, y + 4, 18, 18);
                }
            }

            y += laneH + 4;
        }

        const int totalH = std::max(y, lanesViewport.getHeight());
        lanesContainer.setBounds(0, 0, viewportW, totalH);
    }

    void updateSurface()
    {
        if (laneStates_.size() != config.plotChannels.size())
            layoutLanes();

        const auto acq = recording.acquisition(config);
        const double win = visibleWindowSeconds();
        const double t0 = acq->time.empty() ? 0.0 : acq->time.front();
        const double tLatest = acq->time.empty() ? 0.0 : acq->time.back();

        if (viewPaused_)
        {
            viewStartTime_ = frozenViewStartTime_;
        }
        else if (liveFollow_)
        {
            // Fixed horizontal timebase from the very first sample:
            // While (tLatest - t0) <= win, keep window anchored at [t0, t0 + win]
            // so the selected s/div scale applies immediately without compression.
            // Once (tLatest - t0) > win, scroll continuously as [tLatest - win, tLatest].
            if (acq->time.empty() || (tLatest - t0) <= win)
                viewStartTime_ = t0;
            else
                viewStartTime_ = tLatest - win;
            viewCenterTime_ = viewStartTime_ + win * 0.5;
            frozenViewStartTime_ = viewStartTime_;
        }
        else
        {
            // In history navigation mode, maintain the user's chosen viewStartTime_
            viewCenterTime_ = viewStartTime_ + win * 0.5;
            frozenViewStartTime_ = viewStartTime_;
        }

        const double viewEnd = viewStartTime_ + win;

        for (size_t i = 0; i < config.plotChannels.size(); ++i)
        {
            const auto& ch = config.plotChannels[i];
            auto& st = laneStates_[i];
            if (i < acq->values.size() && !ch.signalName.empty())
            {
                st.yRange = computeLaneVerticalRange(acq->time, acq->values[i], ch, viewStartTime_, viewEnd,
                                                     &st.finiteSampleCount, &st.latestValue);
                st.signalAvailable = (st.finiteSampleCount > 0);
            }
            else
            {
                st.yRange = { -1.0, 1.0, false };
                st.finiteSampleCount = 0;
                st.latestValue = 0.0;
                st.signalAvailable = false;
            }
        }

        if (modeBox.getSelectedId() == 2)
        {
            if (acq->time.empty() || acq->values.size() < 2)
            {
                xySurface.setMessage("No recorded samples yet.");
                xySurface.setAcquisition(nullptr, plot_instrument::Mode::XY, {});
            }
            else
            {
                PlotSurface::Axes axes;
                const int x = juce::jlimit(0, (int)acq->values.size() - 1, config.xChannel);
                const int y = juce::jlimit(0, (int)acq->values.size() - 1, config.yChannel);
                axes.signals = { x, y };
                axes.ranges = { channelRange(*acq, config, x), channelRange(*acq, config, y) };
                xySurface.setTraceColours({ juce::Colour(config.plotChannels[(size_t)x].colourArgb),
                                            juce::Colour(config.plotChannels[(size_t)y].colourArgb) });
                xySurface.setAcquisition(acq, plot_instrument::Mode::XY, axes);
            }
        }

        refreshStatusSummary();
        lanesContainer.repaint();
        timelineBar.repaint();
    }

    void refreshStatusSummary()
    {
        const double win = visibleWindowSeconds();
        juce::String summary;
        summary << (int)recording.time.size() << " samples";
        if (!recording.time.empty())
            summary << " | t=" << juce::String(recording.time.back(), 3) << "s";
        summary << " | Window [" << juce::String(viewStartTime_, 3) << "s .. "
                << juce::String(viewStartTime_ + win, 3) << "s] ("
                << juce::String(secondsPerDiv_, secondsPerDiv_ < 0.1 ? 3 : 2) << " s/div)";
        summary << " | Acq: " << (recordEnabled ? "REC" : "HOLD");
        summary << " | View: " << (viewPaused_ ? "PAUSED" : (liveFollow_ ? "LIVE" : "HISTORY"));
        info.setText(summary, juce::dontSendNotification);
    }

    void paintLanes(juce::Graphics& g)
    {
        g.fillAll(juce::Colour(0xff0b1015));

        const double win = visibleWindowSeconds();
        const double viewStart = viewStartTime_;
        const double viewEnd = viewStart + win;

        for (size_t i = 0; i < config.plotChannels.size() && i < laneStates_.size(); ++i)
        {
            const auto& ch = config.plotChannels[i];
            const auto& st = laneStates_[i];
            if (!ch.visible || st.laneBounds.isEmpty())
                continue;

            const juce::Colour traceCol(ch.colourArgb != 0 ? ch.colourArgb : 0xff35c6ff);

            // Lane background & border
            g.setColour(juce::Colour(0xff121a22));
            g.fillRect(st.laneBounds);
            g.setColour(juce::Colour(0xff16212b));
            g.fillRect(st.headerBounds);

            // Left channel color bar
            g.setColour(traceCol);
            g.fillRect(st.headerBounds.getX(), st.headerBounds.getY(), 4, st.headerBounds.getHeight());

            // Header labels
            const juce::String chBadge = "CH" + juce::String((int)i + 1);
            const juce::String dispName = ch.title.empty()
                                        ? (ch.signalName.empty() ? chBadge : juce::String(ch.signalName))
                                        : juce::String(ch.title);
            const juce::String unitStr = ch.unit.empty() ? juce::String() : (" [" + juce::String(ch.unit) + "]");

            g.setColour(traceCol);
            g.setFont(juce::Font(11.5f, juce::Font::bold));
            g.drawFittedText(chBadge + " " + dispName,
                             juce::Rectangle<int>(28, st.headerBounds.getY() + 4, kLaneHeaderWidth - 76, 18),
                             juce::Justification::centredLeft, 1);

            if (!ch.collapsed)
            {
                g.setColour(juce::Colour(0xff8fa8ba));
                g.setFont(juce::Font(10.5f));
                const juce::String sigLine = ch.signalName.empty()
                                           ? juce::String("Unconnected")
                                           : (juce::String(ch.signalName) + unitStr);
                g.drawFittedText(sigLine,
                                 juce::Rectangle<int>(10, st.headerBounds.getY() + 26, kLaneHeaderWidth - 16, 15),
                                 juce::Justification::centredLeft, 1);

                // Live / current value badge
                juce::String valText;
                juce::Colour valCol = juce::Colour(0xffe6f2f8);
                if (ch.signalName.empty())
                {
                    valText = "UNCONNECTED";
                    valCol = juce::Colour(0xffffb703);
                }
                else if (recording.time.empty())
                {
                    valText = "READY" + unitStr;
                    valCol = juce::Colour(0xff9eb3c4);
                }
                else if (!st.signalAvailable)
                {
                    valText = "UNAVAILABLE";
                    valCol = juce::Colour(0xffff6b6b);
                }
                else
                {
                    valText = juce::String(st.latestValue, 4) + (ch.unit.empty() ? "" : (" " + juce::String(ch.unit)));
                    valCol = traceCol.brighter(0.25f);
                }

                g.setColour(valCol);
                g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
                g.drawFittedText(valText,
                                 juce::Rectangle<int>(10, st.headerBounds.getY() + 44, kLaneHeaderWidth - 16, 18),
                                 juce::Justification::centredLeft, 1);

                // Vertical scale summary in header
                if (st.laneBounds.getHeight() >= 92)
                {
                    const double ySpan = std::max(1.0e-9, st.yRange.max - st.yRange.min);
                    const double vPerDiv = ySpan / 4.0;
                    g.setColour(juce::Colour(0xff748c9e));
                    g.setFont(juce::Font(10.0f));
                    g.drawFittedText("Scale: " + juce::String(vPerDiv, 3) + (ch.unit.empty() ? "/div" : (" " + juce::String(ch.unit) + "/div")),
                                     juce::Rectangle<int>(10, st.headerBounds.getY() + 64, kLaneHeaderWidth - 16, 14),
                                     juce::Justification::centredLeft, 1);
                    g.drawFittedText("[" + juce::String(st.yRange.min, 2) + " .. " + juce::String(st.yRange.max, 2) + "]",
                                     juce::Rectangle<int>(10, st.headerBounds.getY() + 78, kLaneHeaderWidth - 16, 14),
                                     juce::Justification::centredLeft, 1);
                }
            }

            // Waveform plotting canvas for this lane
            const auto wf = st.waveformBounds.toFloat();
            g.setColour(juce::Colour(0xff090d12));
            g.fillRect(wf);

            // Synchronized 10 horizontal time divisions
            for (int div = 0; div <= kTimeDivisions; ++div)
            {
                const float x = wf.getX() + wf.getWidth() * ((float)div / (float)kTimeDivisions);
                g.setColour(div == 0 || div == kTimeDivisions ? juce::Colour(0xff2c3d4d) : juce::Colour(0xff18232d));
                g.drawVerticalLine((int)std::round(x), wf.getY(), wf.getBottom());
            }

            if (ch.collapsed)
            {
                // Compact summary in collapsed waveform bar
                g.setColour(traceCol.withAlpha(0.85f));
                g.setFont(juce::Font(11.0f));
                const juce::String status = ch.signalName.empty() ? "UNCONNECTED"
                                          : (!st.signalAvailable && !recording.time.empty()) ? ("UNAVAILABLE (" + juce::String(ch.signalName) + ")")
                                          : st.signalAvailable ? (juce::String(st.latestValue, 4) + " " + juce::String(ch.unit))
                                          : juce::String(ch.signalName);
                g.drawFittedText(status, st.waveformBounds.reduced(8, 0), juce::Justification::centredLeft, 1);
            }
            else
            {
                const double yMin = st.yRange.min;
                const double yMax = std::max(yMin + 1.0e-9, st.yRange.max);
                auto sy = [&](double v) -> float {
                    return wf.getBottom() - (float)((v - yMin) / (yMax - yMin)) * wf.getHeight();
                };
                auto sx = [&](double t) -> float {
                    return wf.getX() + (float)((t - viewStart) / win) * wf.getWidth();
                };

                // 4 horizontal vertical-scale divisions + tick labels
                g.setFont(juce::Font(9.5f));
                for (int vDiv = 0; vDiv <= 4; ++vDiv)
                {
                    const double frac = (double)vDiv / 4.0;
                    const double val = yMin + frac * (yMax - yMin);
                    const float y = wf.getBottom() - (float)frac * wf.getHeight();
                    g.setColour(vDiv == 2 ? juce::Colour(0xff223240) : juce::Colour(0xff16212b));
                    g.drawHorizontalLine((int)std::round(y), wf.getX(), wf.getRight());

                    g.setColour(juce::Colour(0xff7f97a8));
                    const float labelY = (vDiv == 0) ? (y - 12.0f) : (vDiv == 4 ? y + 1.0f : y - 6.0f);
                    g.drawText(juce::String(val, 2),
                               juce::Rectangle<float>(wf.getX() + 3.0f, labelY, 54.0f, 11.0f),
                               juce::Justification::centredLeft, false);
                }

                // Zero reference line if 0.0 lies within [yMin, yMax]
                if (yMin <= 0.0 && yMax >= 0.0)
                {
                    const float y0 = sy(0.0);
                    g.setColour(juce::Colour(0xff344e63));
                    g.drawHorizontalLine((int)std::round(y0), wf.getX(), wf.getRight());
                }

                if (ch.signalName.empty())
                {
                    g.setColour(juce::Colour(0xffffb703));
                    g.setFont(juce::Font(12.0f, juce::Font::bold));
                    g.drawFittedText("UNCONNECTED — Wire a machine output port to " + juce::String(ch.id),
                                     st.waveformBounds.reduced(16), juce::Justification::centred, 2);
                }
                else if (recording.time.empty())
                {
                    g.setColour(juce::Colour(0xff7f97a8));
                    g.setFont(juce::Font(11.5f));
                    g.drawFittedText("Waiting for simulation samples (" + juce::String(ch.signalName) + ")...",
                                     st.waveformBounds.reduced(16), juce::Justification::centred, 2);
                }
                else if (!st.signalAvailable)
                {
                    g.setColour(juce::Colour(0xffff6b6b));
                    g.setFont(juce::Font(12.0f, juce::Font::bold));
                    g.drawFittedText("SIGNAL UNAVAILABLE: " + juce::String(ch.signalName),
                                     st.waveformBounds.reduced(16), juce::Justification::centred, 2);
                }
                else if (i < recording.values.size())
                {
                    const auto& times = recording.time;
                    const auto& vals = recording.values[i];
                    const size_t n = std::min(times.size(), vals.size());

                    g.saveState();
                    g.reduceClipRegion(st.waveformBounds);

                    juce::Path path;
                    bool pen = false;
                    int drawnPoints = 0;
                    juce::Point<float> lastPt {};

                    // Include one sample before viewStart and one after viewEnd so lines clip cleanly
                    auto itStart = std::lower_bound(times.begin(), times.end(), viewStart);
                    size_t idxStart = (itStart == times.end()) ? 0 : (size_t)(itStart - times.begin());
                    if (idxStart > 0)
                        --idxStart;

                    auto itEnd = std::upper_bound(times.begin(), times.end(), viewEnd);
                    size_t idxEnd = (itEnd == times.end()) ? n : std::min(n, (size_t)(itEnd - times.begin()) + 1);

                    const size_t spanCount = (idxEnd > idxStart) ? (idxEnd - idxStart) : 0;
                    const size_t stride = std::max<size_t>(1, spanCount / 2500);

                    for (size_t s = idxStart; s < idxEnd; s += stride)
                    {
                        const double v = vals[s];
                        if (!std::isfinite(v))
                        {
                            pen = false;
                            continue;
                        }
                        const juce::Point<float> pt { sx(times[s]), sy(v) };
                        if (pen)
                            path.lineTo(pt);
                        else
                            path.startNewSubPath(pt);
                        pen = true;
                        lastPt = pt;
                        ++drawnPoints;
                    }
                    // Ensure the very last sample in range is always included
                    if (idxEnd > idxStart)
                    {
                        const size_t lastIdx = idxEnd - 1;
                        if ((lastIdx - idxStart) % stride != 0 && std::isfinite(vals[lastIdx]))
                        {
                            const juce::Point<float> pt { sx(times[lastIdx]), sy(vals[lastIdx]) };
                            if (pen)
                                path.lineTo(pt);
                            else
                                path.startNewSubPath(pt);
                            lastPt = pt;
                            ++drawnPoints;
                        }
                    }

                    g.setColour(traceCol);
                    if (drawnPoints >= 2)
                    {
                        g.strokePath(path, juce::PathStrokeType(1.8f));
                    }
                    else if (drawnPoints == 1)
                    {
                        g.fillEllipse(lastPt.x - 2.5f, lastPt.y - 2.5f, 5.0f, 5.0f);
                    }

                    if (showMarkers_ && drawnPoints > 0)
                    {
                        const size_t markerStride = std::max<size_t>(1, spanCount / 60);
                        for (size_t s = idxStart; s < idxEnd; s += markerStride)
                        {
                            if (std::isfinite(vals[s]))
                                g.fillRect(sx(times[s]) - 2.0f, sy(vals[s]) - 2.0f, 4.0f, 4.0f);
                        }
                    }

                    g.restoreState();
                }

                // Resize handle indicator at bottom of lane
                if (!st.resizeBarBounds.isEmpty())
                {
                    g.setColour(juce::Colour(0xff233240));
                    g.fillRect(st.resizeBarBounds);
                }
            }

            g.setColour(juce::Colour(0xff293846));
            g.drawRect(st.laneBounds, 1);
            g.drawVerticalLine(kLaneHeaderWidth, (float)st.laneBounds.getY(), (float)st.laneBounds.getBottom());
        }
    }

    void paintTimelineNavigator(juce::Graphics& g, const juce::Rectangle<int>& scrubberBounds)
    {
        g.fillAll(juce::Colour(0xff0e141b));
        const double win = visibleWindowSeconds();
        const double viewStart = viewStartTime_;
        const double viewEnd = viewStart + win;

        // Draw synchronized time axis ticks aligned with waveform area
        const int wfX = kLaneHeaderWidth + 4;
        const int wfW = std::max(40, timelineBar.getWidth() - kLaneHeaderWidth - 10);

        g.setFont(juce::Font(9.5f));
        g.setColour(juce::Colour(0xff9eb3c4));
        for (int div = 0; div <= kTimeDivisions; div += 2)
        {
            const double t = viewStart + win * ((double)div / (double)kTimeDivisions);
            const int x = wfX + (int)std::round((double)wfW * ((double)div / (double)kTimeDivisions));
            g.drawText(juce::String(t, secondsPerDiv_ < 0.05 ? 3 : 2) + "s",
                       juce::Rectangle<int>(x - 28, 0, 56, 11),
                       juce::Justification::centred, false);
        }

        if (scrubberBounds.getWidth() > 12)
        {
            const auto sb = scrubberBounds.withTrimmedTop(10).toFloat();
            g.setColour(juce::Colour(0xff16202a));
            g.fillRoundedRectangle(sb, 3.0f);
            g.setColour(juce::Colour(0xff2c3d4f));
            g.drawRoundedRectangle(sb, 3.0f, 1.0f);

            const double t0 = recording.time.empty() ? 0.0 : recording.time.front();
            const double tLatest = recording.time.empty() ? win : std::max(t0 + win, recording.time.back());
            const double totalSpan = std::max(1.0e-6, tLatest - t0);

            const float f0 = (float)std::clamp((viewStart - t0) / totalSpan, 0.0, 1.0);
            const float f1 = (float)std::clamp((viewEnd - t0) / totalSpan, 0.0, 1.0);
            const float thumbX = sb.getX() + f0 * sb.getWidth();
            const float thumbW = std::max(12.0f, (f1 - f0) * sb.getWidth());

            g.setColour(liveFollow_ ? juce::Colour(0xff35c6ff).withAlpha(0.45f)
                                    : juce::Colour(0xffffd166).withAlpha(0.55f));
            g.fillRoundedRectangle(juce::Rectangle<float>( std::min(thumbX, sb.getRight() - 12.0f), sb.getY() + 1.0f,
                                                           std::min(thumbW, sb.getWidth()), sb.getHeight() - 2.0f ), 2.5f);
        }
    }

    void handleLanesMouseDown(const juce::MouseEvent& e)
    {
        resizingLaneIndex_ = -1;
        panningWaveform_ = false;

        for (size_t i = 0; i < laneStates_.size(); ++i)
        {
            if (!laneStates_[i].resizeBarBounds.isEmpty() && laneStates_[i].resizeBarBounds.expanded(0, 3).contains(e.getPosition()))
            {
                resizingLaneIndex_ = (int)i;
                resizeStartHeight_ = config.plotChannels[i].laneHeight;
                resizeStartMouseY_ = e.y;
                return;
            }
            if (laneStates_[i].waveformBounds.contains(e.getPosition()))
            {
                panningWaveform_ = true;
                panStartMouseX_ = e.x;
                panStartViewTime_ = viewStartTime_;
                panWaveformWidth_ = std::max(40, laneStates_[i].waveformBounds.getWidth());
                return;
            }
        }
    }

    void handleLanesMouseDrag(const juce::MouseEvent& e)
    {
        if (resizingLaneIndex_ >= 0 && resizingLaneIndex_ < (int)config.plotChannels.size())
        {
            const int deltaY = e.y - resizeStartMouseY_;
            setLaneHeight(resizingLaneIndex_, resizeStartHeight_ + deltaY);
            return;
        }

        if (panningWaveform_)
        {
            const int dx = e.x - panStartMouseX_;
            const double dt = -((double)dx / (double)panWaveformWidth_) * visibleWindowSeconds();
            const double targetStartTime = panStartViewTime_ + dt;
            scrollHistoryToStartTime(targetStartTime);
        }
    }

    void handleLanesMouseUp(const juce::MouseEvent&)
    {
        resizingLaneIndex_ = -1;
        panningWaveform_ = false;
    }

    void handleLanesMouseMove(const juce::MouseEvent& e)
    {
        for (const auto& st : laneStates_)
        {
            if (!st.resizeBarBounds.isEmpty() && st.resizeBarBounds.expanded(0, 3).contains(e.getPosition()))
            {
                lanesContainer.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
                return;
            }
            if (st.waveformBounds.contains(e.getPosition()))
            {
                lanesContainer.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
                return;
            }
        }
        lanesContainer.setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void handleLanesMouseWheel(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (std::abs(wheel.deltaY) <= 1.0e-4f)
            return;

        int wfLeft = -1;
        int wfWidth = -1;
        bool overWaveform = false;

        for (const auto& st : laneStates_)
        {
            if (st.waveformBounds.contains(e.getPosition()))
            {
                overWaveform = true;
                wfLeft = st.waveformBounds.getX();
                wfWidth = st.waveformBounds.getWidth();
                break;
            }
        }

        if (!overWaveform)
        {
            for (const auto& st : laneStates_)
            {
                if (!st.waveformBounds.isEmpty() && e.x >= st.waveformBounds.getX() && e.x <= st.waveformBounds.getRight())
                {
                    overWaveform = true;
                    wfLeft = st.waveformBounds.getX();
                    wfWidth = st.waveformBounds.getWidth();
                    break;
                }
            }
        }

        if (overWaveform && wfWidth > 20)
        {
            const double factor = wheel.deltaY > 0.0f ? 0.8 : 1.25;
            zoomAtScreenX(e.x, factor, wfLeft, wfWidth);
        }
    }

    void chooseCsvExport()
    {
        chooser = std::make_unique<juce::FileChooser>("Export plot CSV", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                      "*.csv");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe = juce::Component::SafePointer<PlotPanelWidget>(this)](const juce::FileChooser& fc) {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;
            juce::String error;
            safe->recording.writeCsv(fc.getResult(), safe->config, error);
            safe->info.setText(error.isEmpty() ? "CSV exported" : error, juce::dontSendNotification);
        });
    }

    void chooseSessionSave()
    {
        chooser = std::make_unique<juce::FileChooser>("Save plot recording", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                      "*.plotrecording.json;*.json");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe = juce::Component::SafePointer<PlotPanelWidget>(this)](const juce::FileChooser& fc) {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;
            juce::String error;
            safe->recording.saveJson(fc.getResult(), safe->config, error);
            safe->info.setText(error.isEmpty() ? "Recording saved" : error, juce::dontSendNotification);
        });
    }

    WidgetConfig config;
    MachineRecording& recording;
    bool& recordEnabled;
    std::function<void(const WidgetConfig&)> configChanged;

    bool liveFollow_ = true;
    bool viewPaused_ = false;
    bool showMarkers_ = false;
    double secondsPerDiv_ = 1.0;
    double viewStartTime_ = 0.0;
    double viewCenterTime_ = 5.0;
    double frozenViewStartTime_ = 0.0;
    int frozenSampleCount_ = 0;

    int resizingLaneIndex_ = -1;
    int resizeStartHeight_ = kDefaultExpandedLaneHeight;
    int resizeStartMouseY_ = 0;
    bool panningWaveform_ = false;
    int panStartMouseX_ = 0;
    double panStartViewTime_ = 0.0;
    int panWaveformWidth_ = 200;

    juce::Label title, timebaseLabel, info;
    juce::ComboBox modeBox, timebaseBox;
    juce::ToggleButton recordToggle, liveFollowToggle, pauseDisplayToggle, markersToggle;
    juce::TextButton fitButton { "Fit" }, clearButton { "Clear" }, exportCsvButton { "CSV" }, saveSessionButton { "Save" };

    juce::Viewport lanesViewport;
    LanesContainerComponent lanesContainer;
    TimelineNavigatorComponent timelineBar;
    std::vector<LaneRuntimeState> laneStates_;
    std::vector<std::unique_ptr<juce::TextButton>> laneCollapseButtons_;
    std::vector<std::unique_ptr<juce::TextButton>> laneShorterButtons_;
    std::vector<std::unique_ptr<juce::TextButton>> laneTallerButtons_;

    PlotSurface xySurface;
    std::unique_ptr<juce::FileChooser> chooser;
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

    runButton.onClick = [this] {
        if (!session_)
            return;
        if (onEnsureCompiled)
            onEnsureCompiled();
        session_->start();
    };
    pauseButton.onClick = [this] { if (session_) session_->pause(); };
    stepButton.onClick = [this] {
        if (!session_)
            return;
        if (onEnsureCompiled)
            onEnsureCompiled();
        session_->stepSingle(0.005);
    };
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

    addAndMakeVisible(panelBox);
    panelBox.onChange = [this] {
        const int index = panelBox.getSelectedItemIndex();
        if (index >= 0 && index < (int)layout_.panels.size())
            setActivePanel(layout_.panels[(size_t)index].id);
    };

    auto setupPanelBtn = [this](juce::TextButton& btn) {
        addAndMakeVisible(btn);
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff22313d));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
    };
    setupPanelBtn(newPanelButton);
    setupPanelBtn(renamePanelButton);
    setupPanelBtn(removePanelButton);

    newPanelButton.onClick = [this] {
        createNewPanel();
    };
    renamePanelButton.onClick = [this] {
        if (auto* panel = layout_.findPanel(layout_.activePanelId))
        {
            auto* dialog = new juce::AlertWindow("Rename Panel", "Enter the operator panel name.",
                                                 juce::MessageBoxIconType::NoIcon);
            dialog->addTextEditor("title", panel->title.empty() ? juce::String(panel->id) : juce::String(panel->title), "Name");
            if (auto* editor = dialog->getTextEditor("title"))
            {
                editor->setWantsKeyboardFocus(true);
                editor->setMouseClickGrabsKeyboardFocus(true);
                editor->setEscapeAndReturnKeysConsumed(false);
                editor->selectAll();
                editor->grabKeyboardFocus();
            }
            dialog->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
            dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            dialog->enterModalState(true, juce::ModalCallbackFunction::create(
                [safe = juce::Component::SafePointer<ControlPanelComponent>(this), dialog](int result) {
                    std::unique_ptr<juce::AlertWindow> cleanup(dialog);
                    if (safe == nullptr || result != 1)
                        return;
                    const auto titleText = dialog->getTextEditorContents("title").trim();
                    if (titleText.isNotEmpty())
                        safe->renameActivePanel(titleText.toStdString());
                }), true);
        }
    };
    removePanelButton.onClick = [this] {
        removeActivePanel();
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

std::string ControlPanelComponent::createNewPanel(const std::string& title)
{
    layout_.ensureDefaultPanel();
    int n = (int)layout_.panels.size() + 1;
    OperatorPanelConfig panel;
    do
    {
        panel.id = "panel_" + juce::String(n).toStdString();
        panel.title = title.empty() ? ("Panel " + juce::String(n).toStdString()) : title;
        ++n;
    } while (layout_.hasPanel(panel.id));
    layout_.panels.push_back(panel);
    setActivePanel(panel.id);
    return panel.id;
}

bool ControlPanelComponent::renameActivePanel(const std::string& newTitle)
{
    if (newTitle.empty())
        return false;
    layout_.ensureDefaultPanel();
    if (auto* active = layout_.findPanel(layout_.activePanelId))
    {
        active->title = newTitle;
        rebuildPanelSelector();
        notifyLayoutChanged();
        return true;
    }
    return false;
}

bool ControlPanelComponent::removeActivePanel()
{
    layout_.ensureDefaultPanel();
    if (layout_.panels.size() <= 1)
        return false;
    const auto removed = layout_.activePanelId;
    layout_.panels.erase(std::remove_if(layout_.panels.begin(), layout_.panels.end(),
                                        [&](const auto& panel) { return panel.id == removed; }),
                         layout_.panels.end());
    const auto fallback = layout_.panels.front().id;
    for (auto& widget : layout_.widgets)
        if (widget.panelId == removed)
            widget.panelId = fallback;
    setActivePanel(fallback);
    return true;
}

int ControlPanelComponent::getPanelCount() const
{
    return (int)layout_.panels.size();
}

std::string ControlPanelComponent::getActivePanelId() const
{
    return layout_.activePanelId;
}

bool ControlPanelComponent::loadMachine(const machine_builder::MachineDesign& machine, std::string* error)
{
    activeNumericControlValues_ = machine.simConfig.initialNumericInputs;
    activeBooleanControlValues_ = machine.simConfig.initialBooleanInputs;
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
    if (notifyingLayout_)
        return;
    layout_ = layout;
    layout_.ensureDefaultPanel();
    rebuildPanelSelector();
    buildWidgets();
    resized();
}

void ControlPanelComponent::rebuildPanelSelector()
{
    panelBox.clear(juce::dontSendNotification);
    layout_.ensureDefaultPanel();
    int selected = 1;
    for (size_t i = 0; i < layout_.panels.size(); ++i)
    {
        const auto& panel = layout_.panels[i];
        panelBox.addItem(panel.title.empty() ? juce::String(panel.id) : juce::String(panel.title), (int)i + 1);
        if (panel.id == layout_.activePanelId)
            selected = (int)i + 1;
    }
    panelBox.setSelectedId(selected, juce::dontSendNotification);
}

void ControlPanelComponent::setActivePanel(const std::string& panelId)
{
    if (panelId.empty() || !layout_.hasPanel(panelId))
        return;
    const bool changed = (layout_.activePanelId != panelId);
    layout_.activePanelId = panelId;
    rebuildPanelSelector();
    buildWidgets();
    resized();
    if (changed)
        notifyLayoutChanged();
}

const OperatorPanelConfig& ControlPanelComponent::activePanel() const
{
    static const OperatorPanelConfig fallback;
    if (const auto* panel = layout_.findPanel(layout_.activePanelId))
        return *panel;
    return fallback;
}

void ControlPanelComponent::notifyLayoutChanged()
{
    if (onLayoutChanged && !notifyingLayout_)
    {
        notifyingLayout_ = true;
        onLayoutChanged(layout_);
        notifyingLayout_ = false;
    }
}

void ControlPanelComponent::updateWidgetConfig(const WidgetConfig& config)
{
    for (auto& widget : layout_.widgets)
    {
        if (widget.id == config.id)
        {
            const bool structuralChange = (widget.height != config.height
                                           || widget.column != config.column
                                           || widget.order != config.order
                                           || widget.plotChannels.size() != config.plotChannels.size());
            bool lanesHeightChange = false;
            if (!structuralChange && widget.plotChannels.size() == config.plotChannels.size())
            {
                for (size_t c = 0; c < widget.plotChannels.size(); ++c)
                {
                    if (widget.plotChannels[c].laneHeight != config.plotChannels[c].laneHeight
                        || widget.plotChannels[c].collapsed != config.plotChannels[c].collapsed)
                    {
                        lanesHeightChange = true;
                        break;
                    }
                }
            }

            widget = config;
            for (auto& ov : outputViews_)
                if (ov.config.id == config.id)
                    ov.config = config;

            if (structuralChange || lanesHeightChange)
                resized();

            notifyLayoutChanged();
            return;
        }
    }
}

MachineRecording& ControlPanelComponent::recordingFor(const std::string& widgetId)
{
    auto& ptr = plotRecordings_[widgetId];
    if (ptr == nullptr)
        ptr = std::make_unique<MachineRecording>();
    return *ptr;
}

bool& ControlPanelComponent::recordingEnabledFor(const std::string& widgetId)
{
    auto [it, inserted] = plotRecordingEnabled_.try_emplace(widgetId, true);
    juce::ignoreUnused(inserted);
    return it->second;
}

void ControlPanelComponent::buildWidgets()
{
    inputViews_.clear();
    outputViews_.clear();

    inputContainer = std::make_unique<juce::Component>();
    outputContainer = std::make_unique<juce::Component>();
    const auto panelId = layout_.activePanelId.empty() ? std::string("main") : layout_.activePanelId;

    for (const auto& w : layout_.widgets)
    {
        if (!w.panelId.empty() && w.panelId != panelId)
            continue;

        if (w.column == 0) // Inputs
        {
            if (w.type == WidgetType::PushButton)
            {
                auto btn = std::make_unique<juce::TextButton>(juce::String(w.title));
                const auto nameLower = juce::String(w.title).toLowerCase() + " " + juce::String(w.id).toLowerCase();
                juce::Colour btnCol(0xff223344);
                juce::Colour textCol(0xffe6f0f8);
                if (nameLower.contains("start"))
                {
                    btnCol = juce::Colour(0xff1b5e20); // industrial dark green
                    textCol = juce::Colour(0xffe8f5e9);
                }
                else if (nameLower.contains("stop"))
                {
                    btnCol = juce::Colour(0xff7f1d1d); // industrial stop red / maroon
                    textCol = juce::Colour(0xffffebee);
                }
                else if (nameLower.contains("reset"))
                {
                    btnCol = juce::Colour(0xff2b3a4a); // industrial slate
                    textCol = juce::Colour(0xffe3f2fd);
                }
                btn->setColour(juce::TextButton::buttonColourId, btnCol);
                btn->setColour(juce::TextButton::textColourOffId, textCol);
                btn->setTooltip(juce::String(w.title));
                const auto sig = w.signalName;
                btn->onStateChange = [this, btnPtr = btn.get(), sig] {
                    activeBooleanControlValues_[sig] = btnPtr->isDown();
                    if (session_)
                        session_->setBooleanInput(sig, btnPtr->isDown());
                };
                inputContainer->addAndMakeVisible(*btn);
                inputViews_.push_back({ w, std::move(btn) });
            }
            else if (w.type == WidgetType::ToggleSwitch)
            {
                auto tog = std::make_unique<ToggleSwitchComponent>(w.title);
                const auto sig = w.signalName;
                const bool initState = activeBooleanControlValues_.count(sig) != 0
                                     ? activeBooleanControlValues_[sig]
                                     : (w.defaultValue > 0.5);
                tog->setToggleState(initState, juce::dontSendNotification);
                tog->onClick = [this, togPtr = tog.get(), sig] {
                    activeBooleanControlValues_[sig] = togPtr->getToggleState();
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
                const auto sig = w.signalName;
                const double initVal = activeNumericControlValues_.count(sig) != 0
                                     ? activeNumericControlValues_[sig]
                                     : w.defaultValue;
                ctrl->slider.setValue(initVal, juce::dontSendNotification);
                ctrl->slider.onValueChange = [this, sldPtr = &ctrl->slider, sig] {
                    activeNumericControlValues_[sig] = sldPtr->getValue();
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
                estop->onStateChanged = [this, sig](bool latched) {
                    activeBooleanControlValues_[sig] = latched;
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
                cb->onChange = [this, cbPtr = cb.get(), sig] {
                    const double v = (double)(cbPtr->getSelectedId() - 1);
                    activeNumericControlValues_[sig] = v;
                    if (session_)
                        session_->setNumericInput(sig, v);
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
            else if (w.type == WidgetType::PlotPanel)
            {
                auto plot = std::make_unique<PlotPanelWidget>(
                    w, recordingFor(w.id), recordingEnabledFor(w.id),
                    [this](const WidgetConfig& config) { updateWidgetConfig(config); });
                outputContainer->addAndMakeVisible(*plot);
                outputViews_.push_back({ w, std::move(plot) });
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
    runButton.setBounds(topBar.removeFromLeft(56).reduced(2));
    pauseButton.setBounds(topBar.removeFromLeft(64).reduced(2));
    stepButton.setBounds(topBar.removeFromLeft(58).reduced(2));
    stopButton.setBounds(topBar.removeFromLeft(58).reduced(2));
    resetButton.setBounds(topBar.removeFromLeft(64).reduced(2));
    topBar.removeFromLeft(12);
    rateBox.setBounds(topBar.removeFromLeft(132).reduced(2));
    topBar.removeFromLeft(8);
    panelBox.setBounds(topBar.removeFromLeft(140).reduced(2));
    newPanelButton.setBounds(topBar.removeFromLeft(82).reduced(2));
    renamePanelButton.setBounds(topBar.removeFromLeft(72).reduced(2));
    removePanelButton.setBounds(topBar.removeFromLeft(74).reduced(2));
    topBar.removeFromLeft(12);
    statusLabel.setBounds(topBar);

    b.removeFromTop(8);

    const auto& panel = activePanel();
    const int gap = std::clamp(panel.spacing, 4, 12);
    const int inputCols = std::max(1, panel.controlColumns);
    const int outputCols = std::max(1, panel.instrumentColumns);

    // Operator controls: compact and only take what they need (~200-240px width),
    // giving strip-chart recorder and instruments ~75-80% of workspace.
    const int minControl = 180;
    const int minInstr = std::min(400, std::max(220, b.getWidth() / 3));
    const double ratio = panel.splitRatio > 0.0 ? panel.splitRatio : 0.22;
    const int desiredW = (int)std::round((double)b.getWidth() * juce::jlimit(0.15, 0.40, ratio));
    const int autoControlWidth = std::clamp(desiredW, 190, std::max(210, inputCols * 220));
    const int controlWidth = splitterX_ > 0
                           ? juce::jlimit(minControl, std::max(minControl, b.getWidth() - minInstr - gap), splitterX_)
                           : std::min(autoControlWidth, b.getWidth() - minInstr - gap);

    auto controlArea = b.removeFromLeft(controlWidth);
    b.removeFromLeft(gap);
    auto instrumentArea = b;

    inputViewport.setBounds(controlArea);
    outputViewport.setBounds(instrumentArea);
    if (splitterX_ > 0)
        splitterX_ = controlArea.getRight() - getLocalBounds().getX();

    // Layout controls in compact configurable columns.
    const int padX = 8;
    const int inputInnerW = std::max(80, controlArea.getWidth() - 4);
    const int availInputW = std::max(60, inputInnerW - padX * 2 - gap * (inputCols - 1));
    const int inputColW = std::max(60, availInputW / inputCols);
    std::vector<int> inputY((size_t)inputCols, 8);
    for (auto& iv : inputViews_)
    {
        const int col = juce::jlimit(0, inputCols - 1, iv.config.order % inputCols);
        int h = iv.config.height;
        if (iv.config.type == WidgetType::PushButton || iv.config.type == WidgetType::ToggleSwitch || iv.config.type == WidgetType::ModeSelector)
            h = (h <= 0 || h >= 45) ? 28 : h;
        else if (iv.config.type == WidgetType::Slider || iv.config.type == WidgetType::Knob)
            h = (h <= 0 || h >= 60) ? 36 : h;
        else if (iv.config.type == WidgetType::EmergencyStop)
            h = (h <= 0 || h >= 75) ? 50 : h;
        else
            h = (h <= 0 || h > 60) ? 32 : h;

        const int height = std::clamp(h, 24, 75);
        iv.component->setBounds(padX + col * (inputColW + gap), inputY[(size_t)col], inputColW, height);
        inputY[(size_t)col] += height + gap;
    }
    const auto inputHeight = *std::max_element(inputY.begin(), inputY.end()) + 8;
    inputContainer->setBounds(0, 0, inputInnerW, std::max(inputHeight, inputViewport.getHeight()));

    // Layout instruments/recorders.
    const int outPadX = 8;
    const int outputInnerW = std::max(120, instrumentArea.getWidth() - 4);
    const int availOutputW = std::max(80, outputInnerW - outPadX * 2 - gap * (outputCols - 1));
    const int outputColW = std::max(80, availOutputW / outputCols);
    std::vector<int> outputY((size_t)outputCols, 8);
    for (auto& ov : outputViews_)
    {
        const int col = juce::jlimit(0, outputCols - 1, ov.config.order % outputCols);
        int height = std::max(32, ov.config.height);
        if (ov.config.type == WidgetType::PlotPanel)
        {
            const int remainingH = std::max(380, outputViewport.getHeight() - outputY[(size_t)col] - 8);
            int preferredH = height;
            if (const auto* plot = dynamic_cast<const PlotPanelWidget*>(ov.component.get()))
                preferredH = plot->preferredRecorderHeight();
            height = std::max({ height, remainingH, preferredH });
        }
        ov.component->setBounds(outPadX + col * (outputColW + gap), outputY[(size_t)col], outputColW, height);
        outputY[(size_t)col] += height + gap;
    }
    const auto outputHeight = *std::max_element(outputY.begin(), outputY.end()) + 8;
    outputContainer->setBounds(0, 0, outputInnerW, std::max(outputHeight, outputViewport.getHeight()));
}

void ControlPanelComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff161a22));
    g.setColour(juce::Colour(0xff2b3442));
    g.drawRect(getLocalBounds(), 1.0f);
    const int effectiveSplit = splitterX_ > 0 ? splitterX_ : (inputViewport.getRight() + 2);
    g.setColour(juce::Colour(0xff314152));
    g.fillRect(effectiveSplit - 1, 44, 2, std::max(0, getHeight() - 52));
}

void ControlPanelComponent::mouseDown(const juce::MouseEvent& e)
{
    const int effectiveSplit = splitterX_ > 0 ? splitterX_ : (inputViewport.getRight() + 2);
    draggingSplitter_ = std::abs(e.x - effectiveSplit) <= 6 && e.y > 42;
    if (draggingSplitter_)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}

void ControlPanelComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (!draggingSplitter_)
        return;
    splitterX_ = juce::jlimit(180, std::max(181, getWidth() - 240), e.x);
    if (auto* panel = layout_.findPanel(layout_.activePanelId))
        panel->splitRatio = juce::jlimit(0.20, 0.80, (double)(splitterX_ - 8) / std::max(1, getWidth() - 16));
    resized();
    repaint();
}

void ControlPanelComponent::mouseUp(const juce::MouseEvent&)
{
    const bool changed = draggingSplitter_;
    draggingSplitter_ = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    if (changed)
        notifyLayoutChanged();
}

void ControlPanelComponent::sampleLatestNow()
{
    timerCallback();
}

std::vector<ControlPanelComponent::StripChartRecorderSnapshot> ControlPanelComponent::getStripChartRecorderSnapshots() const
{
    std::vector<StripChartRecorderSnapshot> out;
    for (const auto& ov : outputViews_)
    {
        if (const auto* plot = dynamic_cast<const PlotPanelWidget*>(ov.component.get()))
            out.push_back(plot->getSnapshot());
    }
    return out;
}

bool ControlPanelComponent::setRecorderTimebaseSecondsPerDiv(const std::string& widgetId, double secondsPerDiv)
{
    for (auto& ov : outputViews_)
    {
        if (auto* plot = dynamic_cast<PlotPanelWidget*>(ov.component.get()))
        {
            if (widgetId.empty() || plot->widgetId() == widgetId)
            {
                plot->setTimebaseSecondsPerDiv(secondsPerDiv);
                return true;
            }
        }
    }
    return false;
}

bool ControlPanelComponent::scrollRecorderHistory(const std::string& widgetId, double centerTimeSeconds)
{
    for (auto& ov : outputViews_)
    {
        if (auto* plot = dynamic_cast<PlotPanelWidget*>(ov.component.get()))
        {
            if (widgetId.empty() || plot->widgetId() == widgetId)
            {
                plot->scrollHistoryToCenter(centerTimeSeconds);
                return true;
            }
        }
    }
    return false;
}

bool ControlPanelComponent::setRecorderLiveFollow(const std::string& widgetId, bool liveFollow)
{
    for (auto& ov : outputViews_)
    {
        if (auto* plot = dynamic_cast<PlotPanelWidget*>(ov.component.get()))
        {
            if (widgetId.empty() || plot->widgetId() == widgetId)
            {
                plot->setLiveFollow(liveFollow);
                return true;
            }
        }
    }
    return false;
}

bool ControlPanelComponent::setRecorderViewPaused(const std::string& widgetId, bool paused)
{
    for (auto& ov : outputViews_)
    {
        if (auto* plot = dynamic_cast<PlotPanelWidget*>(ov.component.get()))
        {
            if (widgetId.empty() || plot->widgetId() == widgetId)
            {
                plot->setViewPaused(paused);
                return true;
            }
        }
    }
    return false;
}

bool ControlPanelComponent::setRecorderRecordingEnabled(const std::string& widgetId, bool enabled)
{
    for (auto& ov : outputViews_)
    {
        if (auto* plot = dynamic_cast<PlotPanelWidget*>(ov.component.get()))
        {
            if (widgetId.empty() || plot->widgetId() == widgetId)
            {
                plot->setRecordingEnabled(enabled);
                return true;
            }
        }
    }
    return false;
}

bool ControlPanelComponent::setRecorderLaneCollapsed(const std::string& widgetId, int laneIndex, bool collapsed)
{
    for (auto& ov : outputViews_)
    {
        if (auto* plot = dynamic_cast<PlotPanelWidget*>(ov.component.get()))
        {
            if (widgetId.empty() || plot->widgetId() == widgetId)
                return plot->setLaneCollapsed(laneIndex, collapsed);
        }
    }
    return false;
}

bool ControlPanelComponent::setRecorderLaneHeight(const std::string& widgetId, int laneIndex, int heightPx)
{
    for (auto& ov : outputViews_)
    {
        if (auto* plot = dynamic_cast<PlotPanelWidget*>(ov.component.get()))
        {
            if (widgetId.empty() || plot->widgetId() == widgetId)
                return plot->setLaneHeight(laneIndex, heightPx);
        }
    }
    return false;
}

bool ControlPanelComponent::zoomRecorderAtScreenX(const std::string& widgetId, int screenX, double factor)
{
    for (auto& ov : outputViews_)
    {
        if (auto* plot = dynamic_cast<PlotPanelWidget*>(ov.component.get()))
        {
            if (widgetId.empty() || plot->widgetId() == widgetId)
            {
                plot->zoomAtScreenX(screenX, factor);
                return true;
            }
        }
    }
    return false;
}

bool ControlPanelComponent::fitRecorderRecording(const std::string& widgetId)
{
    for (auto& ov : outputViews_)
    {
        if (auto* plot = dynamic_cast<PlotPanelWidget*>(ov.component.get()))
        {
            if (widgetId.empty() || plot->widgetId() == widgetId)
            {
                plot->fitRecording();
                return true;
            }
        }
    }
    return false;
}

bool ControlPanelComponent::exportRecorderCsv(const std::string& widgetId, const juce::File& file, juce::String& error) const
{
    for (const auto& ov : outputViews_)
    {
        if (const auto* plot = dynamic_cast<const PlotPanelWidget*>(ov.component.get()))
        {
            if (widgetId.empty() || plot->widgetId() == widgetId)
                return plot->exportCsvToFile(file, error);
        }
    }
    error = "Plot recorder widget not found.";
    return false;
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

    for (const auto& widget : layout_.widgets)
    {
        if (widget.type == WidgetType::PlotPanel && recordingEnabledFor(widget.id))
            recordingFor(widget.id).append(snap, widget);
    }

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
        else if (ov.config.type == WidgetType::PlotPanel)
        {
            if (auto* plot = dynamic_cast<PlotPanelWidget*>(ov.component.get()))
                plot->updateDisplay();
        }
    }
}

} // namespace sim_panel
