#include "PlotInstrumentView.h"

#include <cmath>

namespace pi = plot_instrument;

namespace
{
const juce::Colour background { 0xff0c1116 };
const juce::Colour gridColour { 0xff1f2b33 };
const juce::Colour frameColour { 0xff3a4a55 };
const juce::Colour textColour { 0xff9fb3bc };
const juce::Colour traceColours[] { juce::Colour(0xffffd166), juce::Colour(0xff4cc9f0), juce::Colour(0xfff72585),
                                    juce::Colour(0xff80ed99), juce::Colour(0xffb8a1ff), juce::Colour(0xffff8c42) };
const juce::Colour axisColours[] { juce::Colour(0xffff6b6b), juce::Colour(0xff6bcB77), juce::Colour(0xff4d96ff) };

// Early samples cyan, late samples orange: shows the direction of travel.
juce::Colour chronological(float f)
{
    return juce::Colour(0xff35c6ff).interpolatedWith(juce::Colour(0xffffa630), juce::jlimit(0.0f, 1.0f, f));
}

juce::String axisTitle(const pi::Acquisition& a, int signal)
{
    return a.labels[(size_t)signal] + " [" + a.units[(size_t)signal] + "]";
}

juce::String tickText(double v, const juce::String& unit)
{
    return analytics::formatNumber(v, unit, 3);
}

constexpr int chunks = 32; // colour steps along a trajectory
}

PlotSurface::PlotSurface()
{
    setOpaque(true);
}

void PlotSurface::setAcquisition(std::shared_ptr<const pi::Acquisition> acquisition, pi::Mode newMode, Axes newAxes)
{
    acq = std::move(acquisition);
    mode = newMode;
    axes = std::move(newAxes);
    indices = acq != nullptr && acq->ok ? pi::displayIndices(*acq, axes.signals, displayBudget) : std::vector<int>();
    repaint();
}

void PlotSurface::setMessage(const juce::String& text)
{
    message = text;
    repaint();
}

void PlotSurface::setMarkers(bool on)
{
    markers = on;
    repaint();
}

void PlotSurface::setCamera(const pi::Camera& camera)
{
    if (dragging)
        return; // the user is moving it
    view = camera;
    repaint();
}

void PlotSurface::resetView()
{
    const auto perspective = view.perspective;
    view = {};
    view.perspective = perspective;
    repaint();
    if (onViewCommitted)
        onViewCommitted(view);
}

pi::Range PlotSurface::visible(const pi::Range& r, bool horizontal) const
{
    const auto span = (r.max - r.min) / std::max(0.05, view.zoom);
    const auto centre = (r.min + r.max) * 0.5 + (horizontal ? -view.panX : view.panY) * span;
    return { centre - span * 0.5, centre + span * 0.5, true };
}

void PlotSurface::paint(juce::Graphics& g)
{
    g.fillAll(background);
    auto area = getLocalBounds().toFloat().reduced(6.0f);
    const bool ready = acq != nullptr && acq->ok && !indices.empty() && axes.ranges.size() >= 2;
    if (!ready)
    {
        g.setColour(textColour);
        g.setFont(14.0f);
        g.drawFittedText(message, getLocalBounds().reduced(20), juce::Justification::centred, 6);
        return;
    }
    if (mode == pi::Mode::XYZ && axes.signals.size() == 3 && axes.ranges.size() == 3)
        paint3D(g, area);
    else
        paint2D(g, area);
}

void PlotSurface::paint2D(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& a = *acq;
    const bool timeMode = mode == pi::Mode::Time;
    auto plot = area.withTrimmedLeft(70.0f).withTrimmedBottom(40.0f).withTrimmedTop(timeMode ? 8.0f + 16.0f * (float)axes.signals.size() : 8.0f).withTrimmedRight(10.0f);
    if (plot.getWidth() < 40.0f || plot.getHeight() < 40.0f)
        return;
    const auto xr = visible(axes.ranges[0], true), yr = visible(axes.ranges[1], false);
    auto sx = [&](double v) { return plot.getX() + (float)((v - xr.min) / (xr.max - xr.min)) * plot.getWidth(); };
    auto sy = [&](double v) { return plot.getBottom() - (float)((v - yr.min) / (yr.max - yr.min)) * plot.getHeight(); };
    const auto xUnit = timeMode ? juce::String("s") : a.units[(size_t)axes.signals[0]];
    const auto yUnit = timeMode ? a.units[(size_t)axes.signals[0]] : a.units[(size_t)axes.signals[1]];

    g.setFont(11.0f);
    for (auto t : pi::niceTicks(xr.min, xr.max, 8))
    {
        g.setColour(gridColour);
        g.drawVerticalLine((int)sx(t), plot.getY(), plot.getBottom());
        g.setColour(textColour);
        g.drawText(tickText(t, xUnit), juce::Rectangle<float>(sx(t) - 40.0f, plot.getBottom() + 2.0f, 80.0f, 14.0f), juce::Justification::centred);
    }
    for (auto t : pi::niceTicks(yr.min, yr.max, 6))
    {
        g.setColour(gridColour);
        g.drawHorizontalLine((int)sy(t), plot.getX(), plot.getRight());
        g.setColour(textColour);
        g.drawText(tickText(t, yUnit), juce::Rectangle<float>(area.getX(), sy(t) - 7.0f, 66.0f, 14.0f), juce::Justification::centredRight);
    }
    g.setColour(frameColour);
    g.drawRect(plot, 1.0f);
    g.setColour(textColour);
    g.setFont(12.0f);
    g.drawText(timeMode ? juce::String("Time [s]") : "X: " + axisTitle(a, axes.signals[0]),
               juce::Rectangle<float>(plot.getX(), plot.getBottom() + 18.0f, plot.getWidth(), 16.0f), juce::Justification::centred);
    if (!timeMode)
    {
        juce::GlyphArrangement title;
        title.addLineOfText(g.getCurrentFont(), "Y: " + axisTitle(a, axes.signals[1]), 0.0f, 0.0f);
        const auto w = title.getBoundingBox(0, -1, true).getWidth();
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi)
                           .translated(area.getX() + 10.0f, plot.getCentreY() + w * 0.5f));
        title.draw(g);
        g.restoreState();
    }

    g.saveState();
    g.reduceClipRegion(plot.toNearestInt());
    if (timeMode)
    {
        for (size_t k = 0; k < axes.signals.size(); ++k)
        {
            const auto& values = a.values[(size_t)axes.signals[k]];
            juce::Path path;
            bool pen = false;
            for (auto i : indices)
            {
                const auto v = values[(size_t)i];
                if (!std::isfinite(v)) { pen = false; continue; }
                const juce::Point<float> p { sx(a.time[(size_t)i]), sy(v) };
                if (pen) path.lineTo(p); else path.startNewSubPath(p);
                pen = true;
            }
            const auto colour = traceColours[k % 6];
            g.setColour(colour);
            g.strokePath(path, juce::PathStrokeType(1.4f));
            if (markers)
                for (size_t n = 0; n < indices.size(); n += std::max<size_t>(1, indices.size() / 4000))
                    if (std::isfinite(values[(size_t)indices[n]]))
                        g.fillRect(sx(a.time[(size_t)indices[n]]) - 1.5f, sy(values[(size_t)indices[n]]) - 1.5f, 3.0f, 3.0f);
        }
    }
    else
    {
        const auto& xs = a.values[(size_t)axes.signals[0]];
        const auto& ys = a.values[(size_t)axes.signals[1]];
        const auto per = std::max<size_t>(1, indices.size() / chunks);
        for (size_t start = 0; start + 1 < indices.size(); start += per)
        {
            juce::Path path;
            bool pen = false;
            for (size_t n = start; n < std::min(indices.size(), start + per + 1); ++n)
            {
                const auto i = (size_t)indices[n];
                if (!std::isfinite(xs[i]) || !std::isfinite(ys[i])) { pen = false; continue; }
                const juce::Point<float> p { sx(xs[i]), sy(ys[i]) };
                if (pen) path.lineTo(p); else path.startNewSubPath(p);
                pen = true;
            }
            g.setColour(chronological((float)start / (float)indices.size()));
            g.strokePath(path, juce::PathStrokeType(1.3f));
        }
        if (markers)
        {
            g.setColour(juce::Colours::white.withAlpha(0.75f));
            for (size_t n = 0; n < indices.size(); n += std::max<size_t>(1, indices.size() / 4000))
            {
                const auto i = (size_t)indices[n];
                if (std::isfinite(xs[i]) && std::isfinite(ys[i]))
                    g.fillRect(sx(xs[i]) - 1.5f, sy(ys[i]) - 1.5f, 3.0f, 3.0f);
            }
        }
    }
    g.restoreState();

    if (timeMode)
    {
        g.setFont(12.0f);
        for (size_t k = 0; k < axes.signals.size(); ++k)
        {
            g.setColour(traceColours[k % 6]);
            g.drawText(axisTitle(a, axes.signals[k]), juce::Rectangle<float>(plot.getX(), area.getY() + 16.0f * (float)k, plot.getWidth(), 16.0f),
                       juce::Justification::centredLeft);
        }
    }
}

void PlotSurface::paint3D(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& a = *acq;
    auto plot = area.withTrimmedTop(18.0f);
    auto norm = [&](int axis, double v) {
        const auto& r = axes.ranges[(size_t)axis];
        return (v - r.min) / (r.max - r.min) * 2.0 - 1.0;
    };
    auto at = [&](double x, double y, double z) {
        const auto p = pi::project(view, x, y, z, plot);
        return juce::Point<float>(p.x, p.y);
    };

    // Bounding box, then the three axes from the near-low corner.
    g.setColour(gridColour);
    for (int e = 0; e < 12; ++e)
    {
        const int axis = e / 4, k = e % 4;
        const double u = (k & 1) ? 1.0 : -1.0, w = (k & 2) ? 1.0 : -1.0;
        double p0[3], p1[3];
        p0[axis] = -1.0; p1[axis] = 1.0;
        p0[(axis + 1) % 3] = p1[(axis + 1) % 3] = u;
        p0[(axis + 2) % 3] = p1[(axis + 2) % 3] = w;
        g.drawLine({ at(p0[0], p0[1], p0[2]), at(p1[0], p1[1], p1[2]) }, 1.0f);
    }
    g.setFont(11.0f);
    static const char* names[] { "X", "Y", "Z" };
    for (int axis = 0; axis < 3; ++axis)
    {
        double from[3] { -1.0, -1.0, -1.0 }, to[3] { -1.0, -1.0, -1.0 };
        to[axis] = 1.0;
        const auto p0 = at(from[0], from[1], from[2]), p1 = at(to[0], to[1], to[2]);
        g.setColour(axisColours[axis]);
        g.drawLine({ p0, p1 }, 1.6f);
        const auto& r = axes.ranges[(size_t)axis];
        const auto unit = a.units[(size_t)axes.signals[(size_t)axis]];
        for (auto t : pi::niceTicks(r.min, r.max, 4))
        {
            double q[3] { -1.0, -1.0, -1.0 };
            q[axis] = norm(axis, t);
            const auto p = at(q[0], q[1], q[2]);
            g.setColour(axisColours[axis].withAlpha(0.8f));
            g.fillEllipse(p.x - 1.5f, p.y - 1.5f, 3.0f, 3.0f);
            g.setColour(textColour);
            g.drawText(tickText(t, unit), juce::Rectangle<float>(p.x + 3.0f, p.y + 1.0f, 70.0f, 12.0f), juce::Justification::centredLeft);
        }
        g.setColour(axisColours[axis]);
        g.drawText(juce::String(names[axis]) + ": " + axisTitle(a, axes.signals[(size_t)axis]),
                   juce::Rectangle<float>(p1.x + 4.0f, p1.y - 16.0f, 260.0f, 14.0f), juce::Justification::centredLeft);
    }

    const auto& xs = a.values[(size_t)axes.signals[0]];
    const auto& ys = a.values[(size_t)axes.signals[1]];
    const auto& zs = a.values[(size_t)axes.signals[2]];
    auto finite = [&](size_t i) { return std::isfinite(xs[i]) && std::isfinite(ys[i]) && std::isfinite(zs[i]); };
    const auto per = std::max<size_t>(1, indices.size() / chunks);
    for (size_t start = 0; start + 1 < indices.size(); start += per)
    {
        juce::Path path;
        bool pen = false;
        for (size_t n = start; n < std::min(indices.size(), start + per + 1); ++n)
        {
            const auto i = (size_t)indices[n];
            if (!finite(i)) { pen = false; continue; }
            const auto p = at(norm(0, xs[i]), norm(1, ys[i]), norm(2, zs[i]));
            if (pen) path.lineTo(p); else path.startNewSubPath(p);
            pen = true;
        }
        g.setColour(chronological((float)start / (float)indices.size()));
        g.strokePath(path, juce::PathStrokeType(1.2f));
    }
    if (markers)
    {
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        for (size_t n = 0; n < indices.size(); n += std::max<size_t>(1, indices.size() / 4000))
        {
            const auto i = (size_t)indices[n];
            if (!finite(i)) continue;
            const auto p = at(norm(0, xs[i]), norm(1, ys[i]), norm(2, zs[i]));
            g.fillRect(p.x - 1.5f, p.y - 1.5f, 3.0f, 3.0f);
        }
    }
    g.setColour(textColour);
    g.setFont(11.0f);
    g.drawText("Drag: orbit   Shift/right-drag: pan   Wheel: zoom   Double-click: reset view   "
                   + juce::String(view.perspective ? "Perspective" : "Orthographic"),
               area.withHeight(16.0f), juce::Justification::centredLeft);
}

void PlotSurface::mouseDown(const juce::MouseEvent& e)
{
    dragLast = e.position;
    dragging = true;
}

void PlotSurface::mouseDrag(const juce::MouseEvent& e)
{
    const auto d = e.position - dragLast;
    dragLast = e.position;
    const bool pan = mode != pi::Mode::XYZ || e.mods.isShiftDown() || e.mods.isRightButtonDown();
    if (pan)
    {
        if (mode == pi::Mode::XYZ)
        {
            const auto m = std::max(1.0f, (float)std::min(getWidth(), getHeight()));
            view.panX += d.x / m;
            view.panY -= d.y / m;
        }
        else
        {
            view.panX += d.x / std::max(1.0f, (float)getWidth() - 80.0f);
            view.panY += d.y / std::max(1.0f, (float)getHeight() - 50.0f);
        }
    }
    else
    {
        view.yaw = std::fmod(view.yaw + d.x * 0.4, 360.0);
        view.pitch = juce::jlimit(-89.0, 89.0, view.pitch + d.y * 0.4);
    }
    repaint();
}

void PlotSurface::mouseUp(const juce::MouseEvent&)
{
    if (!dragging)
        return;
    dragging = false;
    if (onViewCommitted)
        onViewCommitted(view);
}

void PlotSurface::mouseDoubleClick(const juce::MouseEvent&)
{
    resetView();
}

void PlotSurface::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    view.zoom = juce::jlimit(0.05, 50.0, view.zoom * std::pow(1.2, wheel.deltaY * 5.0));
    repaint();
    startTimer(400); // commit once the wheel stops
}

void PlotSurface::timerCallback()
{
    stopTimer();
    if (onViewCommitted)
        onViewCommitted(view);
}
