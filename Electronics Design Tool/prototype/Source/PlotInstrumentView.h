#pragma once

#include <JuceHeader.h>

#include "PlotInstrument.h"

#include <functional>
#include <memory>

// Draws a plotting instrument's acquisition: traces against time, an XY
// trajectory, or an XYZ trajectory in an orbitable 3D view. One surface for
// all three modes: the same thinned sample indices and axis ranges feed a 2D
// mapping or the 3D camera projection. View changes (orbit, pan, zoom) only
// redraw; they never touch the data or the circuit.
class PlotSurface final : public juce::Component, private juce::Timer
{
public:
    struct Axes
    {
        // Indices into the acquisition: X, Y(, Z) for XY/XYZ, every trace for Time.
        std::vector<int> signals;
        // Data ranges: X, Y(, Z); for Time, [0] is the time window and [1] the value range.
        std::vector<plot_instrument::Range> ranges;
    };

    PlotSurface();

    void setAcquisition(std::shared_ptr<const plot_instrument::Acquisition> acquisition, plot_instrument::Mode mode, Axes axes);
    void setMessage(const juce::String& text); // shown when there is nothing to plot
    void setMarkers(bool on);
    void setCamera(const plot_instrument::Camera& camera);
    const plot_instrument::Camera& camera() const { return view; }
    void resetView();
    int displayedPoints() const { return (int)indices.size(); }

    // Called when the user finishes changing the view (mouse up, end of wheel).
    std::function<void(const plot_instrument::Camera&)> onViewCommitted;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    static constexpr int displayBudget = 24000;

private:
    void timerCallback() override;
    void paint2D(juce::Graphics& g, juce::Rectangle<float> area);
    void paint3D(juce::Graphics& g, juce::Rectangle<float> area);
    plot_instrument::Range visible(const plot_instrument::Range& r, bool horizontal) const;

    std::shared_ptr<const plot_instrument::Acquisition> acq;
    plot_instrument::Mode mode = plot_instrument::Mode::XY;
    Axes axes;
    std::vector<int> indices;
    plot_instrument::Camera view;
    juce::String message { "No data yet." };
    bool markers = false;
    juce::Point<float> dragLast;
    bool dragging = false;
};
