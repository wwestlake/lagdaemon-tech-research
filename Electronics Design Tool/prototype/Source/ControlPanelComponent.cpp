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

// Custom Analog Gauge Component Implementation
AnalogGaugeComponent::AnalogGaugeComponent(const std::string& title, const std::string& unit, double minVal, double maxVal,
                                           int precision,
                                           bool hasWarn, double warnMin, double warnMax,
                                           bool hasCrit, double critMin, double critMax)
    : title_(title), unit_(unit), minVal_(minVal),
      maxVal_(maxVal > minVal ? maxVal : minVal + 1.0),
      precision_(std::clamp(precision, 0, 4)),
      hasWarn_(hasWarn), warnMin_(warnMin), warnMax_(warnMax),
      hasCrit_(hasCrit), critMin_(critMin), critMax_(critMax)
{
    val_ = std::clamp(0.0, minVal_, maxVal_);
}

void AnalogGaugeComponent::setValue(double val)
{
    actualVal_ = val;
    val_ = std::clamp(val, minVal_, maxVal_);
    repaint();
}

void AnalogGaugeComponent::resetValue()
{
    actualVal_ = 0.0;
    val_ = std::clamp(0.0, minVal_, maxVal_);
    repaint();
}

void AnalogGaugeComponent::updateConfig(const WidgetConfig& cfg, bool resetVal)
{
    title_ = cfg.title;
    unit_ = cfg.unit;
    minVal_ = cfg.minValue;
    maxVal_ = (cfg.maxValue > cfg.minValue) ? cfg.maxValue : (cfg.minValue + 1.0);
    precision_ = std::clamp(cfg.precision, 0, 4);
    hasWarn_ = cfg.hasWarnRange;
    warnMin_ = cfg.warnMin;
    warnMax_ = cfg.warnMax;
    hasCrit_ = cfg.hasCritRange;
    critMin_ = cfg.critMin;
    critMax_ = cfg.critMax;

    if (resetVal)
    {
        actualVal_ = 0.0;
        val_ = std::clamp(0.0, minVal_, maxVal_);
    }
    else
    {
        val_ = std::clamp(actualVal_, minVal_, maxVal_);
    }
    repaint();
}

void AnalogGaugeComponent::setUnit(const std::string& unit)
{
    unit_ = unit;
    repaint();
}

void AnalogGaugeComponent::setTitle(const std::string& title)
{
    title_ = title;
    repaint();
}

void AnalogGaugeComponent::setRange(double minVal, double maxVal)
{
    minVal_ = minVal;
    maxVal_ = (maxVal > minVal) ? maxVal : (minVal + 1.0);
    val_ = std::clamp(actualVal_, minVal_, maxVal_);
    repaint();
}

void AnalogGaugeComponent::paint(juce::Graphics& g)
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

    auto toAngle = [&](double v) -> float {
        const double span = std::max(1e-9, maxVal_ - minVal_);
        const float norm = (float)std::clamp((v - minVal_) / span, 0.0, 1.0);
        return startAngle + norm * totalAngle;
    };

    // Warning band
    if (hasWarn_ && warnMax_ > warnMin_)
    {
        const float a0 = toAngle(warnMin_);
        const float a1 = toAngle(warnMax_);
        if (a1 > a0)
        {
            juce::Path warnArc;
            warnArc.addCentredArc(center.x, center.y, radius * 0.85f, radius * 0.85f, 0.0f, a0, a1, true);
            g.setColour(juce::Colour(0xfff39c12)); // amber
            g.strokePath(warnArc, juce::PathStrokeType(4.5f));
        }
    }

    // Critical band
    if (hasCrit_ && critMax_ > critMin_)
    {
        const float a0 = toAngle(critMin_);
        const float a1 = toAngle(critMax_);
        if (a1 > a0)
        {
            juce::Path critArc;
            critArc.addCentredArc(center.x, center.y, radius * 0.85f, radius * 0.85f, 0.0f, a0, a1, true);
            g.setColour(juce::Colour(0xffe74c3c)); // red
            g.strokePath(critArc, juce::PathStrokeType(4.5f));
        }
    }

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

    // Scale numbers at start and end of arc
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xff8a9ba8));
    auto formatScaleNum = [](double v, int prec) -> juce::String {
        if (std::abs(v - std::round(v)) < 1e-4)
            return juce::String((int)std::round(v));
        return juce::String(v, std::clamp(prec, 1, 2));
    };
    const float startCos = std::cos(startAngle - juce::MathConstants<float>::halfPi);
    const float startSin = std::sin(startAngle - juce::MathConstants<float>::halfPi);
    g.drawText(formatScaleNum(minVal_, precision_),
               (int)(center.x + startCos * (radius * 0.58f) - 25.0f),
               (int)(center.y + startSin * (radius * 0.58f) - 7.0f),
               50, 14, juce::Justification::centred);

    const float endCos = std::cos(endAngle - juce::MathConstants<float>::halfPi);
    const float endSin = std::sin(endAngle - juce::MathConstants<float>::halfPi);
    g.drawText(formatScaleNum(maxVal_, precision_),
               (int)(center.x + endCos * (radius * 0.58f) - 25.0f),
               (int)(center.y + endSin * (radius * 0.58f) - 7.0f),
               50, 14, juce::Justification::centred);

    // Title and Unit
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xff8b9bb0));
    g.drawText(juce::String(title_), center.x - 50.0f, center.y - radius * 0.45f, 100.0f, 16.0f, juce::Justification::centred);
    g.setFont(juce::Font(10.0f));
    g.drawText(juce::String(unit_), center.x - 50.0f, center.y + radius * 0.25f, 100.0f, 16.0f, juce::Justification::centred);

    // Digital Readout at bottom of dial
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision_) << actualVal_;
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));
    juce::Colour valCol(0xff00ffcc);
    if (hasCrit_ && actualVal_ >= critMin_ && actualVal_ <= critMax_)
        valCol = juce::Colour(0xffff4444);
    else if (hasWarn_ && actualVal_ >= warnMin_ && actualVal_ <= warnMax_)
        valCol = juce::Colour(0xffffbb00);
    g.setColour(valCol);
    g.drawText(ss.str(), center.x - 40.0f, center.y + radius * 0.45f, 80.0f, 16.0f, juce::Justification::centred);

    // Needle
    const double span = std::max(1e-9, maxVal_ - minVal_);
    const float norm = (float)std::clamp((val_ - minVal_) / span, 0.0, 1.0);
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

namespace
{

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

    void setLabel(const std::string& label) { label_ = label; repaint(); }
    void updateConfig(const WidgetConfig& cfg, bool resetVal = true)
    {
        label_ = cfg.title;
        if (resetVal)
            active_ = false;
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
    DigitalReadoutComponent(const std::string& label, const std::string& unit,
                            int precision = 1,
                            bool hasWarn = false, double warnMin = 0.0, double warnMax = 0.0,
                            bool hasCrit = false, double critMin = 0.0, double critMax = 0.0)
        : label_(label), unit_(unit), precision_(precision),
          hasWarn_(hasWarn), warnMin_(warnMin), warnMax_(warnMax),
          hasCrit_(hasCrit), critMin_(critMin), critMax_(critMax) {}

    void setText(const std::string& valText)
    {
        valText_ = valText;
        hasNumeric_ = false;
        repaint();
    }

    void setValue(double val)
    {
        val_ = val;
        hasNumeric_ = true;
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(precision_) << val;
        valText_ = ss.str();
        repaint();
    }

    void setBooleanValue(bool b)
    {
        val_ = b ? 1.0 : 0.0;
        hasNumeric_ = true;
        valText_ = b ? "TRUE / HIGH" : "FALSE / LOW";
        repaint();
    }

    void updateConfig(const WidgetConfig& cfg, bool resetVal = true)
    {
        label_ = cfg.title;
        unit_ = cfg.unit;
        precision_ = std::clamp(cfg.precision, 0, 4);
        hasWarn_ = cfg.hasWarnRange;
        warnMin_ = cfg.warnMin;
        warnMax_ = cfg.warnMax;
        hasCrit_ = cfg.hasCritRange;
        critMin_ = cfg.critMin;
        critMax_ = cfg.critMax;
        if (resetVal)
        {
            val_ = 0.0;
            hasNumeric_ = true;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(precision_) << 0.0;
            valText_ = ss.str();
        }
        else if (hasNumeric_)
        {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(precision_) << val_;
            valText_ = ss.str();
        }
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
        g.drawText(juce::String(label_), 8, 4, (int)b.getWidth() - 16, 16, juce::Justification::centredLeft);

        juce::Colour textCol(0xff00ffcc);
        if (hasNumeric_)
        {
            if (hasCrit_ && val_ >= critMin_ && val_ <= critMax_)
                textCol = juce::Colour(0xffff4444);
            else if (hasWarn_ && val_ >= warnMin_ && val_ <= warnMax_)
                textCol = juce::Colour(0xffffbb00);
        }

        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::bold));
        g.setColour(textCol);
        juce::String disp = juce::String(valText_);
        if (!unit_.empty())
            disp << " " << juce::String(unit_);
        g.drawText(disp, 8, 16, (int)b.getWidth() - 16, (int)b.getHeight() - 20, juce::Justification::centredRight);
    }

private:
    std::string label_, unit_, valText_ = "0.0";
    double val_ = 0.0;
    bool hasNumeric_ = false;
    int precision_ = 1;
    bool hasWarn_ = false;
    double warnMin_ = 0.0, warnMax_ = 0.0;
    bool hasCrit_ = false;
    double critMin_ = 0.0, critMax_ = 0.0;
};

// Custom Bar Gauge Component (Horizontal and Vertical)
class BarGaugeComponent final : public juce::Component
{
public:
    BarGaugeComponent(const std::string& title, const std::string& unit,
                      double minVal, double maxVal, bool vertical = false,
                      int precision = 1,
                      bool hasWarn = false, double warnMin = 0.0, double warnMax = 0.0,
                      bool hasCrit = false, double critMin = 0.0, double critMax = 0.0)
        : title_(title), unit_(unit), minVal_(minVal), maxVal_(maxVal), vertical_(vertical),
          precision_(precision), hasWarn_(hasWarn), warnMin_(warnMin), warnMax_(warnMax),
          hasCrit_(hasCrit), critMin_(critMin), critMax_(critMax) {}

    void setValue(double val)
    {
        actualVal_ = val;
        val_ = std::clamp(val, minVal_, maxVal_);
        repaint();
    }

    double getValue() const { return actualVal_; }

    void updateConfig(const WidgetConfig& cfg, bool resetVal = true)
    {
        title_ = cfg.title;
        unit_ = cfg.unit;
        minVal_ = cfg.minValue;
        maxVal_ = (cfg.maxValue > cfg.minValue) ? cfg.maxValue : (cfg.minValue + 1.0);
        vertical_ = cfg.vertical;
        precision_ = std::clamp(cfg.precision, 0, 4);
        hasWarn_ = cfg.hasWarnRange;
        warnMin_ = cfg.warnMin;
        warnMax_ = cfg.warnMax;
        hasCrit_ = cfg.hasCritRange;
        critMin_ = cfg.critMin;
        critMax_ = cfg.critMax;
        if (resetVal)
        {
            actualVal_ = 0.0;
            val_ = std::clamp(0.0, minVal_, maxVal_);
        }
        else
        {
            val_ = std::clamp(actualVal_, minVal_, maxVal_);
        }
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(juce::Colour(0xff12161c));
        g.fillRoundedRectangle(b, 4.0f);
        g.setColour(juce::Colour(0xff2a3442));
        g.drawRoundedRectangle(b, 4.0f, 1.2f);

        const double span = std::max(1e-9, maxVal_ - minVal_);
        const float norm = (float)std::clamp((val_ - minVal_) / span, 0.0, 1.0);

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(precision_) << actualVal_;
        if (!unit_.empty()) ss << " " << unit_;

        if (vertical_)
        {
            // Title & Readout at top
            g.setFont(juce::Font(10.5f, juce::Font::bold));
            g.setColour(juce::Colour(0xffc5d1de));
            g.drawText(juce::String(title_), b.getX() + 4.0f, b.getY() + 4.0f, b.getWidth() - 8.0f, 14.0f, juce::Justification::centred);

            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));
            g.setColour(juce::Colour(0xff00ffcc));
            g.drawText(ss.str(), b.getX() + 4.0f, b.getY() + 18.0f, b.getWidth() - 8.0f, 14.0f, juce::Justification::centred);

            // Vertical Bar Track
            const float trackW = std::clamp(b.getWidth() * 0.35f, 14.0f, 32.0f);
            const float trackX = b.getCentreX() - trackW * 0.5f;
            const float trackY = b.getY() + 36.0f;
            const float trackH = std::max(20.0f, b.getHeight() - 44.0f);
            const auto trackRect = juce::Rectangle<float>(trackX, trackY, trackW, trackH);

            g.setColour(juce::Colour(0xff0d1117));
            g.fillRect(trackRect);
            g.setColour(juce::Colour(0xff303d4e));
            g.drawRect(trackRect, 1.0f);

            // Fill from bottom up
            const float fillH = norm * trackH;
            const auto fillRect = juce::Rectangle<float>(trackX + 1.0f, trackRect.getBottom() - fillH, trackW - 2.0f, fillH);

            juce::Colour barCol(0xff2ecc71); // normal green
            if (hasCrit_ && val_ >= critMin_ && val_ <= critMax_)
                barCol = juce::Colour(0xffe74c3c); // red
            else if (hasWarn_ && val_ >= warnMin_ && val_ <= warnMax_)
                barCol = juce::Colour(0xfff39c12); // amber

            g.setColour(barCol);
            g.fillRect(fillRect);

            // Ticks
            g.setColour(juce::Colour(0xff556677));
            for (int i = 0; i <= 4; ++i)
            {
                float ty = trackRect.getBottom() - ((float)i / 4.0f) * trackH;
                g.drawHorizontalLine((int)ty, trackX - 4.0f, trackX);
            }
        }
        else
        {
            // Horizontal bar
            // Top: Title on left, Readout on right
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.setColour(juce::Colour(0xffc5d1de));
            g.drawText(juce::String(title_), b.getX() + 8.0f, b.getY() + 4.0f, b.getWidth() * 0.55f, 14.0f, juce::Justification::centredLeft);

            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::bold));
            g.setColour(juce::Colour(0xff00ffcc));
            g.drawText(ss.str(), b.getRight() - b.getWidth() * 0.40f - 8.0f, b.getY() + 4.0f, b.getWidth() * 0.40f, 14.0f, juce::Justification::centredRight);

            // Horizontal Track
            const float trackX = b.getX() + 8.0f;
            const float trackY = b.getY() + 20.0f;
            const float trackW = b.getWidth() - 16.0f;
            const float trackH = std::max(10.0f, b.getHeight() - 26.0f);
            const auto trackRect = juce::Rectangle<float>(trackX, trackY, trackW, trackH);

            g.setColour(juce::Colour(0xff0d1117));
            g.fillRect(trackRect);
            g.setColour(juce::Colour(0xff303d4e));
            g.drawRect(trackRect, 1.0f);

            // Fill from left to right
            const float fillW = norm * trackW;
            const auto fillRect = juce::Rectangle<float>(trackX + 1.0f, trackY + 1.0f, fillW, trackH - 2.0f);

            juce::Colour barCol(0xff2ecc71);
            if (hasCrit_ && val_ >= critMin_ && val_ <= critMax_)
                barCol = juce::Colour(0xffe74c3c);
            else if (hasWarn_ && val_ >= warnMin_ && val_ <= warnMax_)
                barCol = juce::Colour(0xfff39c12);

            g.setColour(barCol);
            g.fillRect(fillRect);

            // Ticks
            g.setColour(juce::Colour(0xff556677));
            for (int i = 0; i <= 4; ++i)
            {
                float tx = trackX + ((float)i / 4.0f) * trackW;
                g.drawVerticalLine((int)tx, trackRect.getBottom(), trackRect.getBottom() + 3.0f);
            }
        }
    }

private:
    std::string title_, unit_;
    double minVal_ = 0.0, maxVal_ = 100.0, val_ = 0.0, actualVal_ = 0.0;
    bool vertical_ = false;
    int precision_ = 1;
    bool hasWarn_ = false;
    double warnMin_ = 0.0, warnMax_ = 0.0;
    bool hasCrit_ = false;
    double critMin_ = 0.0, critMax_ = 0.0;
};

// Custom Indicator Button Component (Pushbutton with integrated status LED lamp)
class IndicatorButtonComponent final : public juce::Component
{
public:
    std::function<void(bool)> onStateChange;

    IndicatorButtonComponent(const std::string& label, bool latching = false)
        : label_(label), latching_(latching)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    bool isLatched() const { return latched_; }
    bool isButtonActive() const { return latching_ ? latched_ : isDown_; }
    void setLatched(bool l) { latched_ = l; repaint(); }
    void setIndicatorActive(bool a) { indicatorActive_ = a; repaint(); }

    void mouseDown(const juce::MouseEvent&) override
    {
        isDown_ = true;
        if (latching_)
            latched_ = !latched_;
        repaint();
        if (onStateChange)
            onStateChange(latching_ ? latched_ : true);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        isDown_ = false;
        repaint();
        if (!latching_ && onStateChange)
            onStateChange(false);
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(2.0f);
        const bool active = latching_ ? latched_ : isDown_;

        // Button body
        g.setColour(active ? juce::Colour(0xff1e3a5f) : juce::Colour(0xff22313d));
        g.fillRoundedRectangle(b, 4.0f);
        g.setColour(active ? juce::Colour(0xff4cc9f0) : juce::Colour(0xff3a4d5e));
        g.drawRoundedRectangle(b, 4.0f, active ? 1.5f : 1.0f);

        // Integrated indicator LED on left
        const float ledSize = 10.0f;
        const float ledX = b.getX() + 8.0f;
        const float ledY = b.getCentreY() - ledSize * 0.5f;
        const auto ledRect = juce::Rectangle<float>(ledX, ledY, ledSize, ledSize);

        if (indicatorActive_ || (latching_ && latched_))
        {
            g.setColour(juce::Colour(0xff2ecc71).withAlpha(0.35f));
            g.fillEllipse(ledRect.expanded(3.0f));
            g.setColour(juce::Colour(0xff2ecc71));
            g.fillEllipse(ledRect);
        }
        else
        {
            g.setColour(juce::Colour(0xff182028));
            g.fillEllipse(ledRect);
            g.setColour(juce::Colour(0xff3a4856));
            g.drawEllipse(ledRect, 1.0f);
        }

        // Label
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.setColour(active ? juce::Colour(0xffffffff) : juce::Colour(0xffdce9ee));
        g.drawText(juce::String(label_), (int)(ledX + ledSize + 8.0f), 0,
                   (int)(b.getWidth() - ledSize - 20.0f), (int)b.getHeight(),
                   juce::Justification::centredLeft);
    }

private:
    std::string label_;
    bool latching_ = false;
    bool latched_ = false;
    bool isDown_ = false;
    bool indicatorActive_ = false;
};

// Custom Multi-Position Selector Switch Component
class SelectorSwitchComponent final : public juce::Component
{
public:
    std::function<void(int)> onSelectionChanged;

    SelectorSwitchComponent(const std::string& title, const std::vector<std::string>& options)
        : title_(title), options_(options)
    {
        if (options_.empty())
            options_ = { "OFF", "ON" };
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    int getSelectedIndex() const { return selectedIdx_; }
    void setSelectedIndex(int idx)
    {
        if (idx >= 0 && idx < (int)options_.size())
        {
            selectedIdx_ = idx;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        selectedIdx_ = (selectedIdx_ + 1) % (int)options_.size();
        repaint();
        if (onSelectionChanged)
            onSelectionChanged(selectedIdx_);
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(juce::Colour(0xff1a222d));
        g.fillRoundedRectangle(b, 4.0f);
        g.setColour(juce::Colour(0xff324354));
        g.drawRoundedRectangle(b, 4.0f, 1.0f);

        // Title
        g.setFont(juce::Font(10.5f, juce::Font::bold));
        g.setColour(juce::Colour(0xff9eb3c4));
        g.drawText(juce::String(title_), (int)b.getX() + 6, (int)b.getY() + 3, (int)b.getWidth() - 12, 13, juce::Justification::centredLeft);

        // Selector rotary dial / knob on left
        const float knobSize = std::min(b.getHeight() - 16.0f, 26.0f);
        const float knobX = b.getX() + 8.0f;
        const float knobY = b.getY() + 16.0f + (b.getHeight() - 16.0f - knobSize) * 0.5f;
        const auto knobRect = juce::Rectangle<float>(knobX, knobY, knobSize, knobSize);

        g.setColour(juce::Colour(0xff2d3b4b));
        g.fillEllipse(knobRect);
        g.setColour(juce::Colour(0xff556a80));
        g.drawEllipse(knobRect, 1.2f);

        // Pointer
        const float angle = -1.57f + (float)selectedIdx_ / (float)std::max(1, (int)options_.size() - 1) * 3.14f;
        const float pX = knobRect.getCentreX() + std::cos(angle) * (knobSize * 0.4f);
        const float pY = knobRect.getCentreY() + std::sin(angle) * (knobSize * 0.4f);
        g.setColour(juce::Colour(0xff00ffcc));
        g.drawLine(knobRect.getCentreX(), knobRect.getCentreY(), pX, pY, 2.0f);

        // Option text
        const juce::String opt = (selectedIdx_ >= 0 && selectedIdx_ < (int)options_.size()) ? options_[(size_t)selectedIdx_] : "";
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xffffffff));
        g.drawText(opt, (int)(knobRect.getRight() + 8.0f), (int)knobY - 2,
                   (int)(b.getWidth() - knobRect.getRight() - 12.0f), (int)knobSize + 4,
                   juce::Justification::centredLeft);
    }

private:
    std::string title_;
    std::vector<std::string> options_;
    int selectedIdx_ = 0;
};

// Composite Labeled Slider & Knob Widget with Clear Title & Engineering Unit
class LabeledSliderComponent final : public juce::Component
{
public:
    LabeledSliderComponent(const std::string& title, const std::string& unit, bool isKnob, bool isVertical = false)
        : isKnob_(isKnob), isVertical_(isVertical)
    {
        juce::String labelText = juce::String(title);
        if (!unit.empty())
            labelText << " [" << juce::String(unit) << "]";

        addAndMakeVisible(label);
        label.setFont(juce::Font(11.5f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffc5d1de));
        label.setText(labelText, juce::dontSendNotification);

        addAndMakeVisible(slider);
        if (isKnob_)
        {
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 18);
        }
        else if (isVertical_)
        {
            slider.setSliderStyle(juce::Slider::LinearVertical);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 18);
        }
        else
        {
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 18);
        }
        if (!unit.empty())
            slider.setTextValueSuffix(" " + juce::String(unit));
    }

    void updateConfig(const WidgetConfig& cfg)
    {
        juce::String labelText = juce::String(cfg.title);
        if (!cfg.unit.empty())
            labelText << " [" << juce::String(cfg.unit) << "]";
        label.setText(labelText, juce::dontSendNotification);
        slider.setRange(cfg.minValue, cfg.maxValue, cfg.stepSize);
        if (!cfg.unit.empty())
            slider.setTextValueSuffix(" " + juce::String(cfg.unit));
        else
            slider.setTextValueSuffix({});
    }

    void resized() override
    {
        auto b = getLocalBounds();
        if (isKnob_)
        {
            label.setBounds(b.removeFromTop(15));
            slider.setBounds(b);
        }
        else if (isVertical_)
        {
            label.setBounds(b.removeFromTop(15));
            slider.setBounds(b.reduced(2, 0));
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
    bool isVertical_ = false;
};

} // namespace

std::string cleanSignalName(std::string name)
{
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t' || name.front() == '\r' || name.front() == '\n'))
        name.erase(name.begin());
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t' || name.back() == '\r' || name.back() == '\n'))
        name.pop_back();

    auto stripPrefix = [&](const std::string& pfx) {
        if (name.size() >= pfx.size())
        {
            bool match = true;
            for (size_t i = 0; i < pfx.size(); ++i)
            {
                if (std::tolower((unsigned char)name[i]) != std::tolower((unsigned char)pfx[i]))
                {
                    match = false;
                    break;
                }
            }
            if (match)
            {
                name = name.substr(pfx.size());
                while (!name.empty() && (name.front() == ' ' || name.front() == '\t'))
                    name.erase(name.begin());
                return true;
            }
        }
        return false;
    };

    stripPrefix("[out]");
    stripPrefix("[in]");
    stripPrefix("out:");
    stripPrefix("in:");
    stripPrefix("out ");
    stripPrefix("in ");
    return name;
}

bool resolveNumericSignal(const StateSnapshot& snap, const std::string& rawSignal, double& outVal)
{
    const std::string clean = cleanSignalName(rawSignal);
    if (clean.empty())
        return false;

    // 1. Direct exact match in numericSignals
    if (auto it = snap.numericSignals.find(clean); it != snap.numericSignals.end())
    {
        outVal = it->second;
        return true;
    }
    if (clean != rawSignal)
    {
        if (auto it = snap.numericSignals.find(rawSignal); it != snap.numericSignals.end())
        {
            outVal = it->second;
            return true;
        }
    }

    // 2. Case-insensitive match in numericSignals
    std::string cleanLower = clean;
    for (char& c : cleanLower) c = (char)std::tolower((unsigned char)c);

    for (const auto& [k, v] : snap.numericSignals)
    {
        std::string kLower = k;
        for (char& c : kLower) c = (char)std::tolower((unsigned char)c);
        if (kLower == cleanLower)
        {
            outVal = v;
            return true;
        }
    }

    // 3. Subsystem and suffix match
    const auto dotPos = cleanLower.rfind('.');
    const std::string prefix = (dotPos != std::string::npos) ? cleanLower.substr(0, dotPos) : std::string();
    const std::string suffix = (dotPos != std::string::npos) ? cleanLower.substr(dotPos + 1) : cleanLower;

    for (const auto& [k, v] : snap.numericSignals)
    {
        std::string kLower = k;
        for (char& c : kLower) c = (char)std::tolower((unsigned char)c);

        const auto kDot = kLower.rfind('.');
        if (kDot != std::string::npos)
        {
            const std::string kSuff = kLower.substr(kDot + 1);
            const std::string kPre = kLower.substr(0, kDot);
            if (kSuff == suffix)
            {
                if (prefix.empty() || kPre.find(prefix) != std::string::npos || prefix.find(kPre) != std::string::npos)
                {
                    outVal = v;
                    return true;
                }
            }
        }
        else if (kLower == suffix)
        {
            outVal = v;
            return true;
        }
    }

    // Second pass: general suffix matching for common signals (e.g. speed, angle, current, voltage, tach)
    if (suffix == "speed" || suffix == "angle" || suffix == "current" || suffix == "voltage" || suffix == "tach")
    {
        for (const auto& [k, v] : snap.numericSignals)
        {
            std::string kLower = k;
            for (char& c : kLower) c = (char)std::tolower((unsigned char)c);
            if (kLower.size() > suffix.size() + 1 && kLower.rfind("." + suffix) == kLower.size() - suffix.size() - 1)
            {
                outVal = v;
                return true;
            }
        }
    }

    // 4. Boolean signals fallback
    if (auto it = snap.booleanSignals.find(clean); it != snap.booleanSignals.end())
    {
        outVal = it->second ? 1.0 : 0.0;
        return true;
    }
    for (const auto& [k, v] : snap.booleanSignals)
    {
        std::string kLower = k;
        for (char& c : kLower) c = (char)std::tolower((unsigned char)c);
        if (kLower == cleanLower)
        {
            outVal = v ? 1.0 : 0.0;
            return true;
        }
    }

    return false;
}

bool resolveBooleanSignal(const StateSnapshot& snap, const std::string& rawSignal, bool& outVal)
{
    const std::string clean = cleanSignalName(rawSignal);
    if (clean.empty())
        return false;

    // 1. Exact match in booleanSignals
    if (auto it = snap.booleanSignals.find(clean); it != snap.booleanSignals.end())
    {
        outVal = it->second;
        return true;
    }
    if (clean != rawSignal)
    {
        if (auto it = snap.booleanSignals.find(rawSignal); it != snap.booleanSignals.end())
        {
            outVal = it->second;
            return true;
        }
    }

    // 2. Case-insensitive match in booleanSignals
    std::string cleanLower = clean;
    for (char& c : cleanLower) c = (char)std::tolower((unsigned char)c);

    for (const auto& [k, v] : snap.booleanSignals)
    {
        std::string kLower = k;
        for (char& c : kLower) c = (char)std::tolower((unsigned char)c);
        if (kLower == cleanLower)
        {
            outVal = v;
            return true;
        }
    }

    // 3. Suffix match in booleanSignals
    const auto dotPos = cleanLower.rfind('.');
    const std::string suffix = (dotPos != std::string::npos) ? cleanLower.substr(dotPos + 1) : cleanLower;
    for (const auto& [k, v] : snap.booleanSignals)
    {
        std::string kLower = k;
        for (char& c : kLower) c = (char)std::tolower((unsigned char)c);
        if (kLower == suffix || (kLower.size() > suffix.size() + 1 && kLower.rfind("." + suffix) == kLower.size() - suffix.size() - 1))
        {
            outVal = v;
            return true;
        }
    }

    // 4. Numeric signals fallback (> 0.5)
    double numVal = 0.0;
    if (resolveNumericSignal(snap, rawSignal, numVal))
    {
        outVal = (numVal > 0.5);
        return true;
    }

    return false;
}

namespace
{

double signalValue(const StateSnapshot& snap, const std::string& signal, bool* found = nullptr)
{
    double val = 0.0;
    if (resolveNumericSignal(snap, signal, val))
    {
        if (found != nullptr) *found = true;
        return val;
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
        layoutLanes();
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

    void mouseDown(const juce::MouseEvent&) override {}
    void mouseDrag(const juce::MouseEvent&) override {}
    void mouseUp(const juce::MouseEvent&) override {}

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (lanesViewport.isVisible() && lanesViewport.getBounds().contains(e.getPosition()))
        {
            lanesContainer.mouseWheelMove(e.getEventRelativeTo(&lanesContainer), wheel);
            return;
        }
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

        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}

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
        else
        {
            const int curY = lanesViewport.getViewPositionY();
            const int delta = (int)std::round(wheel.deltaY * -40.0f);
            lanesViewport.setViewPosition(lanesViewport.getViewPositionX(), std::max(0, curY + delta));
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

// -----------------------------------------------------------------------------
// Freeform Canvas & Item Wrapper
// -----------------------------------------------------------------------------

class DesignItemWrapper final : public juce::Component
{
public:
    enum class HitZone
    {
        None,
        Body,
        TopLeft, TopMid, TopRight,
        MidLeft, MidRight,
        BottomLeft, BottomMid, BottomRight
    };

    WidgetConfig config;
    std::unique_ptr<juce::Component> widget;
    bool isSelected = false;
    bool isDesignMode = false;

    std::function<void(DesignItemWrapper*, const juce::MouseEvent&)> onMouseDownAction;
    std::function<void(DesignItemWrapper*, const juce::MouseEvent&)> onMouseDragAction;
    std::function<void(DesignItemWrapper*, const juce::MouseEvent&)> onMouseUpAction;
    std::function<void(DesignItemWrapper*)> onOpenProperties;
    std::function<void(DesignItemWrapper*)> onDelete;
    std::function<void(DesignItemWrapper*)> onDuplicate;
    std::function<void(DesignItemWrapper*)> onBringToFront;
    std::function<void(DesignItemWrapper*)> onSendToBack;

    DesignItemWrapper(WidgetConfig cfg, std::unique_ptr<juce::Component> comp)
        : config(std::move(cfg)), widget(std::move(comp))
    {
        if (widget)
            addAndMakeVisible(*widget);
        updateMouseInterception();
    }

    juce::Component* getWidget() const { return widget.get(); }

    void setDesignMode(bool dm)
    {
        isDesignMode = dm;
        updateMouseInterception();
        repaint();
    }

    void setSelected(bool s)
    {
        isSelected = s;
        repaint();
    }

    void updateMouseInterception()
    {
        if (widget)
            widget->setInterceptsMouseClicks(!isDesignMode, !isDesignMode);
    }

    void resized() override
    {
        if (widget)
            widget->setBounds(getLocalBounds());
    }

    HitZone getHitZone(juce::Point<int> pos) const
    {
        const int h = 8;
        const int w = getWidth();
        const int ht = getHeight();

        if (pos.x <= h && pos.y <= h) return HitZone::TopLeft;
        if (pos.x >= w - h && pos.y <= h) return HitZone::TopRight;
        if (pos.x <= h && pos.y >= ht - h) return HitZone::BottomLeft;
        if (pos.x >= w - h && pos.y >= ht - h) return HitZone::BottomRight;

        if (pos.y <= h && std::abs(pos.x - w / 2) <= h) return HitZone::TopMid;
        if (pos.y >= ht - h && std::abs(pos.x - w / 2) <= h) return HitZone::BottomMid;
        if (pos.x <= h && std::abs(pos.y - ht / 2) <= h) return HitZone::MidLeft;
        if (pos.x >= w - h && std::abs(pos.y - ht / 2) <= h) return HitZone::MidRight;

        return HitZone::Body;
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (!isDesignMode) return;
        const auto zone = getHitZone(e.getPosition());
        switch (zone)
        {
            case HitZone::TopLeft:
            case HitZone::BottomRight:
                setMouseCursor(juce::MouseCursor::TopLeftCornerResizeCursor);
                break;
            case HitZone::TopRight:
            case HitZone::BottomLeft:
                setMouseCursor(juce::MouseCursor::TopRightCornerResizeCursor);
                break;
            case HitZone::TopMid:
            case HitZone::BottomMid:
                setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
                break;
            case HitZone::MidLeft:
            case HitZone::MidRight:
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                break;
            case HitZone::Body:
            default:
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
                break;
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem("Properties & Connection...", [this] { if (onOpenProperties) onOpenProperties(this); });
            menu.addSeparator();
            menu.addItem("Bring to Front", [this] { if (onBringToFront) onBringToFront(this); });
            menu.addItem("Send to Back", [this] { if (onSendToBack) onSendToBack(this); });
            menu.addSeparator();
            menu.addItem("Duplicate", [this] { if (onDuplicate) onDuplicate(this); });
            menu.addItem("Delete", [this] { if (onDelete) onDelete(this); });
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
            return;
        }

        if (!isDesignMode) return;

        activeDragZone_ = getHitZone(e.getPosition());
        startDragBounds_ = getBounds();
        mouseDownPos_ = e.getEventRelativeTo(getParentComponent()).getPosition();

        if (onMouseDownAction)
            onMouseDownAction(this, e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!isDesignMode) return;

        const auto curPos = e.getEventRelativeTo(getParentComponent()).getPosition();
        const int dx = curPos.x - mouseDownPos_.x;
        const int dy = curPos.y - mouseDownPos_.y;

        if (activeDragZone_ == HitZone::Body)
        {
            if (onMouseDragAction)
                onMouseDragAction(this, e);
        }
        else
        {
            auto nb = startDragBounds_;
            switch (activeDragZone_)
            {
                case HitZone::BottomRight:
                    nb.setWidth(std::max(30, startDragBounds_.getWidth() + dx));
                    nb.setHeight(std::max(24, startDragBounds_.getHeight() + dy));
                    break;
                case HitZone::BottomMid:
                    nb.setHeight(std::max(24, startDragBounds_.getHeight() + dy));
                    break;
                case HitZone::MidRight:
                    nb.setWidth(std::max(30, startDragBounds_.getWidth() + dx));
                    break;
                case HitZone::TopLeft:
                {
                    const int nw = std::max(30, startDragBounds_.getWidth() - dx);
                    const int nh = std::max(24, startDragBounds_.getHeight() - dy);
                    nb.setX(startDragBounds_.getRight() - nw);
                    nb.setY(startDragBounds_.getBottom() - nh);
                    nb.setWidth(nw);
                    nb.setHeight(nh);
                    break;
                }
                case HitZone::TopMid:
                {
                    const int nh = std::max(24, startDragBounds_.getHeight() - dy);
                    nb.setY(startDragBounds_.getBottom() - nh);
                    nb.setHeight(nh);
                    break;
                }
                case HitZone::TopRight:
                {
                    const int nh = std::max(24, startDragBounds_.getHeight() - dy);
                    nb.setY(startDragBounds_.getBottom() - nh);
                    nb.setWidth(std::max(30, startDragBounds_.getWidth() + dx));
                    nb.setHeight(nh);
                    break;
                }
                case HitZone::MidLeft:
                {
                    const int nw = std::max(30, startDragBounds_.getWidth() - dx);
                    nb.setX(startDragBounds_.getRight() - nw);
                    nb.setWidth(nw);
                    break;
                }
                case HitZone::BottomLeft:
                {
                    const int nw = std::max(30, startDragBounds_.getWidth() - dx);
                    nb.setX(startDragBounds_.getRight() - nw);
                    nb.setWidth(nw);
                    nb.setHeight(std::max(24, startDragBounds_.getHeight() + dy));
                    break;
                }
                default:
                    break;
            }
            setBounds(nb);
            config.x = nb.getX();
            config.y = nb.getY();
            config.width = nb.getWidth();
            config.height = nb.getHeight();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!isDesignMode) return;
        activeDragZone_ = HitZone::None;
        if (onMouseUpAction)
            onMouseUpAction(this, e);
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (isDesignMode && onOpenProperties)
            onOpenProperties(this);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (!isDesignMode) return;

        if (isSelected)
        {
            g.setColour(juce::Colour(0xff00d4ff));
            g.drawRect(getLocalBounds(), 1.5f);

            const int s = 6;
            const int w = getWidth();
            const int ht = getHeight();
            auto drawHandle = [&](int cx, int cy) {
                g.setColour(juce::Colours::white);
                g.fillRect(cx - s / 2, cy - s / 2, s, s);
                g.setColour(juce::Colour(0xff0088cc));
                g.drawRect(cx - s / 2, cy - s / 2, s, s, 1);
            };

            drawHandle(3, 3);
            drawHandle(w / 2, 3);
            drawHandle(w - 3, 3);
            drawHandle(3, ht / 2);
            drawHandle(w - 3, ht / 2);
            drawHandle(3, ht - 3);
            drawHandle(w / 2, ht - 3);
            drawHandle(w - 3, ht - 3);
        }
        else
        {
            if (isMouseOver())
            {
                g.setColour(juce::Colour(0x6600d4ff));
                g.drawRect(getLocalBounds(), 1.0f);
            }
        }
    }

private:
    HitZone activeDragZone_ = HitZone::None;
    juce::Rectangle<int> startDragBounds_;
    juce::Point<int> mouseDownPos_;
};

} // namespace

// Properties and Connection Picker Dialog Implementation
WidgetPropertiesDialog::WidgetPropertiesDialog(WidgetConfig config, std::vector<SignalDef> availableSignals,
                                               std::vector<OperatorPanelConfig> panels,
                                               std::function<void(const WidgetConfig&)> onApplyCallback)
    : cfg_(std::move(config)), signals_(std::move(availableSignals)), panels_(std::move(panels)), onApply_(std::move(onApplyCallback))
{
    titleEd_.setText(juce::String(cfg_.title));
    unitEd_.setText(juce::String(cfg_.unit));
    minEd_.setText(juce::String(cfg_.minValue, 2));
    maxEd_.setText(juce::String(cfg_.maxValue, 2));
    defEd_.setText(juce::String(cfg_.defaultValue, 2));
    stepEd_.setText(juce::String(cfg_.stepSize, 2));
    precBox_.addItem("0 Decimals", 1);
    precBox_.addItem("1 Decimal", 2);
    precBox_.addItem("2 Decimals", 3);
    precBox_.addItem("3 Decimals", 4);
    precBox_.setSelectedId(std::clamp(cfg_.precision, 0, 3) + 1, juce::dontSendNotification);

    int matchPanelId = 1;
    const auto activePanel = cfg_.panelId.empty() ? std::string("main") : cfg_.panelId;
    for (size_t i = 0; i < panels_.size(); ++i)
    {
        const auto& p = panels_[i];
        panelPickerBox_.addItem(p.title.empty() ? juce::String(p.id) : juce::String(p.title), (int)i + 1);
        if (p.id == activePanel)
            matchPanelId = (int)i + 1;
    }
    panelPickerBox_.setSelectedId(matchPanelId, juce::dontSendNotification);

    sigEd_.setText(juce::String(cfg_.signalName));
    secSigEd_.setText(juce::String(cfg_.secondarySignal));

    momentaryBtn_.setButtonText("Momentary Action (Hold to activate)");
    momentaryBtn_.setToggleState(cfg_.momentary, juce::dontSendNotification);

    verticalBtn_.setButtonText("Vertical Orientation");
    verticalBtn_.setToggleState(cfg_.vertical, juce::dontSendNotification);

    hasWarnBtn_.setButtonText("Warning Range Zone");
    hasWarnBtn_.setToggleState(cfg_.hasWarnRange, juce::dontSendNotification);
    warnMinEd_.setText(juce::String(cfg_.warnMin, 2));
    warnMaxEd_.setText(juce::String(cfg_.warnMax, 2));

    hasCritBtn_.setButtonText("Critical Range Zone");
    hasCritBtn_.setToggleState(cfg_.hasCritRange, juce::dontSendNotification);
    critMinEd_.setText(juce::String(cfg_.critMin, 2));
    critMaxEd_.setText(juce::String(cfg_.critMax, 2));

    sigPickerBox_.addItem("-- Select from Available Simulation Connections --", 1);
    int matchId = 1;
    const auto cleanCfgSig = cleanSignalName(cfg_.signalName);
    for (size_t i = 0; i < signals_.size(); ++i)
    {
        const auto& s = signals_[i];
        juce::String itemText;
        itemText << (s.isOutput ? "[OUT] " : "[IN] ") << juce::String(s.name);
        if (!s.unit.empty())
            itemText << " (" << juce::String(s.unit) << ")";
        if (!s.description.empty())
            itemText << " - " << juce::String(s.description);
        sigPickerBox_.addItem(itemText, (int)i + 2);
        if (s.name == cfg_.signalName || cleanSignalName(s.name) == cleanCfgSig)
            matchId = (int)i + 2;
    }
    sigPickerBox_.setSelectedId(matchId, juce::dontSendNotification);
    sigPickerBox_.onChange = [this] {
        const int id = sigPickerBox_.getSelectedId();
        if (id >= 2 && id - 2 < (int)signals_.size())
        {
            const auto& s = signals_[(size_t)(id - 2)];
            sigEd_.setText(juce::String(s.name));
            if (!s.unit.empty())
                unitEd_.setText(juce::String(s.unit));
            if (s.maxValue > s.minValue)
            {
                minEd_.setText(juce::String(s.minValue, 1));
                maxEd_.setText(juce::String(s.maxValue, 1));
            }
            updateCompatibilityLabel(s);
        }
    };

    sigEd_.onTextChange = [this] {
        const auto cleaned = cleanSignalName(sigEd_.getText().toStdString());
        int match = 1;
        for (size_t i = 0; i < signals_.size(); ++i)
        {
            if (signals_[i].name == cleaned || cleanSignalName(signals_[i].name) == cleaned)
            {
                match = (int)i + 2;
                break;
            }
        }
        sigPickerBox_.setSelectedId(match, juce::dontSendNotification);
        if (match >= 2 && match - 2 < (int)signals_.size())
            updateCompatibilityLabel(signals_[(size_t)(match - 2)]);
        else
            compatLabel_.setText("Custom / Unbound signal identifier.", juce::dontSendNotification);
    };

    compatLabel_.setFont(juce::Font(11.0f, juce::Font::bold));
    if (matchId >= 2 && matchId - 2 < (int)signals_.size())
        updateCompatibilityLabel(signals_[(size_t)(matchId - 2)]);
    else
        compatLabel_.setText("Custom / Unbound signal identifier.", juce::dontSendNotification);

    auto addField = [this](juce::Label& lbl, const juce::String& text, juce::Component& comp) {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setFont(juce::Font(11.5f, juce::Font::bold));
        lbl.setColour(juce::Label::textColourId, juce::Colour(0xffc5d1de));
        addAndMakeVisible(lbl);
        addAndMakeVisible(comp);
    };

    addField(lblTitle_, "Title / Label:", titleEd_);
    addField(lblPanelPicker_, "Target Panel:", panelPickerBox_);
    addField(lblUnit_, "Unit:", unitEd_);
    addField(lblMin_, "Min Value:", minEd_);
    addField(lblMax_, "Max Value:", maxEd_);
    addField(lblDef_, "Default:", defEd_);
    addField(lblStep_, "Step Size:", stepEd_);
    addField(lblPrec_, "Precision:", precBox_);
    addField(lblSigPicker_, "Connection Picker:", sigPickerBox_);
    addField(lblSig_, "Signal Name:", sigEd_);
    addField(lblSecSig_, "Secondary Signal:", secSigEd_);

    addAndMakeVisible(compatLabel_);
    addAndMakeVisible(momentaryBtn_);
    addAndMakeVisible(verticalBtn_);
    addAndMakeVisible(hasWarnBtn_);
    addAndMakeVisible(warnMinEd_);
    addAndMakeVisible(warnMaxEd_);
    addAndMakeVisible(hasCritBtn_);
    addAndMakeVisible(critMinEd_);
    addAndMakeVisible(critMaxEd_);

    applyBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b5e20));
    applyBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    applyBtn_.onClick = [this] {
        cfg_.title = titleEd_.getText().toStdString();
        const int selPanel = panelPickerBox_.getSelectedId();
        if (selPanel >= 1 && selPanel <= (int)panels_.size())
            cfg_.panelId = panels_[(size_t)(selPanel - 1)].id;
        cfg_.unit = unitEd_.getText().toStdString();
        cfg_.minValue = minEd_.getText().getDoubleValue();
        cfg_.maxValue = maxEd_.getText().getDoubleValue();
        cfg_.defaultValue = defEd_.getText().getDoubleValue();
        cfg_.stepSize = std::max(1e-6, stepEd_.getText().getDoubleValue());
        cfg_.precision = precBox_.getSelectedId() - 1;
        const auto oldSig = cfg_.signalName;
        cfg_.signalName = cleanSignalName(sigEd_.getText().toStdString());
        if (cfg_.unit.empty())
        {
            if (cfg_.signalName.find("speed") != std::string::npos) cfg_.unit = "rad/s";
            else if (cfg_.signalName.find("torque") != std::string::npos || cfg_.signalName.find("load") != std::string::npos) cfg_.unit = "N*m";
            else if (cfg_.signalName.find("voltage") != std::string::npos || cfg_.signalName.find("drive") != std::string::npos || cfg_.signalName.find("tach") != std::string::npos) cfg_.unit = "V";
            else if (cfg_.signalName.find("current") != std::string::npos) cfg_.unit = "A";
            else if (cfg_.signalName.find("angle") != std::string::npos) cfg_.unit = "rad";
        }
        else if (oldSig != cfg_.signalName)
        {
            // If the signal was rebound, prevent stale unit from old signal
            if (cfg_.signalName.find("speed") != std::string::npos && (cfg_.unit == "A" || cfg_.unit == "V" || cfg_.unit == "N*m"))
                cfg_.unit = "rad/s";
            else if (cfg_.signalName.find("current") != std::string::npos && (cfg_.unit == "rad/s" || cfg_.unit == "V" || cfg_.unit == "N*m"))
                cfg_.unit = "A";
            else if (cfg_.signalName.find("torque") != std::string::npos && (cfg_.unit == "A" || cfg_.unit == "rad/s" || cfg_.unit == "V"))
                cfg_.unit = "N*m";
            else if ((cfg_.signalName.find("voltage") != std::string::npos || cfg_.signalName.find("tach") != std::string::npos) && (cfg_.unit == "A" || cfg_.unit == "rad/s" || cfg_.unit == "N*m"))
                cfg_.unit = "V";
        }
        cfg_.secondarySignal = cleanSignalName(secSigEd_.getText().toStdString());
        cfg_.momentary = momentaryBtn_.getToggleState();
        cfg_.vertical = verticalBtn_.getToggleState();
        cfg_.hasWarnRange = hasWarnBtn_.getToggleState();
        cfg_.warnMin = warnMinEd_.getText().getDoubleValue();
        cfg_.warnMax = warnMaxEd_.getText().getDoubleValue();
        cfg_.hasCritRange = hasCritBtn_.getToggleState();
        cfg_.critMin = critMinEd_.getText().getDoubleValue();
        cfg_.critMax = critMaxEd_.getText().getDoubleValue();

        if (onApply_)
            onApply_(cfg_);

        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(1);
    };
    addAndMakeVisible(applyBtn_);

    cancelBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff445261));
    cancelBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    cancelBtn_.onClick = [this] {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(cancelBtn_);
}

void WidgetPropertiesDialog::setTitleText(const juce::String& text) { titleEd_.setText(text); }
void WidgetPropertiesDialog::setUnitText(const juce::String& text) { unitEd_.setText(text); }
void WidgetPropertiesDialog::setMinText(const juce::String& text) { minEd_.setText(text); }
void WidgetPropertiesDialog::setMaxText(const juce::String& text) { maxEd_.setText(text); }
void WidgetPropertiesDialog::setSignalText(const juce::String& text)
{
    sigEd_.setText(text);
    const auto s = text.toStdString();
    if (s.find("speed") != std::string::npos) unitEd_.setText("rad/s");
    else if (s.find("current") != std::string::npos) unitEd_.setText("A");
    else if (s.find("voltage") != std::string::npos) unitEd_.setText("V");
    else if (s.find("torque") != std::string::npos) unitEd_.setText("N*m");
}
void WidgetPropertiesDialog::setPrecisionIndex(int index) { precBox_.setSelectedId(index, juce::sendNotificationSync); }
void WidgetPropertiesDialog::setWarningZone(bool enabled, double minVal, double maxVal)
{
    hasWarnBtn_.setToggleState(enabled, juce::dontSendNotification);
    warnMinEd_.setText(juce::String(minVal, 2));
    warnMaxEd_.setText(juce::String(maxVal, 2));
}
void WidgetPropertiesDialog::setCriticalZone(bool enabled, double minVal, double maxVal)
{
    hasCritBtn_.setToggleState(enabled, juce::dontSendNotification);
    critMinEd_.setText(juce::String(minVal, 2));
    critMaxEd_.setText(juce::String(maxVal, 2));
}
void WidgetPropertiesDialog::selectSignalByName(const std::string& sigName)
{
    const auto cleaned = cleanSignalName(sigName);
    bool found = false;
    for (size_t i = 0; i < signals_.size(); ++i)
    {
        if (signals_[i].name == cleaned || cleanSignalName(signals_[i].name) == cleaned)
        {
            sigPickerBox_.setSelectedId((int)i + 2, juce::sendNotificationSync);
            found = true;
            break;
        }
    }
    if (!found)
    {
        sigEd_.setText(sigName);
        if (sigName.find("speed") != std::string::npos) { unitEd_.setText("rad/s"); maxEd_.setText("600.0"); }
        else if (sigName.find("current") != std::string::npos) { unitEd_.setText("A"); minEd_.setText("-20.0"); maxEd_.setText("20.0"); }
        else if (sigName.find("voltage") != std::string::npos) { unitEd_.setText("V"); minEd_.setText("0.0"); maxEd_.setText("24.0"); }
        else if (sigName.find("torque") != std::string::npos) { unitEd_.setText("N*m"); minEd_.setText("0.0"); maxEd_.setText("0.05"); }
    }
}
void WidgetPropertiesDialog::clickApply()
{
    if (applyBtn_.onClick)
        applyBtn_.onClick();
    else
        applyBtn_.triggerClick();
}

void WidgetPropertiesDialog::updateCompatibilityLabel(const SignalDef& s)
{
    const bool isInstrument = (cfg_.type == WidgetType::AnalogGauge || cfg_.type == WidgetType::DigitalReadout
                               || cfg_.type == WidgetType::BarGauge || cfg_.type == WidgetType::IndicatorLamp
                               || cfg_.type == WidgetType::TrendChart || cfg_.type == WidgetType::ClockDisplay);
    if (isInstrument)
    {
        if (!s.isOutput)
        {
            compatLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffffbb00));
            compatLabel_.setText("[Notice: Connection is an operator input, but gauge reads outputs]", juce::dontSendNotification);
        }
        else if (cfg_.type == WidgetType::AnalogGauge && s.type == DataType::Boolean)
        {
            compatLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffff6666));
            compatLabel_.setText("[Incompatible: Boolean signal cannot drive analog dial gauge]", juce::dontSendNotification);
        }
        else
        {
            compatLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff44ff88));
            compatLabel_.setText("[Compatible simulation output connection verified]", juce::dontSendNotification);
        }
    }
    else
    {
        if (s.isOutput)
        {
            compatLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffffbb00));
            compatLabel_.setText("[Notice: Connection is a simulation output, but control drives inputs]", juce::dontSendNotification);
        }
        else
        {
            compatLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff44ff88));
            compatLabel_.setText("[Compatible operator control connection verified]", juce::dontSendNotification);
        }
    }
}

void WidgetPropertiesDialog::resized()
{
    auto b = getLocalBounds().reduced(16);
    const int rowH = 24;
    const int gap = 8;
    const int lblW = 120;

    auto addRow = [&](juce::Label& lbl, juce::Component& comp) {
        auto r = b.removeFromTop(rowH);
        lbl.setBounds(r.removeFromLeft(lblW));
        comp.setBounds(r);
        b.removeFromTop(gap);
    };

    addRow(lblTitle_, titleEd_);
    addRow(lblPanelPicker_, panelPickerBox_);
    addRow(lblUnit_, unitEd_);
    addRow(lblMin_, minEd_);
    addRow(lblMax_, maxEd_);
    addRow(lblDef_, defEd_);
    addRow(lblStep_, stepEd_);
    addRow(lblPrec_, precBox_);
    addRow(lblSigPicker_, sigPickerBox_);
    addRow(lblSig_, sigEd_);

    compatLabel_.setBounds(b.removeFromTop(20).withTrimmedLeft(lblW));
    b.removeFromTop(gap);

    addRow(lblSecSig_, secSigEd_);

    auto rMode = b.removeFromTop(rowH);
    momentaryBtn_.setBounds(rMode.removeFromLeft(rMode.getWidth() / 2));
    verticalBtn_.setBounds(rMode);
    b.removeFromTop(gap);

    auto rWarn = b.removeFromTop(rowH);
    hasWarnBtn_.setBounds(rWarn.removeFromLeft(lblW + 40));
    warnMinEd_.setBounds(rWarn.removeFromLeft(70).reduced(2));
    warnMaxEd_.setBounds(rWarn.removeFromLeft(70).reduced(2));
    b.removeFromTop(gap);

    auto rCrit = b.removeFromTop(rowH);
    hasCritBtn_.setBounds(rCrit.removeFromLeft(lblW + 40));
    critMinEd_.setBounds(rCrit.removeFromLeft(70).reduced(2));
    critMaxEd_.setBounds(rCrit.removeFromLeft(70).reduced(2));
    b.removeFromTop(gap * 2);

    auto btnRow = b.removeFromTop(32);
    applyBtn_.setBounds(btnRow.removeFromRight(100).reduced(2));
    btnRow.removeFromRight(8);
    cancelBtn_.setBounds(btnRow.removeFromRight(90).reduced(2));
}

namespace
{

} // namespace

// -----------------------------------------------------------------------------
// ControlPanelCanvas Implementation
// -----------------------------------------------------------------------------

class ControlPanelComponent::ControlPanelCanvas final : public juce::Component
{
public:
    ControlPanelCanvas(ControlPanelComponent& owner) : owner_(owner) {}

    void setDesignMode(bool dm)
    {
        designMode_ = dm;
        if (!designMode_)
            selectedWrappers_.clear();
        for (int i = 0; i < getNumChildComponents(); ++i)
        {
            if (auto* w = dynamic_cast<DesignItemWrapper*>(getChildComponent(i)))
                w->setDesignMode(dm);
        }
        repaint();
    }

    void selectWrapper(DesignItemWrapper* w, bool add)
    {
        if (!add)
        {
            for (auto* sw : selectedWrappers_)
                sw->setSelected(false);
            selectedWrappers_.clear();
        }
        if (w != nullptr)
        {
            if (std::find(selectedWrappers_.begin(), selectedWrappers_.end(), w) == selectedWrappers_.end())
            {
                selectedWrappers_.push_back(w);
                w->setSelected(true);
            }
        }
        repaint();
    }

    void deselectAll()
    {
        for (auto* sw : selectedWrappers_)
            sw->setSelected(false);
        selectedWrappers_.clear();
        repaint();
    }

    std::vector<DesignItemWrapper*> getSelectedWrappers() const { return selectedWrappers_; }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff161a22));

        if (designMode_)
        {
            g.setColour(juce::Colour(0x28a0c0e0));
            const int step = 20;
            for (int x = step; x < getWidth(); x += step)
                for (int y = step; y < getHeight(); y += step)
                    g.fillRect(x - 1, y - 1, 2, 2);

            if (isMarqueeDragging_ && !marqueeRect_.isEmpty())
            {
                g.setColour(juce::Colour(0x3300d4ff));
                g.fillRect(marqueeRect_);
                g.setColour(juce::Colour(0xff00d4ff));
                g.drawRect(marqueeRect_, 1.0f);
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            owner_.showAddWidgetMenu(e.getPosition(), nullptr);
            return;
        }

        if (!designMode_) return;

        if (!e.mods.isShiftDown() && !e.mods.isCommandDown())
            deselectAll();

        isMarqueeDragging_ = true;
        marqueeStart_ = e.getPosition();
        marqueeRect_ = juce::Rectangle<int>(marqueeStart_.x, marqueeStart_.y, 0, 0);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!designMode_ || !isMarqueeDragging_) return;

        const int mx = juce::jmin(marqueeStart_.x, e.getPosition().x);
        const int my = juce::jmin(marqueeStart_.y, e.getPosition().y);
        const int mw = std::abs(e.getPosition().x - marqueeStart_.x);
        const int mh = std::abs(e.getPosition().y - marqueeStart_.y);
        marqueeRect_ = juce::Rectangle<int>(mx, my, mw, mh);
        for (int i = 0; i < getNumChildComponents(); ++i)
        {
            if (auto* w = dynamic_cast<DesignItemWrapper*>(getChildComponent(i)))
            {
                if (marqueeRect_.intersects(w->getBounds()))
                {
                    if (std::find(selectedWrappers_.begin(), selectedWrappers_.end(), w) == selectedWrappers_.end())
                    {
                        selectedWrappers_.push_back(w);
                        w->setSelected(true);
                    }
                }
            }
        }
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        isMarqueeDragging_ = false;
        marqueeRect_ = {};
        repaint();
    }

private:
    ControlPanelComponent& owner_;
    bool designMode_ = false;
    std::vector<DesignItemWrapper*> selectedWrappers_;
    bool isMarqueeDragging_ = false;
    juce::Point<int> marqueeStart_;
    juce::Rectangle<int> marqueeRect_;
};

// -----------------------------------------------------------------------------
// ControlPanelSplitter & PlotContainerComponent
// -----------------------------------------------------------------------------

class ControlPanelComponent::ControlPanelSplitter final : public juce::Component
{
public:
    ControlPanelSplitter(std::function<void(int deltaX)> onDragDelta,
                         std::function<void()> onDragStart,
                         std::function<void()> onDragEnd)
        : onDragDelta_(std::move(onDragDelta)),
          onDragStart_(std::move(onDragStart)),
          onDragEnd_(std::move(onDragEnd))
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        setRepaintsOnMouseActivity(true);
        setInterceptsMouseClicks(true, false);
    }

    bool hitTest(int x, int y) override
    {
        return x >= 0 && x < getWidth() && y >= 0 && y < getHeight();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        const bool active = isMouseOverOrDragging();

        // Background track
        g.setColour(juce::Colour(0xff141923));
        g.fillRect(b);

        // Divider line
        g.setColour(active ? juce::Colour(0xff00d2ff) : juce::Colour(0xff2b3848));
        const int midX = b.getCentreX();
        g.fillRect(midX - 1, 0, 2, b.getHeight());

        // Vertical pill grip handle
        const int gripH = 36;
        const int gripY = (b.getHeight() - gripH) / 2;
        g.setColour(active ? juce::Colour(0xff00d2ff) : juce::Colour(0xff4a5d73));
        g.fillRoundedRectangle((float)(midX - 2), (float)gripY, 4.0f, (float)gripH, 2.0f);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        isDragging_ = true;
        dragStartScreenX_ = e.getScreenX();
        if (onDragStart_)
            onDragStart_();
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!isDragging_)
            return;
        int delta = e.getDistanceFromDragStartX();
        if (delta == 0 && dragStartScreenX_ != 0)
            delta = e.getScreenX() - dragStartScreenX_;
        if (onDragDelta_)
            onDragDelta_(delta);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (isDragging_)
        {
            isDragging_ = false;
            dragStartScreenX_ = 0;
            if (onDragEnd_)
                onDragEnd_();
            repaint();
        }
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}

    bool isDragging() const { return isDragging_; }

private:
    std::function<void(int)> onDragDelta_;
    std::function<void()> onDragStart_;
    std::function<void()> onDragEnd_;
    bool isDragging_ = false;
    int dragStartScreenX_ = 0;
};

class ControlPanelComponent::PlotContainerComponent final : public juce::Component
{
public:
    PlotContainerComponent()
    {
        setInterceptsMouseClicks(true, true);
    }

    void layoutChildren()
    {
        if (inLayout_) return;
        juce::ScopedValueSetter<bool> guard(inLayout_, true);
        auto b = getLocalBounds();
        for (int i = 0; i < getNumChildComponents(); ++i)
        {
            if (auto* child = getChildComponent(i))
                child->setBounds(b);
        }
    }

    void resized() override
    {
        layoutChildren();
    }

    void childrenChanged() override
    {
        layoutChildren();
    }

    void childBoundsChanged(juce::Component* child) override
    {
        if (inLayout_) return;
        if (child != nullptr && child->getBounds() != getLocalBounds())
        {
            juce::ScopedValueSetter<bool> guard(inLayout_, true);
            child->setBounds(getLocalBounds());
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff10161d));
    }

    void mouseDown(const juce::MouseEvent&) override {}
    void mouseDrag(const juce::MouseEvent&) override {}
    void mouseUp(const juce::MouseEvent&) override {}
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}

private:
    bool inLayout_ = false;
};

namespace
{
PlotPanelWidget* getPlotWidgetFromComp(juce::Component* comp)
{
    if (auto* plot = dynamic_cast<PlotPanelWidget*>(comp))
        return plot;
    if (auto* wrapper = dynamic_cast<DesignItemWrapper*>(comp))
        return dynamic_cast<PlotPanelWidget*>(wrapper->getWidget());
    return nullptr;
}

const PlotPanelWidget* getPlotWidgetFromComp(const juce::Component* comp)
{
    if (const auto* plot = dynamic_cast<const PlotPanelWidget*>(comp))
        return plot;
    if (const auto* wrapper = dynamic_cast<const DesignItemWrapper*>(comp))
        return dynamic_cast<const PlotPanelWidget*>(wrapper->getWidget());
    return nullptr;
}
} // namespace

// -----------------------------------------------------------------------------
// ControlPanelComponent Methods
// -----------------------------------------------------------------------------

ControlPanelComponent::ControlPanelComponent()
    : ControlPanelComponent(std::make_shared<SystemSimulationSession>())
{
}

ControlPanelComponent::ControlPanelComponent(std::shared_ptr<SystemSimulationSession> session)
    : session_(session)
{
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
        if (!session_) return;
        if (onEnsureCompiled) onEnsureCompiled();
        publishAllControlInputs();
        session_->start();
    };
    pauseButton.onClick = [this] { if (session_) session_->pause(); };
    stepButton.onClick = [this] {
        if (!session_) return;
        if (onEnsureCompiled) onEnsureCompiled();
        publishAllControlInputs();
        session_->stepSingle(0.005);
    };
    stopButton.onClick = [this] { if (session_) session_->stop(); };
    resetButton.onClick = [this] {
        if (!session_) return;
        session_->reset();
        publishAllControlInputs();
    };

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
        const int id = panelBox.getSelectedId();
        if (id >= 1 && id <= (int)layout_.panels.size())
            setActivePanel(layout_.panels[(size_t)(id - 1)].id);
    };

    auto setupPanelBtn = [this](juce::TextButton& btn) {
        addAndMakeVisible(btn);
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff22313d));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
    };
    setupPanelBtn(newPanelButton);
    setupPanelBtn(renamePanelButton);
    setupPanelBtn(removePanelButton);

    setupPanelBtn(saveButton);
    saveButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b5e20));
    saveButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe8f5e9));
    saveButton.setTooltip("Save Control Panels and Machine Project (Ctrl+S)");
    saveButton.onClick = [this] {
        if (onSaveRequested)
            onSaveRequested();
        else
            setStatusText("SAVED", juce::Colour(0xff80ed99));
    };

    newPanelButton.onClick = [this] { createNewPanel(); };
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
    removePanelButton.onClick = [this] { removeActivePanel(); };

    // Designer transport buttons
    setupPanelBtn(modeButton);
    modeButton.onClick = [this] { setDesignMode(!designMode_); };

    setupPanelBtn(addWidgetButton);
    addWidgetButton.onClick = [this] { showAddWidgetMenu(juce::Point<int>(-1, -1), &addWidgetButton); };
    addWidgetButton.setVisible(false);

    setupPanelBtn(alignButton);
    alignButton.onClick = [this] { showAlignMenu(); };
    alignButton.setVisible(false);

    addAndMakeVisible(statusLabel);
    statusLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff66ff99));
    statusLabel.setText("STOPPED", juce::dontSendNotification);

    canvasComponent = std::make_unique<ControlPanelCanvas>(*this);
    addAndMakeVisible(canvasViewport);
    canvasViewport.setViewedComponent(canvasComponent.get(), false);
    canvasViewport.setScrollBarsShown(true, true);

    splitter_ = std::make_unique<ControlPanelSplitter>(
        [this](int deltaX) { handleSplitterDrag(deltaX); },
        [this]() { handleSplitterDragStart(); },
        [this]() { handleSplitterDragEnd(); });
    addChildComponent(*splitter_);

    plotContainer_ = std::make_unique<PlotContainerComponent>();
    addChildComponent(*plotContainer_);

    ControlPanelLayout blank;
    blank.title = "No Active Machine";
    setLayout(blank);
    startTimerHz(30);
}

ControlPanelComponent::~ControlPanelComponent()
{
    stopTimer();
}

void ControlPanelComponent::setDesignMode(bool isDesign)
{
    designMode_ = isDesign;
    if (auto* panel = layout_.findPanel(layout_.activePanelId))
        panel->designMode = isDesign;
    if (canvasComponent)
        canvasComponent->setDesignMode(isDesign);

    modeButton.setButtonText(designMode_ ? "Run Mode" : "Design Mode");
    modeButton.setColour(juce::TextButton::buttonColourId,
                         designMode_ ? juce::Colour(0xffb86200) : juce::Colour(0xff22313d));
    addWidgetButton.setVisible(designMode_);
    alignButton.setVisible(designMode_);
    resized();
}

bool ControlPanelComponent::addWidget(const WidgetConfig& config)
{
    auto copy = config;
    if (copy.id.empty())
        copy.id = "elem_" + juce::String(juce::Random::getSystemRandom().nextInt(999999)).toStdString();
    if (copy.panelId.empty())
        copy.panelId = layout_.activePanelId;

    layout_.widgets.push_back(copy);
    layout_.ensureWidgetBounds();
    buildWidgets();
    resized();

    // Select the newly added widget so selection handles immediately appear
    selectWidget(copy.id, false);

    // Scroll viewport to keep the new widget visible if needed
    const int viewX = canvasViewport.getViewPositionX();
    const int viewY = canvasViewport.getViewPositionY();
    const int viewW = canvasViewport.getViewWidth();
    const int viewH = canvasViewport.getViewHeight();
    if (viewW > 0 && viewH > 0)
    {
        if (copy.x < viewX || copy.x + copy.width > viewX + viewW ||
            copy.y < viewY || copy.y + copy.height > viewY + viewH)
        {
            canvasViewport.setViewPosition(juce::jmax(0, copy.x - 24), juce::jmax(0, copy.y - 24));
        }
    }

    notifyLayoutChanged();
    return true;
}

bool ControlPanelComponent::removeWidget(const std::string& widgetId)
{
    const auto initialSize = layout_.widgets.size();
    layout_.widgets.erase(std::remove_if(layout_.widgets.begin(), layout_.widgets.end(),
                                         [&](const auto& w) { return w.id == widgetId; }),
                          layout_.widgets.end());
    if (layout_.widgets.size() != initialSize)
    {
        buildWidgets();
        resized();
        notifyLayoutChanged();
        return true;
    }
    return false;
}

bool ControlPanelComponent::configureWidget(const std::string& widgetId, const WidgetConfig& newConfig)
{
    for (auto& w : layout_.widgets)
    {
        if (w.id == widgetId)
        {
            w = newConfig;
            updateWidgetConfig(newConfig);
            return true;
        }
    }
    return false;
}

const WidgetConfig* ControlPanelComponent::getWidgetConfig(const std::string& widgetId) const
{
    return layout_.findWidget(widgetId);
}

bool ControlPanelComponent::setWidgetBounds(const std::string& widgetId, int x, int y, int width, int height)
{
    for (auto& w : layout_.widgets)
    {
        if (w.id == widgetId)
        {
            w.x = x;
            w.y = y;
            w.width = width;
            w.height = height;
            for (auto& ov : outputViews_)
            {
                if (auto* wrapper = dynamic_cast<DesignItemWrapper*>(ov.component.get()))
                {
                    if (wrapper->config.id == widgetId)
                    {
                        wrapper->setBounds(x, y, width, height);
                        wrapper->config = w;
                        ov.config = w;
                        break;
                    }
                }
            }
            resized();
            notifyLayoutChanged();
            return true;
        }
    }
    return false;
}

juce::Rectangle<int> ControlPanelComponent::getWidgetBounds(const std::string& widgetId) const
{
    for (const auto& w : layout_.widgets)
        if (w.id == widgetId)
            return { w.x, w.y, w.width, w.height };
    return {};
}

void ControlPanelComponent::selectWidget(const std::string& widgetId, bool addToSelection)
{
    if (!canvasComponent) return;
    for (int i = 0; i < canvasComponent->getNumChildComponents(); ++i)
    {
        if (auto* wrapper = dynamic_cast<DesignItemWrapper*>(canvasComponent->getChildComponent(i)))
        {
            if (wrapper->config.id == widgetId)
            {
                canvasComponent->selectWrapper(wrapper, addToSelection);
                return;
            }
        }
    }
}

void ControlPanelComponent::deselectAll()
{
    if (canvasComponent)
        canvasComponent->deselectAll();
}

std::vector<std::string> ControlPanelComponent::getSelectedWidgetIds() const
{
    std::vector<std::string> ids;
    if (canvasComponent)
    {
        for (auto* w : canvasComponent->getSelectedWrappers())
            ids.push_back(w->config.id);
    }
    return ids;
}

bool ControlPanelComponent::alignSelectedWidgets(const std::string& alignment)
{
    if (!canvasComponent) return false;
    const auto selected = canvasComponent->getSelectedWrappers();
    if (selected.size() < 2) return false;

    if (alignment == "left")
    {
        int minX = std::numeric_limits<int>::max();
        for (auto* w : selected) minX = std::min(minX, w->getX());
        for (auto* w : selected)
        {
            w->setTopLeftPosition(minX, w->getY());
            w->config.x = minX;
            updateWidgetConfig(w->config);
        }
    }
    else if (alignment == "right")
    {
        int maxR = std::numeric_limits<int>::min();
        for (auto* w : selected) maxR = std::max(maxR, w->getRight());
        for (auto* w : selected)
        {
            w->setTopLeftPosition(maxR - w->getWidth(), w->getY());
            w->config.x = maxR - w->getWidth();
            updateWidgetConfig(w->config);
        }
    }
    else if (alignment == "top")
    {
        int minY = std::numeric_limits<int>::max();
        for (auto* w : selected) minY = std::min(minY, w->getY());
        for (auto* w : selected)
        {
            w->setTopLeftPosition(w->getX(), minY);
            w->config.y = minY;
            updateWidgetConfig(w->config);
        }
    }
    else if (alignment == "bottom")
    {
        int maxB = std::numeric_limits<int>::min();
        for (auto* w : selected) maxB = std::max(maxB, w->getBottom());
        for (auto* w : selected)
        {
            w->setTopLeftPosition(w->getX(), maxB - w->getHeight());
            w->config.y = maxB - w->getHeight();
            updateWidgetConfig(w->config);
        }
    }
    else if (alignment == "sameWidth")
    {
        const int targetW = selected.front()->getWidth();
        for (auto* w : selected)
        {
            w->setSize(targetW, w->getHeight());
            w->config.width = targetW;
            updateWidgetConfig(w->config);
        }
    }
    else if (alignment == "sameHeight")
    {
        const int targetH = selected.front()->getHeight();
        for (auto* w : selected)
        {
            w->setSize(w->getWidth(), targetH);
            w->config.height = targetH;
            updateWidgetConfig(w->config);
        }
    }

    notifyLayoutChanged();
    return true;
}

bool ControlPanelComponent::duplicateSelectedWidgets()
{
    const auto ids = getSelectedWidgetIds();
    if (ids.empty()) return false;
    for (const auto& id : ids)
    {
        if (const auto* cfg = getWidgetConfig(id))
        {
            auto copy = *cfg;
            copy.id = copy.id + "_copy";
            copy.x += 24;
            copy.y += 24;
            addWidget(copy);
        }
    }
    return true;
}

bool ControlPanelComponent::deleteSelectedWidgets()
{
    const auto ids = getSelectedWidgetIds();
    if (ids.empty()) return false;
    for (const auto& id : ids)
        removeWidget(id);
    return true;
}

std::vector<SignalDef> ControlPanelComponent::getAvailableSignals() const
{
    std::vector<SignalDef> sigs;
    std::map<std::string, bool> seen;

    auto addSig = [&](const std::string& name, DataType dt, bool isOutput, const std::string& unit,
                      const std::string& desc, double minVal = 0.0, double maxVal = 100.0) {
        if (name.empty() || seen.count(name) != 0) return;
        seen[name] = true;
        SignalDef def;
        def.name = name;
        def.type = dt;
        def.isOutput = isOutput;
        def.unit = unit;
        def.minValue = minVal;
        def.maxValue = maxVal;
        def.description = desc;
        sigs.push_back(def);
    };

    if (session_)
    {
        for (const auto& [name, node] : session_->outputNodeBindings())
        {
            std::string unit;
            double minV = 0.0, maxV = 100.0;
            if (name.find("speed") != std::string::npos) { unit = "rad/s"; minV = 0.0; maxV = 600.0; }
            else if (name.find("torque") != std::string::npos || name.find("load") != std::string::npos) { unit = "N*m"; minV = 0.0; maxV = 0.05; }
            else if (name.find("voltage") != std::string::npos || name.find("drive") != std::string::npos || name.find("tach") != std::string::npos || name.find("v_in") != std::string::npos) { unit = "V"; minV = 0.0; maxV = 24.0; }
            else if (name.find("current") != std::string::npos) { unit = "A"; minV = -20.0; maxV = 20.0; }
            else if (name.find("angle") != std::string::npos) { unit = "rad"; minV = -1000.0; maxV = 1000.0; }
            addSig(name, DataType::Numeric, true, unit, "Simulation Node Output", minV, maxV);
        }

        for (const auto& [name, elem] : session_->outputBranchBindings())
        {
            addSig(name, DataType::Numeric, true, "A", "Simulation Branch Current", -20.0, 20.0);
        }

        for (const auto& [name, elem] : session_->inputElementBindings())
        {
            const bool isBool = name.find("power") != std::string::npos
                             || name.find("start") != std::string::npos
                             || name.find("stop") != std::string::npos
                             || name.find("estop") != std::string::npos
                             || name.find("reset") != std::string::npos;
            if (isBool)
            {
                addSig(name, DataType::Boolean, false, "", "Operator Boolean Control", 0.0, 1.0);
            }
            else
            {
                std::string unit;
                double minV = 0.0, maxV = 100.0;
                if (name.find("speed") != std::string::npos) { unit = "rad/s"; minV = 0.0; maxV = 600.0; }
                else if (name.find("torque") != std::string::npos || name.find("load") != std::string::npos) { unit = "N*m"; minV = 0.0; maxV = 0.05; }
                else if (name.find("voltage") != std::string::npos || name.find("drive") != std::string::npos) { unit = "V"; minV = 0.0; maxV = 24.0; }
                addSig(name, DataType::Numeric, false, unit, "Operator Numeric Input", minV, maxV);
            }
        }

        const auto snap = session_->getSnapshot();
        for (const auto& [name, val] : snap.numericSignals)
        {
            std::string unit;
            double minV = 0.0, maxV = 100.0;
            if (name.find("speed") != std::string::npos) { unit = "rad/s"; minV = 0.0; maxV = 600.0; }
            else if (name.find("torque") != std::string::npos || name.find("load") != std::string::npos) { unit = "N*m"; minV = 0.0; maxV = 0.05; }
            else if (name.find("voltage") != std::string::npos || name.find("drive") != std::string::npos || name.find("tach") != std::string::npos) { unit = "V"; minV = 0.0; maxV = 24.0; }
            else if (name.find("current") != std::string::npos) { unit = "A"; minV = -20.0; maxV = 20.0; }
            else if (name.find("angle") != std::string::npos) { unit = "rad"; minV = -1000.0; maxV = 1000.0; }
            addSig(name, DataType::Numeric, true, unit, "Simulation Numeric Output", minV, maxV);
        }
        for (const auto& [name, val] : snap.booleanSignals)
        {
            addSig(name, DataType::Boolean, true, "", "Simulation Status Output", 0.0, 1.0);
        }
    }

    addSig("system.power", DataType::Boolean, false, "", "Master Power Switch", 0.0, 1.0);
    addSig("system.start", DataType::Boolean, false, "", "Start Cycle Command", 0.0, 1.0);
    addSig("system.stop", DataType::Boolean, false, "", "Stop Cycle Command", 0.0, 1.0);
    addSig("system.estop", DataType::Boolean, false, "", "Emergency Stop", 0.0, 1.0);
    addSig("system.reset", DataType::Boolean, false, "", "Fault Reset", 0.0, 1.0);
    addSig("system.speed_setpoint", DataType::Numeric, false, "rad/s", "Speed Setpoint", 0.0, 600.0);
    addSig("motor.load_torque", DataType::Numeric, false, "N*m", "Mechanical Load", 0.0, 0.05);
    addSig("motor.speed", DataType::Numeric, true, "rad/s", "Motor Speed", 0.0, 600.0);
    addSig("motor.angle", DataType::Numeric, true, "rad", "Rotor Angle", -1000.0, 1000.0);
    addSig("motor.current", DataType::Numeric, true, "A", "Armature Current", -20.0, 20.0);
    addSig("running", DataType::Boolean, true, "", "Running Status Indicator", 0.0, 1.0);
    addSig("fault", DataType::Boolean, true, "", "Fault Alarm Indicator", 0.0, 1.0);

    for (const auto& w : layout_.widgets)
    {
        if (!w.signalName.empty())
        {
            const bool isInput = (w.column == 0);
            const DataType dt = (w.type == WidgetType::PushButton || w.type == WidgetType::ToggleSwitch
                                 || w.type == WidgetType::IndicatorLamp || w.type == WidgetType::EmergencyStop)
                              ? DataType::Boolean : DataType::Numeric;
            addSig(w.signalName, dt, !isInput, w.unit, "Configured Widget Signal");
        }
    }

    return sigs;
}

juce::Point<int> ControlPanelComponent::getCanvasScrollPosition() const
{
    return { canvasViewport.getViewPositionX(), canvasViewport.getViewPositionY() };
}

void ControlPanelComponent::setCanvasScrollPosition(int x, int y)
{
    canvasViewport.setViewPosition(x, y);
}

juce::Rectangle<int> ControlPanelComponent::getCanvasVisibleArea() const
{
    return canvasViewport.getViewArea();
}

juce::Rectangle<int> ControlPanelComponent::getCanvasTotalArea() const
{
    return canvasComponent ? canvasComponent->getBounds() : juce::Rectangle<int>();
}

std::unique_ptr<WidgetPropertiesDialog> ControlPanelComponent::createWidgetPropertiesDialog(const std::string& widgetId)
{
    const auto* cfg = getWidgetConfig(widgetId);
    if (cfg == nullptr) return nullptr;

    return std::make_unique<WidgetPropertiesDialog>(
        *cfg, getAvailableSignals(), layout_.panels,
        [this](const WidgetConfig& updated) {
            configureWidget(updated.id, updated);
        });
}

void ControlPanelComponent::showWidgetPropertiesDialog(const std::string& widgetId)
{
    const auto* cfg = getWidgetConfig(widgetId);
    if (cfg == nullptr) return;

    auto dialog = createWidgetPropertiesDialog(widgetId);
    if (!dialog) return;

    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = "Widget Properties & Signal Connection - " + juce::String(cfg->id);
    opt.content.setOwned(dialog.release());
    opt.content->setSize(520, 590);
    opt.dialogBackgroundColour = juce::Colour(0xff1b222c);
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

juce::Component* ControlPanelComponent::getWidgetComponent(const std::string& widgetId) const
{
    for (const auto& ov : outputViews_)
    {
        if (auto* wrapper = dynamic_cast<DesignItemWrapper*>(ov.component.get()))
        {
            if (wrapper->config.id == widgetId)
                return wrapper->widget.get();
        }
        else if (auto* plot = dynamic_cast<PlotPanelWidget*>(ov.component.get()))
        {
            if (plot->widgetId() == widgetId)
                return plot;
        }
    }
    return nullptr;
}

juce::Point<int> ControlPanelComponent::getNewWidgetSpawnPosition(int widgetW, int widgetH) const
{
    const int viewX = canvasViewport.getViewPositionX();
    const int viewY = canvasViewport.getViewPositionY();
    const int viewW = std::max(200, canvasViewport.getViewWidth());
    const int viewH = std::max(200, canvasViewport.getViewHeight());

    int candX = viewX + 24;
    int candY = viewY + 24;

    const auto& currentWidgets = layout_.widgets;
    const auto panelId = layout_.activePanelId.empty() ? std::string("main") : layout_.activePanelId;

    bool collision = true;
    int attempts = 0;
    while (collision && attempts < 35)
    {
        collision = false;
        juce::Rectangle<int> candRect(candX, candY, widgetW, widgetH);
        for (const auto& w : currentWidgets)
        {
            const auto widgetPanelId = w.panelId.empty() ? std::string("main") : w.panelId;
            if (widgetPanelId != panelId)
                continue;
            juce::Rectangle<int> wRect(w.x, w.y, w.width, w.height);
            if (candRect.intersects(wRect.reduced(4)))
            {
                collision = true;
                break;
            }
        }
        if (collision)
        {
            candY += 36;
            if (candY + widgetH > viewY + viewH - 20)
            {
                candY = viewY + 24;
                candX += 48;
            }
        }
        attempts++;
    }

    return { std::max(10, candX), std::max(10, candY) };
}

void ControlPanelComponent::showAddWidgetMenu(juce::Point<int> canvasPos, juce::Component* targetComponent)
{
    const auto targetPanelId = layout_.activePanelId.empty() ? std::string("main") : layout_.activePanelId;
    auto resolvePos = [this, canvasPos](int defW, int defH) -> juce::Point<int> {
        if (canvasPos.x >= 0 && canvasPos.y >= 0)
            return canvasPos;
        return getNewWidgetSpawnPosition(defW, defH);
    };

    juce::PopupMenu menu;
    juce::PopupMenu instrMenu;
    instrMenu.addItem("Analog Needle Gauge", [this, resolvePos, targetPanelId] {
        WidgetConfig w;
        w.id = "gauge_" + juce::String(juce::Random::getSystemRandom().nextInt(9999)).toStdString();
        w.type = WidgetType::AnalogGauge;
        w.title = "Analog Gauge";
        w.panelId = targetPanelId;
        w.width = 180; w.height = 180;
        auto p = resolvePos(w.width, w.height);
        w.x = p.x; w.y = p.y;
        w.column = 1;
        addWidget(w);
    });
    instrMenu.addItem("Digital Readout", [this, resolvePos, targetPanelId] {
        WidgetConfig w;
        w.id = "num_" + juce::String(juce::Random::getSystemRandom().nextInt(9999)).toStdString();
        w.type = WidgetType::DigitalReadout;
        w.title = "Digital Readout";
        w.panelId = targetPanelId;
        w.width = 180; w.height = 48;
        auto p = resolvePos(w.width, w.height);
        w.x = p.x; w.y = p.y;
        w.column = 1;
        addWidget(w);
    });
    instrMenu.addItem("Bar Gauge (Horizontal)", [this, resolvePos, targetPanelId] {
        WidgetConfig w;
        w.id = "bar_" + juce::String(juce::Random::getSystemRandom().nextInt(9999)).toStdString();
        w.type = WidgetType::BarGauge;
        w.title = "Bar Gauge";
        w.vertical = false;
        w.panelId = targetPanelId;
        w.width = 200; w.height = 42;
        auto p = resolvePos(w.width, w.height);
        w.x = p.x; w.y = p.y;
        w.column = 1;
        addWidget(w);
    });
    instrMenu.addItem("Bar Gauge (Vertical)", [this, resolvePos, targetPanelId] {
        WidgetConfig w;
        w.id = "bar_" + juce::String(juce::Random::getSystemRandom().nextInt(9999)).toStdString();
        w.type = WidgetType::BarGauge;
        w.title = "Bar Gauge";
        w.vertical = true;
        w.panelId = targetPanelId;
        w.width = 80; w.height = 180;
        auto p = resolvePos(w.width, w.height);
        w.x = p.x; w.y = p.y;
        w.column = 1;
        addWidget(w);
    });
    instrMenu.addItem("Indicator Lamp", [this, resolvePos, targetPanelId] {
        WidgetConfig w;
        w.id = "lamp_" + juce::String(juce::Random::getSystemRandom().nextInt(9999)).toStdString();
        w.type = WidgetType::IndicatorLamp;
        w.title = "Indicator Lamp";
        w.panelId = targetPanelId;
        w.width = 180; w.height = 36;
        auto p = resolvePos(w.width, w.height);
        w.x = p.x; w.y = p.y;
        w.column = 1;
        addWidget(w);
    });
    menu.addSubMenu("Add Instrument", instrMenu);

    juce::PopupMenu ctrlMenu;
    ctrlMenu.addItem("Push Button", [this, resolvePos, targetPanelId] {
        WidgetConfig w;
        w.id = "btn_" + juce::String(juce::Random::getSystemRandom().nextInt(9999)).toStdString();
        w.type = WidgetType::PushButton;
        w.title = "Push Button";
        w.momentary = true;
        w.panelId = targetPanelId;
        w.width = 160; w.height = 32;
        auto p = resolvePos(w.width, w.height);
        w.x = p.x; w.y = p.y;
        w.column = 0;
        addWidget(w);
    });
    ctrlMenu.addItem("Momentary Pushbutton", [this, resolvePos, targetPanelId] {
        WidgetConfig w;
        w.id = "btn_" + juce::String(juce::Random::getSystemRandom().nextInt(9999)).toStdString();
        w.type = WidgetType::PushButton;
        w.title = "Push Button";
        w.momentary = true;
        w.panelId = targetPanelId;
        w.width = 160; w.height = 32;
        auto p = resolvePos(w.width, w.height);
        w.x = p.x; w.y = p.y;
        w.column = 0;
        addWidget(w);
    });
    ctrlMenu.addItem("Latching Switch", [this, resolvePos, targetPanelId] {
        WidgetConfig w;
        w.id = "latch_" + juce::String(juce::Random::getSystemRandom().nextInt(9999)).toStdString();
        w.type = WidgetType::LatchingButton;
        w.title = "Latching Switch";
        w.momentary = false;
        w.panelId = targetPanelId;
        w.width = 160; w.height = 32;
        auto p = resolvePos(w.width, w.height);
        w.x = p.x; w.y = p.y;
        w.column = 0;
        addWidget(w);
    });
    ctrlMenu.addItem("Toggle Switch", [this, resolvePos, targetPanelId] {
        WidgetConfig w;
        w.id = "sw_" + juce::String(juce::Random::getSystemRandom().nextInt(9999)).toStdString();
        w.type = WidgetType::ToggleSwitch;
        w.title = "Toggle Switch";
        w.panelId = targetPanelId;
        w.width = 160; w.height = 32;
        auto p = resolvePos(w.width, w.height);
        w.x = p.x; w.y = p.y;
        w.column = 0;
        addWidget(w);
    });
    ctrlMenu.addItem("Rotary Knob", [this, resolvePos, targetPanelId] {
        WidgetConfig w;
        w.id = "knob_" + juce::String(juce::Random::getSystemRandom().nextInt(9999)).toStdString();
        w.type = WidgetType::Knob;
        w.title = "Control Knob";
        w.panelId = targetPanelId;
        w.width = 140; w.height = 64;
        auto p = resolvePos(w.width, w.height);
        w.x = p.x; w.y = p.y;
        w.column = 0;
        addWidget(w);
    });
    ctrlMenu.addItem("Slider (Horizontal)", [this, resolvePos, targetPanelId] {
        WidgetConfig w;
        w.id = "slider_" + juce::String(juce::Random::getSystemRandom().nextInt(9999)).toStdString();
        w.type = WidgetType::Slider;
        w.title = "Control Slider";
        w.vertical = false;
        w.panelId = targetPanelId;
        w.width = 220; w.height = 42;
        auto p = resolvePos(w.width, w.height);
        w.x = p.x; w.y = p.y;
        w.column = 0;
        addWidget(w);
    });
    ctrlMenu.addItem("Selector Switch", [this, resolvePos, targetPanelId] {
        WidgetConfig w;
        w.id = "sel_" + juce::String(juce::Random::getSystemRandom().nextInt(9999)).toStdString();
        w.type = WidgetType::ModeSelector;
        w.title = "Mode Selector";
        w.options = { "OFF", "MANUAL", "AUTO" };
        w.panelId = targetPanelId;
        w.width = 180; w.height = 42;
        auto p = resolvePos(w.width, w.height);
        w.x = p.x; w.y = p.y;
        w.column = 0;
        addWidget(w);
    });
    ctrlMenu.addItem("Indicator Button", [this, resolvePos, targetPanelId] {
        WidgetConfig w;
        w.id = "indbtn_" + juce::String(juce::Random::getSystemRandom().nextInt(9999)).toStdString();
        w.type = WidgetType::IndicatorButton;
        w.title = "Status Button";
        w.panelId = targetPanelId;
        w.width = 180; w.height = 36;
        auto p = resolvePos(w.width, w.height);
        w.x = p.x; w.y = p.y;
        w.column = 0;
        addWidget(w);
    });
    ctrlMenu.addItem("Emergency Stop", [this, resolvePos, targetPanelId] {
        WidgetConfig w;
        w.id = "estop_" + juce::String(juce::Random::getSystemRandom().nextInt(9999)).toStdString();
        w.type = WidgetType::EmergencyStop;
        w.title = "EMERGENCY STOP";
        w.panelId = targetPanelId;
        w.width = 220; w.height = 54;
        auto p = resolvePos(w.width, w.height);
        w.x = p.x; w.y = p.y;
        w.column = 0;
        addWidget(w);
    });
    menu.addSubMenu("Add Control", ctrlMenu);

    if (!clipboard_.empty())
    {
        menu.addSeparator();
        menu.addItem("Paste", [this, resolvePos, targetPanelId] {
            for (auto item : clipboard_)
            {
                item.id = item.id + "_copy";
                item.panelId = targetPanelId;
                auto p = resolvePos(item.width, item.height);
                item.x = p.x;
                item.y = p.y;
                addWidget(item);
            }
        });
    }

    juce::PopupMenu::Options opts;
    if (targetComponent != nullptr)
    {
        opts = opts.withTargetComponent(targetComponent);
    }
    else
    {
        const auto screenPos = canvasComponent ? canvasComponent->localPointToGlobal(canvasPos) : localPointToGlobal(canvasPos);
        opts = opts.withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1));
    }

    opts = opts.withMinimumWidth(200)
               .withStandardItemHeight(28);

    menu.showMenuAsync(opts);
}

void ControlPanelComponent::showAlignMenu()
{
    juce::PopupMenu menu;
    menu.addItem("Align Left", [this] { alignSelectedWidgets("left"); });
    menu.addItem("Align Right", [this] { alignSelectedWidgets("right"); });
    menu.addItem("Align Top", [this] { alignSelectedWidgets("top"); });
    menu.addItem("Align Bottom", [this] { alignSelectedWidgets("bottom"); });
    menu.addSeparator();
    menu.addItem("Make Same Width", [this] { alignSelectedWidgets("sameWidth"); });
    menu.addItem("Make Same Height", [this] { alignSelectedWidgets("sameHeight"); });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&alignButton));
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
    panel.designMode = designMode_;
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
    {
        const auto widgetPanelId = widget.panelId.empty() ? std::string("main") : widget.panelId;
        if (widgetPanelId == removed)
            widget.panelId = fallback;
    }
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

void ControlPanelComponent::publishAllControlInputs()
{
    if (!session_)
        return;

    // Harvest values from instantiated components on the active canvas
    for (const auto& ov : outputViews_)
    {
        auto* wrapper = dynamic_cast<DesignItemWrapper*>(ov.component.get());
        if (!wrapper) continue;
        auto* comp = wrapper->getWidget();
        if (!comp) continue;
        const auto& w = wrapper->config;
        if (w.signalName.empty()) continue;

        if (w.type == WidgetType::Knob || w.type == WidgetType::Slider)
        {
            if (auto* ctrl = dynamic_cast<LabeledSliderComponent*>(comp))
            {
                const double val = ctrl->slider.getValue();
                activeNumericControlValues_[w.signalName] = val;
                session_->setNumericInput(w.signalName, val);
            }
        }
        else if (w.type == WidgetType::ToggleSwitch)
        {
            if (auto* tog = dynamic_cast<ToggleSwitchComponent*>(comp))
            {
                const bool st = tog->getToggleState();
                activeBooleanControlValues_[w.signalName] = st;
                session_->setBooleanInput(w.signalName, st);
            }
        }
        else if (w.type == WidgetType::ModeSelector)
        {
            if (auto* sel = dynamic_cast<SelectorSwitchComponent*>(comp))
            {
                const double idx = (double)sel->getSelectedIndex();
                activeNumericControlValues_[w.signalName] = idx;
                session_->setNumericInput(w.signalName, idx);
            }
        }
        else if (w.type == WidgetType::EmergencyStop)
        {
            if (auto* estop = dynamic_cast<EmergencyStopComponent*>(comp))
            {
                const bool latched = estop->isLatched();
                activeBooleanControlValues_[w.signalName] = latched;
                session_->setBooleanInput(w.signalName, latched);
            }
        }
        else if (w.type == WidgetType::IndicatorButton)
        {
            if (auto* ib = dynamic_cast<IndicatorButtonComponent*>(comp))
            {
                const bool active = ib->isButtonActive();
                activeBooleanControlValues_[w.signalName] = active;
                session_->setBooleanInput(w.signalName, active);
            }
        }
    }

    // Now publish configured values for all widgets in the layout (including uninstantiated ones)
    for (const auto& w : layout_.widgets)
    {
        if (w.signalName.empty()) continue;

        if (w.type == WidgetType::Knob || w.type == WidgetType::Slider || w.type == WidgetType::ModeSelector)
        {
            double val = w.defaultValue;
            if (activeNumericControlValues_.count(w.signalName))
                val = activeNumericControlValues_[w.signalName];
            else
                activeNumericControlValues_[w.signalName] = val;
            session_->setNumericInput(w.signalName, val);
        }
        else if (w.type == WidgetType::ToggleSwitch || w.type == WidgetType::EmergencyStop
                 || w.type == WidgetType::IndicatorButton)
        {
            bool val = (w.defaultValue > 0.5);
            if (activeBooleanControlValues_.count(w.signalName))
                val = activeBooleanControlValues_[w.signalName];
            else
                activeBooleanControlValues_[w.signalName] = val;
            session_->setBooleanInput(w.signalName, val);
        }
    }
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
    publishAllControlInputs();
    statusLabel.setText("STOPPED", juce::dontSendNotification);
    return true;
}

void ControlPanelComponent::setLayout(const ControlPanelLayout& layout)
{
    if (notifyingLayout_)
        return;
    layout_ = layout;
    layout_.ensureDefaultPanel();
    layout_.ensureWidgetBounds();
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
    if (auto* panel = layout_.findPanel(panelId))
        panel->designMode = designMode_;
    deselectAll();
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
    bool foundInLayout = false;
    for (auto& widget : layout_.widgets)
    {
        if (widget.id == config.id)
        {
            widget = config;
            foundInLayout = true;
            break;
        }
    }
    if (!foundInLayout)
        return;

    const auto currentPanelId = layout_.activePanelId.empty() ? std::string("main") : layout_.activePanelId;
    const auto widgetPanelId = config.panelId.empty() ? std::string("main") : config.panelId;

    if (widgetPanelId != currentPanelId)
    {
        buildWidgets();
        resized();
        notifyLayoutChanged();
        return;
    }

    bool foundView = false;
    for (auto& ov : outputViews_)
    {
        if (auto* wrapper = dynamic_cast<DesignItemWrapper*>(ov.component.get()))
        {
            if (wrapper->config.id == config.id)
            {
                foundView = true;
                if (wrapper->config.type != config.type)
                {
                    buildWidgets();
                    resized();
                    notifyLayoutChanged();
                    return;
                }

                const bool signalChanged = (cleanSignalName(wrapper->config.signalName) != cleanSignalName(config.signalName));
                wrapper->config = config;
                ov.config = config;
                wrapper->setBounds(config.x, config.y, config.width, config.height);

                if (auto* inner = wrapper->getWidget())
                {
                    if (auto* g = dynamic_cast<AnalogGaugeComponent*>(inner))
                    {
                        g->updateConfig(config, signalChanged);
                        if (session_)
                        {
                            const auto snap = session_->getSnapshot();
                            double val = 0.0;
                            if (resolveNumericSignal(snap, config.signalName, val))
                                g->setValue(val);
                        }
                    }
                    else if (auto* dr = dynamic_cast<DigitalReadoutComponent*>(inner))
                    {
                        dr->updateConfig(config, signalChanged);
                        if (session_)
                        {
                            const auto snap = session_->getSnapshot();
                            double val = 0.0;
                            bool bVal = false;
                            if (resolveNumericSignal(snap, config.signalName, val))
                                dr->setValue(val);
                            else if (resolveBooleanSignal(snap, config.signalName, bVal))
                                dr->setBooleanValue(bVal);
                        }
                    }
                    else if (auto* bg = dynamic_cast<BarGaugeComponent*>(inner))
                    {
                        bg->updateConfig(config, signalChanged);
                        if (session_)
                        {
                            const auto snap = session_->getSnapshot();
                            double val = 0.0;
                            if (resolveNumericSignal(snap, config.signalName, val))
                                bg->setValue(val);
                        }
                    }
                    else if (auto* lamp = dynamic_cast<IndicatorLampComponent*>(inner))
                    {
                        lamp->updateConfig(config, signalChanged);
                        if (session_)
                        {
                            const auto snap = session_->getSnapshot();
                            bool bVal = false;
                            double val = 0.0;
                            if (resolveBooleanSignal(snap, config.signalName, bVal))
                                lamp->setActive(bVal);
                            else if (resolveNumericSignal(snap, config.signalName, val))
                                lamp->setActive(val > 0.5);
                        }
                    }
                    else if (auto* sld = dynamic_cast<LabeledSliderComponent*>(inner))
                    {
                        sld->updateConfig(config);
                    }
                    else if (auto* btn = dynamic_cast<juce::Button*>(inner))
                    {
                        btn->setButtonText(juce::String(config.title));
                    }
                    inner->repaint();
                }
                wrapper->repaint();
                break;
            }
        }
        else if (auto* plot = dynamic_cast<PlotPanelWidget*>(ov.component.get()))
        {
            if (plot->widgetId() == config.id)
            {
                foundView = true;
                ov.config = config;
                plot->updateDisplay();
                break;
            }
        }
    }

    if (!foundView)
    {
        buildWidgets();
        resized();
    }

    notifyLayoutChanged();
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
    if (!canvasComponent)
        return;

    deselectAll();
    canvasComponent->removeAllChildren();
    if (plotContainer_)
        plotContainer_->removeAllChildren();
    outputViews_.clear();

    const auto panelId = layout_.activePanelId.empty() ? std::string("main") : layout_.activePanelId;
    layout_.ensureDefaultPanel();
    layout_.ensureWidgetBounds();

    for (const auto& w : layout_.widgets)
    {
        const auto widgetPanelId = w.panelId.empty() ? std::string("main") : w.panelId;
        if (widgetPanelId != panelId)
            continue;

        std::unique_ptr<juce::Component> comp;

        if (w.type == WidgetType::PushButton)
        {
            if (w.momentary)
            {
                auto btn = std::make_unique<juce::TextButton>(juce::String(w.title));
                const auto nameLower = juce::String(w.title).toLowerCase() + " " + juce::String(w.id).toLowerCase();
                juce::Colour btnCol(0xff223344);
                juce::Colour textCol(0xffe6f0f8);
                if (nameLower.contains("start"))
                {
                    btnCol = juce::Colour(0xff1b5e20);
                    textCol = juce::Colour(0xffe8f5e9);
                }
                else if (nameLower.contains("stop"))
                {
                    btnCol = juce::Colour(0xff7f1d1d);
                    textCol = juce::Colour(0xffffebee);
                }
                else if (nameLower.contains("reset"))
                {
                    btnCol = juce::Colour(0xff2b3a4a);
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
                comp = std::move(btn);
            }
            else
            {
                auto indBtn = std::make_unique<IndicatorButtonComponent>(w.title, true);
                const auto sig = w.signalName;
                const bool initState = activeBooleanControlValues_.count(sig) != 0
                                     ? activeBooleanControlValues_[sig]
                                     : (w.defaultValue > 0.5);
                indBtn->setLatched(initState);
                indBtn->onStateChange = [this, sig](bool latched) {
                    activeBooleanControlValues_[sig] = latched;
                    if (session_)
                        session_->setBooleanInput(sig, latched);
                };
                comp = std::move(indBtn);
            }
        }
        else if (w.type == WidgetType::LatchingButton)
        {
            auto indBtn = std::make_unique<IndicatorButtonComponent>(w.title, true);
            const auto sig = w.signalName;
            const bool initState = activeBooleanControlValues_.count(sig) != 0
                                 ? activeBooleanControlValues_[sig]
                                 : (w.defaultValue > 0.5);
            indBtn->setLatched(initState);
            indBtn->onStateChange = [this, sig](bool latched) {
                activeBooleanControlValues_[sig] = latched;
                if (session_)
                    session_->setBooleanInput(sig, latched);
            };
            comp = std::move(indBtn);
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
            comp = std::move(tog);
        }
        else if (w.type == WidgetType::Knob || w.type == WidgetType::Slider)
        {
            auto ctrl = std::make_unique<LabeledSliderComponent>(w.title, w.unit, w.type == WidgetType::Knob, w.vertical);
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
            comp = std::move(ctrl);
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
            comp = std::move(estop);
        }
        else if (w.type == WidgetType::ModeSelector)
        {
            auto sel = std::make_unique<SelectorSwitchComponent>(w.title, w.options);
            const auto sig = w.signalName;
            const int initVal = activeNumericControlValues_.count(sig) != 0
                              ? (int)activeNumericControlValues_[sig]
                              : (int)w.defaultValue;
            sel->setSelectedIndex(initVal);
            sel->onSelectionChanged = [this, sig](int idx) {
                activeNumericControlValues_[sig] = (double)idx;
                if (session_)
                    session_->setNumericInput(sig, (double)idx);
            };
            comp = std::move(sel);
        }
        else if (w.type == WidgetType::IndicatorButton)
        {
            auto indBtn = std::make_unique<IndicatorButtonComponent>(w.title, !w.momentary);
            const auto sig = w.signalName;
            indBtn->onStateChange = [this, sig](bool active) {
                activeBooleanControlValues_[sig] = active;
                if (session_)
                    session_->setBooleanInput(sig, active);
            };
            comp = std::move(indBtn);
        }
        else if (w.type == WidgetType::AnalogGauge)
        {
            comp = std::make_unique<AnalogGaugeComponent>(
                w.title, w.unit, w.minValue, w.maxValue, w.precision,
                w.hasWarnRange, w.warnMin, w.warnMax,
                w.hasCritRange, w.critMin, w.critMax);
        }
        else if (w.type == WidgetType::DigitalReadout || w.type == WidgetType::ClockDisplay)
        {
            comp = std::make_unique<DigitalReadoutComponent>(
                w.title, w.unit, w.precision,
                w.hasWarnRange, w.warnMin, w.warnMax,
                w.hasCritRange, w.critMin, w.critMax);
        }
        else if (w.type == WidgetType::BarGauge)
        {
            comp = std::make_unique<BarGaugeComponent>(
                w.title, w.unit, w.minValue, w.maxValue, w.vertical, w.precision,
                w.hasWarnRange, w.warnMin, w.warnMax,
                w.hasCritRange, w.critMin, w.critMax);
        }
        else if (w.type == WidgetType::IndicatorLamp)
        {
            juce::Colour c = (w.id.find("fault") != std::string::npos || w.title.find("Fault") != std::string::npos)
                           ? juce::Colour(0xffff3333) : juce::Colour(0xff33dd55);
            comp = std::make_unique<IndicatorLampComponent>(w.title, c);
        }
        else if (w.type == WidgetType::TrendChart)
        {
            comp = std::make_unique<TrendChartComponent>();
        }
        else if (w.type == WidgetType::PlotPanel)
        {
            auto plotWidget = std::make_unique<PlotPanelWidget>(
                w, recordingFor(w.id), recordingEnabledFor(w.id),
                [this](const WidgetConfig& config) { updateWidgetConfig(config); });
            if (plotContainer_)
            {
                plotContainer_->addAndMakeVisible(plotWidget.get());
                plotWidget->setBounds(plotContainer_->getLocalBounds());
            }
            outputViews_.push_back({ w, std::move(plotWidget) });
            continue;
        }

        if (comp != nullptr)
        {
            auto wrapper = std::make_unique<DesignItemWrapper>(w, std::move(comp));
            wrapper->setBounds(w.x, w.y, w.width, w.height);
            wrapper->setDesignMode(designMode_);

            wrapper->onMouseDownAction = [this](DesignItemWrapper* item, const juce::MouseEvent& e) {
                canvasComponent->selectWrapper(item, e.mods.isShiftDown() || e.mods.isCommandDown());
            };
            wrapper->onMouseDragAction = [this](DesignItemWrapper* item, const juce::MouseEvent& e) {
                const auto selected = canvasComponent->getSelectedWrappers();
                const auto cur = e.getEventRelativeTo(canvasComponent.get()).getPosition();
                const auto start = e.getMouseDownPosition() + item->getPosition();
                const int dx = cur.x - start.x;
                const int dy = cur.y - start.y;
                for (auto* sw : selected)
                {
                    const int nx = std::max(0, sw->config.x + dx);
                    const int ny = std::max(0, sw->config.y + dy);
                    sw->setTopLeftPosition(nx, ny);
                }
            };
            wrapper->onMouseUpAction = [this](DesignItemWrapper*, const juce::MouseEvent&) {
                for (auto* sw : canvasComponent->getSelectedWrappers())
                {
                    sw->config.x = sw->getX();
                    sw->config.y = sw->getY();
                    sw->config.width = sw->getWidth();
                    sw->config.height = sw->getHeight();
                    for (auto& wcfg : layout_.widgets)
                    {
                        if (wcfg.id == sw->config.id)
                        {
                            wcfg.x = sw->config.x;
                            wcfg.y = sw->config.y;
                            wcfg.width = sw->config.width;
                            wcfg.height = sw->config.height;
                            break;
                        }
                    }
                }
                notifyLayoutChanged();
            };
            wrapper->onOpenProperties = [this](DesignItemWrapper* item) {
                showWidgetPropertiesDialog(item->config.id);
            };
            wrapper->onDelete = [this](DesignItemWrapper* item) {
                removeWidget(item->config.id);
            };
            wrapper->onDuplicate = [this](DesignItemWrapper* item) {
                auto dup = item->config;
                dup.id = dup.id + "_copy";
                dup.x += 24;
                dup.y += 24;
                addWidget(dup);
            };
            wrapper->onBringToFront = [this](DesignItemWrapper* item) {
                item->toFront(false);
                notifyLayoutChanged();
            };
            wrapper->onSendToBack = [this](DesignItemWrapper* item) {
                item->toBack();
                notifyLayoutChanged();
            };

            canvasComponent->addAndMakeVisible(*wrapper);
            outputViews_.push_back({ w, std::unique_ptr<juce::Component>(wrapper.release()) });
        }
    }
    publishAllControlInputs();
}

void ControlPanelComponent::resized()
{
    auto b = getLocalBounds().reduced(8);

    // Top Transport Bar
    auto topBar = b.removeFromTop(36);
    runButton.setBounds(topBar.removeFromLeft(54).reduced(2));
    pauseButton.setBounds(topBar.removeFromLeft(58).reduced(2));
    stepButton.setBounds(topBar.removeFromLeft(54).reduced(2));
    stopButton.setBounds(topBar.removeFromLeft(54).reduced(2));
    resetButton.setBounds(topBar.removeFromLeft(58).reduced(2));
    topBar.removeFromLeft(8);
    rateBox.setBounds(topBar.removeFromLeft(125).reduced(2));
    topBar.removeFromLeft(6);
    panelBox.setBounds(topBar.removeFromLeft(130).reduced(2));
    newPanelButton.setBounds(topBar.removeFromLeft(74).reduced(2));
    renamePanelButton.setBounds(topBar.removeFromLeft(64).reduced(2));
    removePanelButton.setBounds(topBar.removeFromLeft(64).reduced(2));
    saveButton.setBounds(topBar.removeFromLeft(54).reduced(2));
    topBar.removeFromLeft(8);
    modeButton.setBounds(topBar.removeFromLeft(95).reduced(2));
    if (designMode_)
    {
        addWidgetButton.setBounds(topBar.removeFromLeft(95).reduced(2));
        alignButton.setBounds(topBar.removeFromLeft(58).reduced(2));
    }
    topBar.removeFromLeft(8);
    statusLabel.setBounds(topBar);

    b.removeFromTop(6);

    const bool showPlot = hasPlotPanel();
    if (splitter_)
        splitter_->setVisible(showPlot);
    if (plotContainer_)
        plotContainer_->setVisible(showPlot);

    if (showPlot && splitter_ && plotContainer_)
    {
        const auto& panel = activePanel();
        const double ratio = std::clamp(panel.splitRatio > 0.0 ? panel.splitRatio : 0.22, 0.15, 0.85);
        const int splitterW = 6;
        const int availW = b.getWidth() - splitterW;
        const int minControlW = 180;
        const int minPlotW = 320;

        int controlW = std::clamp((int)std::round((double)availW * ratio), minControlW, std::max(minControlW, availW - minPlotW));

        auto leftArea = b.removeFromLeft(controlW);
        auto splitArea = b.removeFromLeft(splitterW);
        auto rightArea = b;

        canvasViewport.setBounds(leftArea);
        splitter_->setBounds(splitArea);
        plotContainer_->setBounds(rightArea);
        for (int i = 0; i < plotContainer_->getNumChildComponents(); ++i)
        {
            if (auto* child = plotContainer_->getChildComponent(i))
                child->setBounds(plotContainer_->getLocalBounds());
        }
    }
    else
    {
        canvasViewport.setBounds(b);
    }

    int canvasW = std::max(1600, activePanel().canvasWidth);
    int canvasH = std::max(1000, activePanel().canvasHeight);
    for (const auto& w : layout_.widgets)
    {
        if (w.type == WidgetType::PlotPanel)
            continue;
        canvasW = std::max(canvasW, w.x + w.width + 100);
        canvasH = std::max(canvasH, w.y + w.height + 100);
    }
    if (canvasComponent)
        canvasComponent->setSize(std::max(canvasW, canvasViewport.getWidth()),
                                 std::max(canvasH, canvasViewport.getHeight()));
}

void ControlPanelComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff161a22));
    g.setColour(juce::Colour(0xff2b3442));
    g.drawRect(getLocalBounds(), 1.0f);
}

void ControlPanelComponent::mouseDown(const juce::MouseEvent&) {}
void ControlPanelComponent::mouseDrag(const juce::MouseEvent&) {}
void ControlPanelComponent::mouseUp(const juce::MouseEvent&) {}
void ControlPanelComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) {}

void ControlPanelComponent::setStatusText(const juce::String& text, juce::Colour colour, int durationTicks)
{
    transientStatusText_ = text;
    transientStatusColour_ = colour;
    transientStatusTicks_ = std::max(1, durationTicks);
    statusLabel.setColour(juce::Label::textColourId, transientStatusColour_);
    statusLabel.setText(transientStatusText_, juce::dontSendNotification);
}

bool ControlPanelComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == 'S' && key.getModifiers().isCommandDown())
    {
        saveButton.triggerClick();
        return true;
    }
    return false;
}

void ControlPanelComponent::handleSplitterDragStart()
{
    dragStartControlW_ = canvasViewport.getWidth();
    dragStartAvailableW_ = canvasViewport.getWidth() + (plotContainer_ ? plotContainer_->getWidth() : 0);
}

void ControlPanelComponent::handleSplitterDrag(int deltaX)
{
    if (!hasPlotPanel())
        return;

    if (dragStartAvailableW_ <= 100)
        handleSplitterDragStart();

    if (dragStartAvailableW_ <= 100)
        return;

    const int minControlW = 180;
    const int minPlotW = 320;
    int newControlW = std::clamp(dragStartControlW_ + deltaX, minControlW, std::max(minControlW, dragStartAvailableW_ - minPlotW));
    double newRatio = (double)newControlW / (double)dragStartAvailableW_;
    newRatio = std::clamp(newRatio, 0.15, 0.85);

    if (auto* panel = layout_.findPanel(layout_.activePanelId))
        panel->splitRatio = newRatio;

    resized();
}

void ControlPanelComponent::handleSplitterDragEnd()
{
    notifyLayoutChanged();
}

double ControlPanelComponent::getSplitRatio() const
{
    const double r = activePanel().splitRatio;
    return r > 0.0 ? r : 0.22;
}

bool ControlPanelComponent::setSplitRatio(double ratio)
{
    ratio = std::clamp(ratio, 0.15, 0.85);
    if (auto* panel = layout_.findPanel(layout_.activePanelId))
    {
        panel->splitRatio = ratio;
        resized();
        notifyLayoutChanged();
        return true;
    }
    return false;
}

int ControlPanelComponent::getSplitterX() const
{
    return splitter_ ? splitter_->getX() : 0;
}

juce::Component* ControlPanelComponent::getSplitterComponent() const
{
    return splitter_.get();
}

juce::Component* ControlPanelComponent::getPlotContainerComponent() const
{
    return plotContainer_.get();
}

juce::Rectangle<int> ControlPanelComponent::getCanvasViewportBounds() const
{
    return canvasViewport.getBounds();
}

juce::Rectangle<int> ControlPanelComponent::getPlotContainerBounds() const
{
    return plotContainer_ ? plotContainer_->getBounds() : juce::Rectangle<int>();
}

bool ControlPanelComponent::hasPlotPanel() const
{
    const auto panelId = layout_.activePanelId.empty() ? std::string("main") : layout_.activePanelId;
    for (const auto& w : layout_.widgets)
    {
        const auto widgetPanelId = w.panelId.empty() ? std::string("main") : w.panelId;
        if (widgetPanelId == panelId && w.type == WidgetType::PlotPanel)
            return true;
    }
    return false;
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
        if (const auto* plot = getPlotWidgetFromComp(ov.component.get()))
            out.push_back(plot->getSnapshot());
    }
    return out;
}

bool ControlPanelComponent::setRecorderTimebaseSecondsPerDiv(const std::string& widgetId, double secondsPerDiv)
{
    for (auto& ov : outputViews_)
    {
        if (auto* plot = getPlotWidgetFromComp(ov.component.get()))
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
        if (auto* plot = getPlotWidgetFromComp(ov.component.get()))
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
        if (auto* plot = getPlotWidgetFromComp(ov.component.get()))
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
        if (auto* plot = getPlotWidgetFromComp(ov.component.get()))
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
        if (auto* plot = getPlotWidgetFromComp(ov.component.get()))
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
        if (auto* plot = getPlotWidgetFromComp(ov.component.get()))
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
        if (auto* plot = getPlotWidgetFromComp(ov.component.get()))
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
        if (auto* plot = getPlotWidgetFromComp(ov.component.get()))
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
        if (auto* plot = getPlotWidgetFromComp(ov.component.get()))
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
        if (const auto* plot = getPlotWidgetFromComp(ov.component.get()))
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
    if (transientStatusTicks_ > 0)
    {
        --transientStatusTicks_;
        statusLabel.setColour(juce::Label::textColourId, transientStatusColour_);
        statusLabel.setText(transientStatusText_, juce::dontSendNotification);
    }
    else if (snap.faulted)
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
    double targetSpeed = 300.0;
    if (resolveNumericSignal(snap, "system.speed_setpoint", targetSpeed)
        || resolveNumericSignal(snap, "controller.setpoint", targetSpeed)
        || resolveNumericSignal(snap, "speed_setpoint", targetSpeed)
        || resolveNumericSignal(snap, "motorA.speed", targetSpeed))
    {
    }

    double actualSpeed = 0.0;
    if (resolveNumericSignal(snap, "motor.speed", actualSpeed)
        || resolveNumericSignal(snap, "motorB.speed", actualSpeed)
        || resolveNumericSignal(snap, "motorA.speed", actualSpeed)
        || resolveNumericSignal(snap, "speed", actualSpeed))
    {
    }

    trendHistory_.push_back({ snap.simTime, targetSpeed, actualSpeed });
    while (trendHistory_.size() > 400 || (trendHistory_.size() > 50 && (trendHistory_.back().time - trendHistory_.front().time) > 10.0))
        trendHistory_.pop_front();

    for (const auto& widget : layout_.widgets)
    {
        if (widget.type == WidgetType::PlotPanel && recordingEnabledFor(widget.id))
            recordingFor(widget.id).append(snap, widget);
    }

    // Update Output Views & Instruments
    for (auto& ov : outputViews_)
    {
        if (auto* plot = getPlotWidgetFromComp(ov.component.get()))
        {
            plot->updateDisplay();
            continue;
        }

        auto* wrapper = dynamic_cast<DesignItemWrapper*>(ov.component.get());
        if (!wrapper) continue;
        auto* comp = wrapper->getWidget();
        if (!comp) continue;
        const auto& w = wrapper->config;

        if (w.type == WidgetType::AnalogGauge)
        {
            if (auto* g = dynamic_cast<AnalogGaugeComponent*>(comp))
            {
                double v = 0.0;
                if (resolveNumericSignal(snap, w.signalName, v))
                    g->setValue(v);
            }
        }
        else if (w.type == WidgetType::DigitalReadout)
        {
            if (auto* dr = dynamic_cast<DigitalReadoutComponent*>(comp))
            {
                double v = 0.0;
                bool b = false;
                if (resolveNumericSignal(snap, w.signalName, v))
                    dr->setValue(v);
                else if (resolveBooleanSignal(snap, w.signalName, b))
                    dr->setBooleanValue(b);
            }
        }
        else if (w.type == WidgetType::ClockDisplay)
        {
            if (auto* dr = dynamic_cast<DigitalReadoutComponent*>(comp))
            {
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(3) << snap.simTime;
                dr->setText(ss.str());
            }
        }
        else if (w.type == WidgetType::BarGauge)
        {
            if (auto* bg = dynamic_cast<BarGaugeComponent*>(comp))
            {
                double v = 0.0;
                if (resolveNumericSignal(snap, w.signalName, v))
                    bg->setValue(v);
            }
        }
        else if (w.type == WidgetType::IndicatorLamp)
        {
            if (auto* lamp = dynamic_cast<IndicatorLampComponent*>(comp))
            {
                bool b = false;
                double v = 0.0;
                if (resolveBooleanSignal(snap, w.signalName, b))
                    lamp->setActive(b);
                else if (resolveNumericSignal(snap, w.signalName, v))
                    lamp->setActive(v > 0.5);
            }
        }
        else if (w.type == WidgetType::IndicatorButton)
        {
            if (auto* ib = dynamic_cast<IndicatorButtonComponent*>(comp))
            {
                const auto& indSig = w.secondarySignal.empty() ? w.signalName : w.secondarySignal;
                bool b = false;
                double v = 0.0;
                if (resolveBooleanSignal(snap, indSig, b))
                    ib->setIndicatorActive(b);
                else if (resolveNumericSignal(snap, indSig, v))
                    ib->setIndicatorActive(v > 0.5);
            }
        }
        else if (w.type == WidgetType::TrendChart)
        {
            if (auto* tc = dynamic_cast<TrendChartComponent*>(comp))
                tc->updateData(trendHistory_);
        }
        else if (w.type == WidgetType::PlotPanel)
        {
            if (auto* plot = dynamic_cast<PlotPanelWidget*>(comp))
                plot->updateDisplay();
        }
    }
}

} // namespace sim_panel
