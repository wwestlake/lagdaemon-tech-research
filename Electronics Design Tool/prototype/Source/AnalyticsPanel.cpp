#include "AnalyticsPanel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const juce::Colour background(0xff10161d), panelColour(0xff151a20), raised(0xff1d2731), border(0xff33424d);
const juce::Colour textColour(0xffdce9ee), muted(0xff93a7b0), faint(0xff71808c), accent(0xff78dcca), warning(0xffffb36b);
const juce::Colour gridColour(0xff202b35), gridMinor(0xff19222a);

juce::Colour traceColour(int i)
{
    static const juce::uint32 colours[] = { 0xff5ec8e5, 0xfff2a65a, 0xff8fd16b, 0xffe56b8f, 0xffc49cf0,
                                            0xfff0d264, 0xff4fd1b0, 0xffe58c5e, 0xff8aa4f5, 0xffd0d0d0 };
    return juce::Colour(colours[(size_t)(i % 10)]);
}

void styleButton(juce::TextButton& b, bool primary = false)
{
    b.setColour(juce::TextButton::buttonColourId, primary ? juce::Colour(0xff2f7f73) : raised);
    b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff23394a));
    b.setColour(juce::TextButton::textColourOffId, primary ? juce::Colours::white : textColour);
    b.setColour(juce::TextButton::textColourOnId, accent);
}

void styleCombo(juce::ComboBox& c)
{
    c.setColour(juce::ComboBox::backgroundColourId, background);
    c.setColour(juce::ComboBox::outlineColourId, border);
    c.setColour(juce::ComboBox::textColourId, textColour);
    c.setColour(juce::ComboBox::arrowColourId, muted);
}

void styleEditor(juce::TextEditor& e)
{
    e.setMultiLine(false);
    e.setFont(juce::Font("Consolas", 13.5f, juce::Font::plain));
    e.setColour(juce::TextEditor::backgroundColourId, background);
    e.setColour(juce::TextEditor::outlineColourId, border);
    e.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff5aa7c8));
    e.setColour(juce::TextEditor::textColourId, textColour);
}

void styleLabel(juce::Label& l, float size, juce::Colour colour, bool bold = false)
{
    l.setFont(juce::Font(size, bold ? juce::Font::bold : juce::Font::plain));
    l.setColour(juce::Label::textColourId, colour);
    l.setMinimumHorizontalScale(1.0f);
}

juce::String eng(double v, const juce::String& unit, int significant = 4)
{
    return analytics::formatNumber(v, unit, significant);
}

std::vector<double> niceTicks(double lo, double hi, int target)
{
    std::vector<double> ticks;
    const auto span = hi - lo;
    if (!(span > 0.0) || !std::isfinite(span))
        return ticks;
    const auto raw = span / std::max(1, target);
    const auto magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const auto norm = raw / magnitude;
    const auto step = (norm < 1.5 ? 1.0 : norm < 3.0 ? 2.0 : norm < 7.0 ? 5.0 : 10.0) * magnitude;
    for (auto v = std::ceil(lo / step) * step; v <= hi + step * 1e-9 && ticks.size() < 200; v += step)
        ticks.push_back(std::abs(v) < step * 1e-9 ? 0.0 : v);
    return ticks;
}

double nan() { return std::numeric_limits<double>::quiet_NaN(); }

double interpolateAt(const analytics::Trace& t, double x)
{
    if (t.x.empty() || x < std::min(t.x.front(), t.x.back()) || x > std::max(t.x.front(), t.x.back()))
        return nan();
    const auto it = std::lower_bound(t.x.begin(), t.x.end(), x);
    if (it == t.x.begin()) return t.y.front();
    if (it == t.x.end()) return t.y.back();
    const auto i = (size_t)(it - t.x.begin());
    const auto x0 = t.x[i - 1], x1 = t.x[i];
    return x1 != x0 ? t.y[i - 1] + (t.y[i] - t.y[i - 1]) * (x - x0) / (x1 - x0) : t.y[i];
}

juce::File settingsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DjehutiElectronicsLab").getChildFile("analytics_settings.json");
}
}

// ============================================================================
// Plot

class AnalyticsPanel::PlotView final : public juce::Component
{
public:
    void setPlot(const analytics::Plot* p)
    {
        plot = p;
        visible.assign(p != nullptr ? p->traces.size() : 0, true);
        cursorA = cursorB = nan();
        hoverText.clear();
        resetView();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(panelColour);
        layout();
        if (plot == nullptr)
        {
            g.setColour(faint);
            g.setFont(14.0f);
            g.drawText("Run an analysis to see its plots here.", getLocalBounds(), juce::Justification::centred);
            return;
        }
        g.setColour(textColour);
        g.setFont(juce::Font(13.5f, juce::Font::bold));
        g.drawText(plot->title, getLocalBounds().removeFromTop(24).reduced(10, 0), juce::Justification::centredLeft, true);

        g.setColour(background);
        g.fillRect(plotArea);
        drawGrid(g);
        {
            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(plotArea);
            switch (plot->kind)
            {
                case analytics::Plot::Kind::Lines: drawLines(g); break;
                case analytics::Plot::Kind::Bars: drawBars(g); break;
                case analytics::Plot::Kind::Histogram: drawHistogram(g); break;
                case analytics::Plot::Kind::PoleZero: drawPoleZero(g); break;
            }
            drawCursors(g);
            if (dragging)
            {
                g.setColour(accent.withAlpha(0.15f));
                g.fillRect(dragRect);
                g.setColour(accent.withAlpha(0.7f));
                g.drawRect(dragRect, 1);
            }
        }
        g.setColour(border);
        g.drawRect(plotArea, 1);
        if (plot->kind == analytics::Plot::Kind::Bars)
            drawBarLabels(g);
        drawLegend(g);
        drawReadout(g);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (plot == nullptr) return;
        for (size_t i = 0; i < legendRows.size(); ++i)
            if (legendRows[i].contains(e.getPosition()))
            {
                if (e.mods.isShiftDown() || e.getNumberOfClicks() > 1)
                    for (size_t k = 0; k < visible.size(); ++k) visible[k] = k == i;
                else
                    visible[i] = !visible[i];
                repaint();
                return;
            }
        dragStart = e.getPosition();
        dragging = false;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (plot == nullptr || !plotArea.contains(dragStart)) return;
        if (e.getDistanceFromDragStart() > 5)
        {
            dragging = true;
            dragRect = juce::Rectangle<int>(dragStart, e.getPosition()).getIntersection(plotArea);
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (plot == nullptr || !plotArea.contains(dragStart)) return;
        if (dragging)
        {
            dragging = false;
            if (dragRect.getWidth() > 4 && dragRect.getHeight() > 4)
            {
                const auto nx0 = fromX((float)dragRect.getX()), nx1 = fromX((float)dragRect.getRight());
                const auto ny0 = fromY((float)dragRect.getBottom()), ny1 = fromY((float)dragRect.getY());
                x0 = std::min(nx0, nx1); x1 = std::max(nx0, nx1);
                y0 = std::min(ny0, ny1); y1 = std::max(ny0, ny1);
            }
            repaint();
            return;
        }
        if (plot->kind == analytics::Plot::Kind::PoleZero && (e.mods.isRightButtonDown() || e.mods.isPopupMenu()))
        {
            pzLog = !pzLog;
            resetView();
            repaint();
            return;
        }
        if (plot->kind != analytics::Plot::Kind::Lines) return;
        const auto x = fromX((float)e.x);
        if (e.mods.isRightButtonDown() || e.mods.isShiftDown()) cursorB = x;
        else cursorA = x;
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (plotArea.contains(e.getPosition()))
        {
            resetView();
            cursorA = cursorB = nan();
            repaint();
        }
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (plot == nullptr || !plotArea.contains(e.getPosition()) || symAxes()) return;
        const auto factor = std::pow(0.85, wheel.deltaY * 4.0);
        if (e.mods.isCtrlDown())
        {
            const auto centre = fromY((float)e.y);
            zoomAxis(y0, y1, centre, factor, logY());
        }
        else
        {
            const auto centre = fromX((float)e.x);
            zoomAxis(x0, x1, centre, factor, logX());
        }
        repaint();
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (plot == nullptr || plot->kind != analytics::Plot::Kind::PoleZero) return;
        hoverText.clear();
        double best = 14.0;
        auto check = [&](const std::complex<double>& s, const juce::String& kind) {
            const auto d = juce::Point<float>(toX(s.real()), toY(s.imag())).getDistanceFrom(e.position);
            if (d < best)
            {
                best = d;
                const auto magnitude = std::abs(s);
                hoverText = kind + ": " + juce::String(s.real(), 6) + (s.imag() >= 0 ? " + j" : " - j") + juce::String(std::abs(s.imag()), 6)
                          + " rad/s   |s|/2pi = " + eng(magnitude / (2.0 * juce::MathConstants<double>::pi), "Hz")
                          + (magnitude > 0.0 ? "   zeta = " + juce::String(-s.real() / magnitude, 4) : juce::String());
            }
        };
        for (const auto& p : plot->poles) check(p, "Pole");
        for (const auto& z : plot->zeros) check(z, "Zero");
        repaint();
    }

private:
    const analytics::Plot* plot = nullptr;
    std::vector<bool> visible;
    double x0 = 0, x1 = 1, y0 = 0, y1 = 1;
    double cursorA = nan(), cursorB = nan();
    juce::Point<int> dragStart;
    bool dragging = false;
    juce::Rectangle<int> dragRect, plotArea, legendArea;
    std::vector<juce::Rectangle<int>> legendRows;
    juce::String hoverText;

    bool pzLog = true;
    bool symAxes() const { return plot != nullptr && plot->kind == analytics::Plot::Kind::PoleZero && pzLog; }
    static double sym(double v) { return (v >= 0.0 ? 1.0 : -1.0) * std::log10(1.0 + std::abs(v)); }
    static double unsym(double u) { return (u >= 0.0 ? 1.0 : -1.0) * (std::pow(10.0, std::abs(u)) - 1.0); }
    bool logX() const { return plot != nullptr && plot->logX && plot->kind == analytics::Plot::Kind::Lines; }
    bool logY() const { return plot != nullptr && plot->logY && plot->kind == analytics::Plot::Kind::Lines; }

    void layout()
    {
        auto area = getLocalBounds();
        area.removeFromTop(26);
        const bool legend = plot != nullptr && (plot->kind == analytics::Plot::Kind::Lines) && !plot->traces.empty();
        auto legendColumn = legend ? area.removeFromRight(juce::jlimit(130, 260, getWidth() / 4)) : juce::Rectangle<int>();
        area.removeFromLeft(74);
        area.removeFromBottom(plot != nullptr && plot->kind == analytics::Plot::Kind::Bars ? 54 : 40);
        area.removeFromRight(legend ? 30 : 34); // room for the last x label
        area.removeFromTop(4);
        plotArea = area;
        legendArea = legend ? legendColumn.withY(plotArea.getY()).withHeight(plotArea.getHeight()).reduced(6, 0) : juce::Rectangle<int>();
    }

    static void zoomAxis(double& lo, double& hi, double centre, double factor, bool log)
    {
        if (log && lo > 0.0 && hi > 0.0 && centre > 0.0)
        {
            const auto a = std::log10(lo), b = std::log10(hi), c = std::log10(centre);
            lo = std::pow(10.0, c + (a - c) * factor);
            hi = std::pow(10.0, c + (b - c) * factor);
            return;
        }
        lo = centre + (lo - centre) * factor;
        hi = centre + (hi - centre) * factor;
    }

    void resetView()
    {
        x0 = 0; x1 = 1; y0 = 0; y1 = 1;
        if (plot == nullptr) return;
        if (plot->kind == analytics::Plot::Kind::PoleZero)
        {
            double re = 0.0, im = 0.0, right = 0.0;
            for (const auto* roots : { &plot->poles, &plot->zeros })
                for (const auto& r : *roots)
                {
                    re = std::max(re, std::abs(r.real()));
                    im = std::max(im, std::abs(r.imag()));
                    right = std::max(right, r.real());
                }
            if (pzLog)
            {
                // Signed-log axes: every decade from 1 rad/s up gets equal room.
                const auto ux = std::max({ sym(re), sym(std::max(right, 0.0)), 1.0 });
                const auto uy = std::max(sym(im), ux * 0.4);
                x0 = unsym(-std::max(sym(re), ux * 0.25) * 1.08);
                x1 = unsym(std::max(sym(std::max(right, 0.0)), ux * 0.25) * 1.08);
                y0 = unsym(-uy * 1.08);
                y1 = unsym(uy * 1.08);
                return;
            }
            const auto span = std::max({ re, im, 1.0 });
            x0 = -std::max(re, span * 0.2) * 1.25;
            x1 = std::max(right * 1.25, span * 0.25);
            const auto vertical = std::max(im, span * 0.3) * 1.25;
            y0 = -vertical;
            y1 = vertical;
            return;
        }
        double xmin = std::numeric_limits<double>::max(), xmax = -xmin, ymin = xmin, ymax = -xmin;
        for (size_t t = 0; t < plot->traces.size(); ++t)
        {
            if (t < visible.size() && !visible[t]) continue;
            const auto& tr = plot->traces[t];
            for (size_t k = 0; k < tr.x.size() && k < tr.y.size(); ++k)
            {
                if (logX() && tr.x[k] <= 0.0) continue;
                if (!std::isfinite(tr.x[k])) continue;
                xmin = std::min(xmin, tr.x[k]); xmax = std::max(xmax, tr.x[k]);
                if (logY() && tr.y[k] <= 0.0) continue;
                if (!std::isfinite(tr.y[k])) continue;
                ymin = std::min(ymin, tr.y[k]); ymax = std::max(ymax, tr.y[k]);
            }
        }
        if (xmin > xmax) return;
        if (plot->kind == analytics::Plot::Kind::Bars)
        {
            x0 = -0.6; x1 = (double)plot->categories.size() - 0.4;
            if (decibelBars())
            {
                ymin = std::max(ymin, ymax - 160.0) - 10.0; // a floor under the smallest bar
                ymax = std::max(ymax, 0.0) + 3.0;
            }
            else
            {
                ymin = std::min(ymin, 0.0);
                ymax = std::max(ymax, 0.0);
            }
        }
        else if (plot->kind == analytics::Plot::Kind::Histogram)
        {
            const auto halfBin = plot->traces.front().x.size() > 1 ? 0.5 * (plot->traces.front().x[1] - plot->traces.front().x[0]) : 0.5;
            x0 = xmin - halfBin; x1 = xmax + halfBin;
            ymin = 0.0;
        }
        else
        {
            x0 = xmin; x1 = xmax;
            if (x1 <= x0) { x0 -= 0.5; x1 += 0.5; }
        }
        if (ymin > ymax) { ymin = 0.0; ymax = 1.0; }
        if (logY())
        {
            if (ymax <= ymin) { ymin /= 2.0; ymax *= 2.0; }
            y0 = ymin / std::pow(ymax / ymin, 0.05);
            y1 = ymax * std::pow(ymax / ymin, 0.05);
            return;
        }
        if (ymax - ymin < 1e-12 * std::max(1.0, std::abs(ymax)))
        {
            const auto pad = std::max(std::abs(ymax) * 0.1, 1e-9);
            ymin -= pad; ymax += pad;
        }
        const auto pad = (ymax - ymin) * 0.07;
        y0 = plot->kind == analytics::Plot::Kind::Histogram ? 0.0 : ymin - pad;
        y1 = ymax + pad;
    }

    float toX(double x) const
    {
        if (symAxes()) return plotArea.getX() + (float)((sym(x) - sym(x0)) / (sym(x1) - sym(x0))) * plotArea.getWidth();
        if (logX()) return plotArea.getX() + (float)((std::log10(x) - std::log10(x0)) / (std::log10(x1) - std::log10(x0))) * plotArea.getWidth();
        return plotArea.getX() + (float)((x - x0) / (x1 - x0)) * plotArea.getWidth();
    }

    float toY(double y) const
    {
        if (symAxes()) return plotArea.getBottom() - (float)((sym(y) - sym(y0)) / (sym(y1) - sym(y0))) * plotArea.getHeight();
        if (logY()) return plotArea.getBottom() - (float)((std::log10(y) - std::log10(y0)) / (std::log10(y1) - std::log10(y0))) * plotArea.getHeight();
        return plotArea.getBottom() - (float)((y - y0) / (y1 - y0)) * plotArea.getHeight();
    }

    double fromX(float px) const
    {
        const auto f = (px - plotArea.getX()) / (double)std::max(1, plotArea.getWidth());
        if (symAxes()) return unsym(sym(x0) + f * (sym(x1) - sym(x0)));
        return logX() ? std::pow(10.0, std::log10(x0) + f * (std::log10(x1) - std::log10(x0))) : x0 + f * (x1 - x0);
    }

    double fromY(float py) const
    {
        const auto f = (plotArea.getBottom() - py) / (double)std::max(1, plotArea.getHeight());
        if (symAxes()) return unsym(sym(y0) + f * (sym(y1) - sym(y0)));
        return logY() ? std::pow(10.0, std::log10(y0) + f * (std::log10(y1) - std::log10(y0))) : y0 + f * (y1 - y0);
    }

    std::vector<std::pair<double, bool>> axisTicks(double lo, double hi, bool log, int target) const
    {
        std::vector<std::pair<double, bool>> ticks; // value, major
        if (symAxes())
        {
            ticks.push_back({ 0.0, true });
            const auto top = (int)std::ceil(std::max(sym(std::abs(lo)), sym(std::abs(hi))));
            const int every = std::max(1, top / std::max(1, target / 2));
            for (int d = 0; d <= top; d += every)
            {
                const auto v = std::pow(10.0, d);
                if (v <= hi) ticks.push_back({ v, true });
                if (-v >= lo) ticks.push_back({ -v, true });
            }
            return ticks;
        }
        if (log && lo > 0.0 && hi > 0.0)
        {
            const int first = (int)std::floor(std::log10(lo)), last = (int)std::ceil(std::log10(hi));
            const bool minors = last - first <= 8;
            for (int d = first; d <= last; ++d)
                for (int m = 1; m <= 9; ++m)
                {
                    const auto v = m * std::pow(10.0, d);
                    if (v < lo * 0.999 || v > hi * 1.001) continue;
                    if (m == 1) ticks.push_back({ v, true });
                    else if (minors) ticks.push_back({ v, false });
                }
            return ticks;
        }
        for (auto v : niceTicks(lo, hi, target)) ticks.push_back({ v, true });
        return ticks;
    }

    void drawGrid(juce::Graphics& g)
    {
        g.setFont(11.5f);
        const auto xUnit = plot->xUnit, yUnit = plot->kind == analytics::Plot::Kind::Histogram ? juce::String() : plot->yUnit;
        if (plot->kind != analytics::Plot::Kind::Bars)
            for (const auto& [v, major] : axisTicks(x0, x1, logX(), std::max(3, plotArea.getWidth() / 110)))
            {
                const auto px = toX(v);
                g.setColour(major ? gridColour : gridMinor);
                g.drawVerticalLine((int)px, (float)plotArea.getY(), (float)plotArea.getBottom());
                if (major)
                {
                    g.setColour(muted);
                    g.drawText(eng(v, xUnit, 3), juce::Rectangle<float>(px - 50.0f, (float)plotArea.getBottom() + 3.0f, 100.0f, 16.0f), juce::Justification::centredTop);
                }
            }
        for (const auto& [v, major] : axisTicks(y0, y1, logY(), std::max(3, plotArea.getHeight() / 50)))
        {
            const auto py = toY(v);
            g.setColour(major ? gridColour : gridMinor);
            g.drawHorizontalLine((int)py, (float)plotArea.getX(), (float)plotArea.getRight());
            if (major)
            {
                g.setColour(muted);
                g.drawText(eng(v, yUnit, 3), juce::Rectangle<float>(0.0f, py - 8.0f, (float)plotArea.getX() - 6.0f, 16.0f), juce::Justification::centredRight);
            }
        }
        if (plot->kind == analytics::Plot::Kind::PoleZero)
        {
            g.setColour(muted.withAlpha(0.6f));
            g.drawVerticalLine((int)toX(0.0), (float)plotArea.getY(), (float)plotArea.getBottom());
            g.drawHorizontalLine((int)toY(0.0), (float)plotArea.getX(), (float)plotArea.getRight());
        }
        g.setColour(faint);
        g.setFont(12.0f);
        const auto xTitle = plot->xLabel + (plot->xUnit.isNotEmpty() ? " (" + plot->xUnit + ")" : juce::String());
        g.drawText(xTitle, plotArea.getX(), getHeight() - 17, plotArea.getWidth(), 16, juce::Justification::centred);
    }

    void drawLines(juce::Graphics& g)
    {
        for (size_t t = 0; t < plot->traces.size(); ++t)
        {
            if (t < visible.size() && !visible[t]) continue;
            const auto& tr = plot->traces[t];
            juce::Path path;
            bool started = false;
            for (size_t k = 0; k < tr.x.size() && k < tr.y.size(); ++k)
            {
                if ((logX() && tr.x[k] <= 0.0) || (logY() && tr.y[k] <= 0.0) || !std::isfinite(tr.y[k]))
                {
                    started = false;
                    continue;
                }
                const auto px = juce::jlimit(-1e5f, 1e5f, toX(tr.x[k])), py = juce::jlimit(-1e5f, 1e5f, toY(tr.y[k]));
                if (!started) { path.startNewSubPath(px, py); started = true; }
                else path.lineTo(px, py);
            }
            g.setColour(traceColour((int)t));
            g.strokePath(path, juce::PathStrokeType(1.6f));
        }
    }

    bool decibelBars() const { return plot != nullptr && plot->yUnit.containsIgnoreCase("dB"); }

    void drawBarLabels(juce::Graphics& g)
    {
        const auto slot = plotArea.getWidth() / (float)std::max(1, plot->categories.size());
        g.setColour(muted);
        g.setFont(11.0f);
        const int every = std::max(1, (int)std::ceil(60.0f / std::max(1.0f, slot)));
        for (int k = 0; k < plot->categories.size(); k += every)
            g.drawFittedText(plot->categories[k], juce::Rectangle<int>((int)(toX((double)k) - slot * 0.5f * every), plotArea.getBottom() + 3, (int)(slot * every), 30),
                             juce::Justification::centredTop, 2, 0.8f);
    }

    void drawBars(juce::Graphics& g)
    {
        // Spectra in dB rise from the bottom of the plot; signed quantities from zero.
        const auto zero = decibelBars() ? (float)plotArea.getBottom() : toY(0.0);
        const auto slot = plotArea.getWidth() / (float)std::max(1, plot->categories.size());
        for (size_t t = 0; t < plot->traces.size(); ++t)
        {
            const auto& tr = plot->traces[t];
            for (size_t k = 0; k < tr.y.size(); ++k)
            {
                const auto cx = toX(tr.x[k]);
                const auto top = toY(tr.y[k]);
                const auto r = juce::Rectangle<float>(cx - slot * 0.32f, std::min(top, zero), slot * 0.64f, std::abs(zero - top));
                g.setColour(traceColour((int)t).withAlpha(tr.y[k] >= 0 ? 0.85f : 0.6f));
                g.fillRect(r);
            }
        }
        g.setColour(muted.withAlpha(0.7f));
        g.drawHorizontalLine((int)toY(0.0), (float)plotArea.getX(), (float)plotArea.getRight());
    }

    void drawHistogram(juce::Graphics& g)
    {
        if (plot->traces.empty()) return;
        const auto& tr = plot->traces.front();
        const auto width = tr.x.size() > 1 ? tr.x[1] - tr.x[0] : 1.0;
        for (size_t k = 0; k < tr.x.size(); ++k)
        {
            const auto l = toX(tr.x[k] - width / 2), r = toX(tr.x[k] + width / 2);
            const auto top = toY(tr.y[k]);
            g.setColour(traceColour(0).withAlpha(0.75f));
            g.fillRect(juce::Rectangle<float>(l + 1.0f, top, std::max(1.0f, r - l - 2.0f), (float)plotArea.getBottom() - top));
        }
    }

    void drawPoleZero(juce::Graphics& g)
    {
        g.setColour(juce::Colour(0xffe56b8f));
        for (const auto& p : plot->poles)
        {
            const auto px = toX(p.real()), py = toY(p.imag());
            g.drawLine(px - 6, py - 6, px + 6, py + 6, 2.2f);
            g.drawLine(px - 6, py + 6, px + 6, py - 6, 2.2f);
        }
        g.setColour(juce::Colour(0xff5ec8e5));
        for (const auto& z : plot->zeros)
            g.drawEllipse(toX(z.real()) - 6.0f, toY(z.imag()) - 6.0f, 12.0f, 12.0f, 2.0f);
        if (plot->poles.empty() && plot->zeros.empty())
        {
            g.setColour(faint);
            g.drawText("No finite poles or zeros.", plotArea, juce::Justification::centred);
        }
        g.setColour(faint);
        g.setFont(11.0f);
        g.drawText(pzLog ? "signed-log axes (right-click: linear)  -  hover a root for its value" : "linear axes (right-click: signed-log)  -  drag to zoom",
                   plotArea.reduced(8, 4), juce::Justification::bottomRight);
    }

    void drawCursors(juce::Graphics& g)
    {
        if (std::isfinite(cursorA))
        {
            g.setColour(accent);
            g.drawVerticalLine((int)toX(cursorA), (float)plotArea.getY(), (float)plotArea.getBottom());
        }
        if (std::isfinite(cursorB))
        {
            g.setColour(warning);
            const float dash[] = { 5.0f, 4.0f };
            g.drawDashedLine(juce::Line<float>(toX(cursorB), (float)plotArea.getY(), toX(cursorB), (float)plotArea.getBottom()), dash, 2, 1.2f);
        }
    }

    void drawLegend(juce::Graphics& g)
    {
        legendRows.clear();
        if (legendArea.isEmpty()) return;
        g.setFont(12.0f);
        auto area = legendArea;
        for (size_t t = 0; t < plot->traces.size(); ++t)
        {
            if (area.getHeight() < 18) break;
            auto row = area.removeFromTop(19);
            legendRows.push_back(row);
            const bool on = t >= visible.size() || visible[t];
            g.setColour(traceColour((int)t).withAlpha(on ? 1.0f : 0.25f));
            g.fillRect(row.getX(), row.getCentreY() - 2, 16, 4);
            g.setColour(on ? textColour : faint);
            g.drawText(plot->traces[t].name, row.withTrimmedLeft(22), juce::Justification::centredLeft, true);
        }
        if (area.getHeight() >= 60)
        {
            g.setColour(faint);
            g.setFont(11.0f);
            g.drawFittedText("Legend: click hides, shift-click solos.\nPlot: click = cursor A, right-click = B, drag = zoom, wheel = zoom X (Ctrl: Y), double-click = reset.",
                             area.removeFromBottom(std::min(area.getHeight(), 70)), juce::Justification::bottomLeft, 5, 0.85f);
        }
    }

    void drawReadout(juce::Graphics& g)
    {
        juce::StringArray lines;
        if (plot->kind == analytics::Plot::Kind::PoleZero)
        {
            if (hoverText.isNotEmpty()) lines.add(hoverText);
        }
        else if (std::isfinite(cursorA) || std::isfinite(cursorB))
        {
            const auto xu = plot->xUnit;
            juce::String head;
            if (std::isfinite(cursorA)) head << "A: " << eng(cursorA, xu);
            if (std::isfinite(cursorB)) head << "   B: " << eng(cursorB, xu);
            if (std::isfinite(cursorA) && std::isfinite(cursorB))
            {
                const auto dx = cursorB - cursorA;
                head << "   dX: " << eng(dx, xu);
                if (xu == "s" && dx != 0.0) head << "  (1/dX " << eng(1.0 / std::abs(dx), "Hz") << ")";
            }
            lines.add(head);
            for (size_t t = 0; t < plot->traces.size() && lines.size() < 14; ++t)
            {
                if (t < visible.size() && !visible[t]) continue;
                const auto& tr = plot->traces[t];
                juce::String line = tr.name + ":";
                const auto a = std::isfinite(cursorA) ? interpolateAt(tr, cursorA) : nan();
                const auto b = std::isfinite(cursorB) ? interpolateAt(tr, cursorB) : nan();
                if (std::isfinite(a)) line << "  A " << eng(a, tr.unit);
                if (std::isfinite(b)) line << "  B " << eng(b, tr.unit);
                if (std::isfinite(a) && std::isfinite(b)) line << "  d " << eng(b - a, tr.unit);
                lines.add(line);
            }
        }
        if (lines.isEmpty()) return;
        g.setFont(juce::Font("Consolas", 12.0f, juce::Font::plain));
        int width = 0;
        for (const auto& l : lines) width = std::max(width, juce::roundToInt(g.getCurrentFont().getStringWidthFloat(l)) + 16);
        width = std::min(width, plotArea.getWidth() - 8);
        auto box = juce::Rectangle<int>(plotArea.getX() + 6, plotArea.getY() + 6, width, lines.size() * 16 + 8);
        g.setColour(juce::Colour(0xe0151a20));
        g.fillRoundedRectangle(box.toFloat(), 4.0f);
        g.setColour(border);
        g.drawRoundedRectangle(box.toFloat(), 4.0f, 1.0f);
        for (int i = 0; i < lines.size(); ++i)
        {
            g.setColour(i == 0 ? accent : textColour);
            g.drawText(lines[i], box.getX() + 8, box.getY() + 4 + i * 16, box.getWidth() - 12, 16, juce::Justification::centredLeft, true);
        }
    }
};

// ============================================================================
// Report: summary, warnings, tables

class AnalyticsPanel::ReportView final : public juce::Component
{
public:
    void setContent(const analytics::Result* r, const analytics::Table* m)
    {
        result = r;
        measurements = m;
        relayout(getWidth());
    }

    void relayout(int width)
    {
        width = std::max(300, width);
        int y = 8;
        summaryHeight = 0;
        if (result != nullptr)
        {
            summaryLayout = makeText(headline(), width - 20, 13.5f, result->ok ? textColour : warning);
            summaryHeight = (int)summaryLayout.getHeight() + 6;
            y += summaryHeight;
            warningLayout = makeText(result->warnings.joinIntoString("\n"), width - 20, 12.5f, warning);
            y += result->warnings.isEmpty() ? 0 : (int)warningLayout.getHeight() + 8;
        }
        tableAreas.clear();
        for (const auto* t : tables())
        {
            const auto rows = (int)t->rows.size();
            const auto h = 24 + 22 + rows * 20 + 14;
            tableAreas.push_back({ 10, y, width - 20, h });
            y += h;
        }
        setSize(width, std::max(y + 10, 40));
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(panelColour);
        if (result == nullptr)
        {
            g.setColour(faint);
            g.setFont(13.5f);
            g.drawText("Results tables appear here: operating points, device detail, noise contributions, poles and zeros, harmonics, statistics.",
                       getLocalBounds().reduced(12), juce::Justification::topLeft, true);
            return;
        }
        summaryLayout.draw(g, juce::Rectangle<float>(10.0f, 8.0f, (float)getWidth() - 20.0f, (float)summaryHeight));
        if (!result->warnings.isEmpty())
            warningLayout.draw(g, juce::Rectangle<float>(10.0f, 8.0f + summaryHeight, (float)getWidth() - 20.0f, warningLayout.getHeight()));
        const auto list = tables();
        for (size_t i = 0; i < list.size() && i < tableAreas.size(); ++i)
            drawTable(g, *list[i], tableAreas[i]);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!e.mods.isPopupMenu()) return;
        const auto list = tables();
        int hit = -1;
        for (size_t i = 0; i < tableAreas.size(); ++i)
            if (tableAreas[i].contains(e.getPosition())) hit = (int)i;
        juce::PopupMenu menu;
        menu.addItem(1, "Copy this table", hit >= 0);
        menu.addItem(2, "Copy all results");
        menu.showMenuAsync(juce::PopupMenu::Options(), [this, hit, list](int choice) {
            juce::String text;
            if (choice == 1 && hit >= 0) text = tsv(*list[(size_t)hit]);
            if (choice == 2)
            {
                text << headline() << "\n\n";
                for (const auto* t : list) text << tsv(*t) << "\n";
            }
            if (text.isNotEmpty()) juce::SystemClipboard::copyTextToClipboard(text);
        });
    }

private:
    const analytics::Result* result = nullptr;
    const analytics::Table* measurements = nullptr;
    juce::TextLayout summaryLayout, warningLayout;
    int summaryHeight = 0;
    std::vector<juce::Rectangle<int>> tableAreas;

    juce::String headline() const
    {
        if (result == nullptr) return {};
        return result->ok ? result->title + "  -  " + result->summary + "   (" + juce::String(result->seconds, 3) + " s)"
                          : result->title + " failed: " + result->error;
    }

    std::vector<const analytics::Table*> tables() const
    {
        std::vector<const analytics::Table*> list;
        if (measurements != nullptr && !measurements->rows.empty()) list.push_back(measurements);
        if (result != nullptr)
            for (const auto& t : result->tables) list.push_back(&t);
        return list;
    }

    static juce::TextLayout makeText(const juce::String& text, int width, float size, juce::Colour colour)
    {
        juce::AttributedString s;
        s.append(text, juce::Font(size), colour);
        s.setWordWrap(juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout(s, (float)std::max(50, width));
        return layout;
    }

    static juce::String tsv(const analytics::Table& t)
    {
        juce::String s = t.title + "\n" + t.columns.joinIntoString("\t") + "\n";
        for (const auto& r : t.rows) s << r.joinIntoString("\t") << "\n";
        return s;
    }

    void drawTable(juce::Graphics& g, const analytics::Table& t, juce::Rectangle<int> area)
    {
        g.setColour(accent);
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(t.title, area.removeFromTop(24), juce::Justification::centredLeft, true);
        const juce::Font cellFont("Consolas", 12.5f, juce::Font::plain);
        const auto columns = std::max(1, t.columns.size());
        std::vector<float> widths((size_t)columns, 40.0f);
        for (int c = 0; c < columns; ++c)
        {
            widths[(size_t)c] = std::max(widths[(size_t)c], juce::Font(12.5f, juce::Font::bold).getStringWidthFloat(t.columns[c]) + 18.0f);
            for (const auto& r : t.rows)
                if (c < r.size()) widths[(size_t)c] = std::max(widths[(size_t)c], cellFont.getStringWidthFloat(r[c]) + 18.0f);
        }
        float total = 0.0f;
        for (auto w : widths) total += w;
        if (total > area.getWidth())
        {
            // Shrink the widest columns first.
            const auto excess = total - area.getWidth();
            float wide = 0.0f;
            for (auto w : widths) if (w > 120.0f) wide += w - 120.0f;
            for (auto& w : widths) if (w > 120.0f && wide > 0.0f) w -= excess * (w - 120.0f) / wide;
        }
        auto header = area.removeFromTop(22);
        g.setColour(raised);
        g.fillRect(header);
        g.setFont(juce::Font(12.5f, juce::Font::bold));
        g.setColour(textColour);
        float x = (float)header.getX();
        for (int c = 0; c < columns; ++c)
        {
            g.drawText(t.columns[c], juce::Rectangle<float>(x + 6.0f, (float)header.getY(), widths[(size_t)c] - 10.0f, 22.0f), juce::Justification::centredLeft, true);
            x += widths[(size_t)c];
        }
        g.setFont(cellFont);
        for (size_t r = 0; r < t.rows.size(); ++r)
        {
            auto row = area.removeFromTop(20);
            if (r % 2 == 1) { g.setColour(juce::Colour(0xff18202a)); g.fillRect(row); }
            g.setColour(textColour);
            x = (float)row.getX();
            for (int c = 0; c < columns && c < t.rows[r].size(); ++c)
            {
                g.drawText(t.rows[r][c], juce::Rectangle<float>(x + 6.0f, (float)row.getY(), widths[(size_t)c] - 10.0f, 20.0f), juce::Justification::centredLeft, true);
                x += widths[(size_t)c];
            }
        }
    }
};

// ============================================================================
// Settings form, built from the analysis field table

class AnalyticsPanel::FormView final : public juce::Component
{
public:
    std::function<void()> onChange;

    void build(analytics::Analysis analysis, const analytics::Settings& values, const analytics::Netlist& netlist)
    {
        rows.clear();
        removeAllChildren();
        nets = analytics::netChoices(netlist);
        sources = analytics::sourceChoices(netlist);
        targets = analytics::targetChoices(netlist, true);
        currents.clear();
        for (const auto& p : netlist.parts) currents.add("I(" + p.refdes + ")");
        juce::String group;
        for (const auto& f : analytics::fieldsFor(analysis))
        {
            if (f.group != group)
            {
                group = f.group;
                Row heading;
                heading.heading = true;
                heading.label = std::make_unique<juce::Label>(juce::String(), group);
                styleLabel(*heading.label, 12.5f, accent, true);
                addAndMakeVisible(*heading.label);
                rows.push_back(std::move(heading));
            }
            const auto found = values.find(f.key);
            const auto value = found != values.end() ? found->second : f.defaultValue;
            rows.push_back(makeRow(f, value));
        }
        resized();
    }

    analytics::Settings values() const
    {
        analytics::Settings s;
        for (const auto& row : rows)
        {
            if (row.heading) continue;
            if (auto* e = dynamic_cast<juce::TextEditor*>(row.control.get())) s[row.field.key] = e->getText().trim();
            else if (auto* c = dynamic_cast<juce::ComboBox*>(row.control.get())) s[row.field.key] = c->getText().trim();
        }
        return s;
    }

    int preferredHeight() const
    {
        int h = 6;
        for (const auto& row : rows) h += height(row);
        return h + 10;
    }

    void resized() override
    {
        int y = 6;
        const int w = getWidth() - 16;
        for (auto& row : rows)
        {
            if (row.heading)
            {
                row.label->setBounds(8, y + 6, w, 18);
                y += height(row);
                continue;
            }
            row.label->setBounds(8, y, w, 16);
            auto controlArea = juce::Rectangle<int>(8, y + 17, w, 26);
            if (row.pick != nullptr) row.pick->setBounds(controlArea.removeFromRight(30));
            row.control->setBounds(controlArea.withTrimmedRight(row.pick != nullptr ? 4 : 0));
            if (row.hint != nullptr) row.hint->setBounds(8, y + 44, w, 28);
            y += height(row);
        }
    }

private:
    struct Row
    {
        analytics::Field field;
        bool heading = false;
        std::unique_ptr<juce::Label> label, hint;
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<juce::TextButton> pick;
    };
    std::vector<Row> rows;
    juce::StringArray nets, sources, targets, currents;

    static int height(const Row& row) { return row.heading ? 30 : row.hint != nullptr ? 76 : 50; }

    Row makeRow(const analytics::Field& f, const juce::String& value)
    {
        Row row;
        row.field = f;
        row.label = std::make_unique<juce::Label>(juce::String(), f.label + (f.unit.isNotEmpty() ? " (" + f.unit + ")" : juce::String()));
        styleLabel(*row.label, 12.0f, muted);
        addAndMakeVisible(*row.label);
        if (f.help.isNotEmpty())
        {
            row.hint = std::make_unique<juce::Label>(juce::String(), f.help);
            styleLabel(*row.hint, 11.0f, faint);
            row.hint->setJustificationType(juce::Justification::topLeft);
            addAndMakeVisible(*row.hint);
        }
        auto changed = [this] { if (onChange) onChange(); };
        juce::StringArray items;
        switch (f.kind)
        {
            case analytics::FieldKind::Choice: items = f.options; break;
            case analytics::FieldKind::Net: items = nets; break;
            case analytics::FieldKind::Source: items = sources; break;
            case analytics::FieldKind::Target:
                if (f.key.startsWith("step")) items.add("None");
                items.addArray(targets);
                break;
            default: break;
        }
        const bool combo = f.kind == analytics::FieldKind::Choice || f.kind == analytics::FieldKind::Net
                        || f.kind == analytics::FieldKind::Source || f.kind == analytics::FieldKind::Target;
        if (combo)
        {
            auto* box = new juce::ComboBox();
            styleCombo(*box);
            if (value.isNotEmpty() && !items.contains(value)) items.insert(0, value);
            for (int i = 0; i < items.size(); ++i) box->addItem(items[i], i + 1);
            if (value.isNotEmpty()) box->setText(value, juce::dontSendNotification);
            else if (!items.isEmpty()) box->setSelectedItemIndex(0, juce::dontSendNotification);
            box->onChange = changed;
            row.control.reset(box);
        }
        else
        {
            auto* editor = new juce::TextEditor();
            styleEditor(*editor);
            editor->setText(value, false);
            editor->setTextToShowWhenEmpty(f.kind == analytics::FieldKind::Nets ? "every net" : f.defaultValue, faint);
            editor->onTextChange = changed;
            row.control.reset(editor);
            if (f.kind == analytics::FieldKind::Nets)
            {
                row.pick = std::make_unique<juce::TextButton>("+");
                styleButton(*row.pick);
                row.pick->setTooltip("Add a net or a current");
                auto* target = editor;
                row.pick->onClick = [this, target] {
                    juce::PopupMenu menu, netMenu, currentMenu;
                    for (int i = 1; i < nets.size(); ++i) netMenu.addItem(1 + i, nets[i]);
                    for (int i = 0; i < currents.size(); ++i) currentMenu.addItem(1000 + i, currents[i]);
                    menu.addSubMenu("Node voltage", netMenu);
                    menu.addSubMenu("Element current", currentMenu);
                    menu.addItem(9999, "Clear (every net)");
                    juce::Component::SafePointer<juce::TextEditor> safe(target);
                    menu.showMenuAsync(juce::PopupMenu::Options(), [this, safe](int id) {
                        if (safe == nullptr || id == 0) return;
                        juce::String add = id >= 1000 && id < 9999 ? currents[id - 1000] : id < 1000 ? nets[id - 1] : juce::String();
                        if (id == 9999) safe->setText({}, true);
                        else safe->setText(safe->getText().trim().isEmpty() ? add : safe->getText().trim() + ", " + add, true);
                    });
                };
                addAndMakeVisible(*row.pick);
            }
        }
        addAndMakeVisible(*row.control);
        return row;
    }
};

// ============================================================================
// Analysis list

class AnalyticsPanel::AnalysisList final : public juce::Component
{
public:
    std::function<void(analytics::Analysis)> onSelect;

    AnalysisList()
    {
        for (const auto& a : analytics::analyses())
        {
            auto* b = buttons.add(new juce::TextButton(a.title));
            b->setClickingTogglesState(true);
            b->setRadioGroupId(4101);
            b->setTooltip(a.description);
            styleButton(*b);
            b->setColour(juce::TextButton::buttonColourId, panelColour);
            const auto id = a.id;
            b->onClick = [this, id] { if (onSelect) onSelect(id); };
            addAndMakeVisible(b);
        }
    }

    void select(analytics::Analysis id)
    {
        for (size_t i = 0; i < analytics::analyses().size(); ++i)
            buttons[(int)i]->setToggleState(analytics::analyses()[i].id == id, juce::dontSendNotification);
    }

    int preferredHeight() const { return buttons.size() * 25 + 4; }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4, 2);
        for (auto* b : buttons)
            b->setBounds(area.removeFromTop(25).reduced(0, 1));
    }

private:
    juce::OwnedArray<juce::TextButton> buttons;
};

// ============================================================================
// Panel

AnalyticsPanel::AnalyticsPanel()
{
    list = std::make_unique<AnalysisList>();
    list->onSelect = [this](analytics::Analysis a) { selectAnalysis(a); };
    addAndMakeVisible(*list);

    form = std::make_unique<FormView>();
    form->onChange = [this] { settings[current] = form->values(); saveSettings(); };
    formViewport.setViewedComponent(form.get(), false);
    formViewport.setScrollBarsShown(true, false);
    formViewport.setScrollBarThickness(9);
    addAndMakeVisible(formViewport);

    styleLabel(titleLabel, 17.0f, textColour, true);
    styleLabel(descriptionLabel, 12.5f, muted);
    styleLabel(statusLabel, 12.5f, faint);
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(descriptionLabel);
    addAndMakeVisible(statusLabel);

    styleButton(runButton, true);
    runButton.setTooltip("Run this analysis on the open diagram (parts are never changed)");
    runButton.onClick = [this] { runSelected(); };
    addAndMakeVisible(runButton);
    styleButton(filesButton);
    filesButton.onClick = [this] {
        if (shownRun >= 0 && !runs[(size_t)shownRun].files.isEmpty())
            juce::File(runs[(size_t)shownRun].files[0]).revealToUser();
    };
    addAndMakeVisible(filesButton);
    styleButton(refreshButton);
    refreshButton.setTooltip("Re-read the nets, sources and parts from the diagram");
    refreshButton.onClick = [this] { refreshChoices(); };
    addAndMakeVisible(refreshButton);
    styleCombo(historyBox);
    historyBox.setTextWhenNothingSelected("Run history");
    historyBox.onChange = [this] { if (historyBox.getSelectedId() > 0) showRun(historyBox.getSelectedId() - 1); };
    addAndMakeVisible(historyBox);

    plot = std::make_unique<PlotView>();
    addAndMakeVisible(*plot);

    styleCombo(measureTrace);
    measureTrace.setTextWhenNothingSelected("Trace");
    styleCombo(measureKind);
    int id = 1;
    for (auto k : signal_measure::allKinds()) measureKind.addItem(signal_measure::kindName(k), id++);
    measureKind.setSelectedId(1, juce::dontSendNotification);
    measureKind.onChange = [this] { updateMeasureControls(); };
    for (auto* e : { &measureFrom, &measureTo, &measureValue }) { styleEditor(*e); addAndMakeVisible(*e); }
    measureFrom.setTextToShowWhenEmpty("from", faint);
    measureTo.setTextToShowWhenEmpty("to", faint);
    measureFrom.setTooltip("Window start on the x axis (empty: start of the trace)");
    measureTo.setTooltip("Window end on the x axis (empty: end of the trace)");
    styleLabel(measureValueLabel, 12.0f, muted);
    measureValueLabel.setJustificationType(juce::Justification::centredRight);
    styleButton(measureButton);
    measureButton.onClick = [this] { addMeasurement(); };
    addAndMakeVisible(measureTrace);
    addAndMakeVisible(measureKind);
    addAndMakeVisible(measureValueLabel);
    addAndMakeVisible(measureButton);
    updateMeasureControls();

    report = std::make_unique<ReportView>();
    reportViewport.setViewedComponent(report.get(), false);
    reportViewport.setScrollBarsShown(true, false);
    reportViewport.setScrollBarThickness(9);
    addAndMakeVisible(reportViewport);

    loadSettings();
    selectAnalysis(analytics::Analysis::OperatingPoint);
    statusLabel.setText("Choose an analysis, check its settings, press Run.", juce::dontSendNotification);
}

AnalyticsPanel::~AnalyticsPanel()
{
    alive.reset();
    pool.removeAllJobs(true, 60000);
}

void AnalyticsPanel::paint(juce::Graphics& g)
{
    g.fillAll(background);
    g.setColour(panelColour);
    g.fillRect(getLocalBounds().removeFromLeft(leftWidth()));
    g.setColour(border);
    g.drawVerticalLine(leftWidth(), 0.0f, (float)getHeight());
    g.setColour(muted);
    g.setFont(juce::Font(12.5f, juce::Font::bold));
    g.drawText("ANALYSIS", 12, 6, 200, 18, juce::Justification::centredLeft);
    g.drawText("SETTINGS", 12, list->getBottom() + 4, 200, 18, juce::Justification::centredLeft);
    g.setColour(raised);
    g.fillRect(dividerArea);
    g.setColour(border);
    g.fillRect(dividerArea.withSizeKeepingCentre(40, 2));
}

void AnalyticsPanel::resized()
{
    auto area = getLocalBounds();
    auto left = area.removeFromLeft(leftWidth());
    left.removeFromTop(24);
    list->setBounds(left.removeFromTop(list->preferredHeight()));
    left.removeFromTop(24);
    refreshButton.setBounds(left.removeFromBottom(34).reduced(8, 4));
    formViewport.setBounds(left.reduced(2, 0));
    form->setSize(formViewport.getWidth() - formViewport.getScrollBarThickness() - 2, form->preferredHeight());

    area.removeFromLeft(1);
    auto heading = area.removeFromTop(48).reduced(12, 4);
    titleLabel.setBounds(heading.removeFromTop(22));
    descriptionLabel.setBounds(heading);
    auto controls = area.removeFromTop(38).reduced(12, 3);
    runButton.setBounds(controls.removeFromLeft(100));
    controls.removeFromLeft(8);
    filesButton.setBounds(controls.removeFromRight(juce::jmin(134, controls.getWidth() / 3)));
    controls.removeFromRight(8);
    historyBox.setBounds(controls);
    statusLabel.setBounds(area.removeFromTop(20).reduced(12, 0));

    auto plotRow = area.removeFromTop(30).reduced(10, 3);
    for (auto* b : plotButtons)
        b->setBounds(plotRow.removeFromLeft(juce::jlimit(90, 260, juce::roundToInt(juce::Font(12.5f).getStringWidthFloat(b->getButtonText())) + 24)).reduced(2, 0));

    const auto available = area.getHeight();
    const auto plotHeight = juce::jlimit(120, std::max(120, available - 150), juce::roundToInt(available * splitRatio));
    plot->setBounds(area.removeFromTop(plotHeight).reduced(6, 2));
    dividerArea = area.removeFromTop(6);
    auto measure = area.removeFromTop(34).reduced(10, 4);
    const auto unit = (float)measure.getWidth() / 100.0f;
    auto take = [&](float share, int gap) {
        auto r = measure.removeFromLeft(juce::roundToInt(share * unit));
        measure.removeFromLeft(gap);
        return r;
    };
    measureTrace.setBounds(take(26.0f, 4));
    measureKind.setBounds(take(22.0f, 4));
    measureFrom.setBounds(take(10.0f, 3));
    measureTo.setBounds(take(10.0f, 3));
    measureValueLabel.setBounds(take(7.0f, 2));
    measureValue.setBounds(take(9.0f, 4));
    measureButton.setBounds(measure);
    reportViewport.setBounds(area.reduced(6, 2));
    report->relayout(reportViewport.getWidth() - reportViewport.getScrollBarThickness() - 2);
}

void AnalyticsPanel::visibilityChanged()
{
    if (isShowing())
        refreshChoices();
}

void AnalyticsPanel::mouseDown(const juce::MouseEvent& e)
{
    draggingDivider = dividerArea.expanded(0, 3).contains(e.getPosition());
}

void AnalyticsPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (!draggingDivider) return;
    const auto topOfPlot = plot->getY();
    const auto available = getHeight() - topOfPlot;
    splitRatio = juce::jlimit(0.2f, 0.85f, (float)(e.y - topOfPlot) / (float)std::max(1, available));
    resized();
    repaint();
}

void AnalyticsPanel::mouseUp(const juce::MouseEvent&)
{
    draggingDivider = false;
}

void AnalyticsPanel::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(dividerArea.expanded(0, 3).contains(e.getPosition()) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void AnalyticsPanel::selectAnalysis(analytics::Analysis analysis)
{
    if (form != nullptr && settings.count(current) != 0)
        settings[current] = form->values();
    current = analysis;
    list->select(analysis);
    const auto& info = analytics::infoFor(analysis);
    titleLabel.setText(info.title, juce::dontSendNotification);
    descriptionLabel.setText(info.description, juce::dontSendNotification);
    runButton.setButtonText("Run");
    refreshChoices();
}

void AnalyticsPanel::setSettings(analytics::Analysis analysis, const analytics::Settings& values)
{
    auto& s = settings[analysis];
    for (const auto& [k, v] : values) s[k] = v;
    if (analysis == current)
        form->build(current, settings[current], choicesNetlist);
    saveSettings();
}

analytics::Settings AnalyticsPanel::settingsFor(analytics::Analysis analysis) const
{
    const auto found = settings.find(analysis);
    return found != settings.end() ? found->second : analytics::Settings {};
}

void AnalyticsPanel::refreshChoices()
{
    if (getNetlist != nullptr)
        choicesNetlist = getNetlist();
    form->build(current, settings[current], choicesNetlist);
    form->setSize(std::max(200, formViewport.getWidth() - formViewport.getScrollBarThickness() - 2), form->preferredHeight());
}

void AnalyticsPanel::runSelected()
{
    if (running.load())
        return;
    settings[current] = form->values();
    saveSettings();
    auto netlist = getNetlist != nullptr ? getNetlist() : analytics::Netlist {};
    choicesNetlist = netlist;
    running = true;
    runButton.setEnabled(false);
    runButton.setButtonText("Running...");
    statusLabel.setText("Running " + analytics::infoFor(current).title + " on the open diagram...", juce::dontSendNotification);
    const auto analysis = current;
    const auto values = settings[current];
    std::weak_ptr<bool> weak = alive;
    pool.addJob([this, analysis, values, netlist, weak] {
        auto result = analytics::run(analysis, values, netlist);
        juce::MessageManager::callAsync([this, weak, result]() mutable {
            if (weak.expired()) return;
            deliver(std::move(result));
        });
    });
}

void AnalyticsPanel::deliver(analytics::Result result)
{
    running = false;
    runButton.setEnabled(true);
    runButton.setButtonText("Run");
    storeRun(std::move(result));
    showRun((int)runs.size() - 1);
}

const AnalyticsPanel::Run& AnalyticsPanel::runNow(analytics::Analysis analysis, const analytics::Settings& values)
{
    // Exactly what was asked for, on top of the defaults (not what an earlier run left behind).
    const auto netlist = getNetlist != nullptr ? getNetlist() : analytics::Netlist {};
    auto result = analytics::run(analysis, values, netlist);
    settings[analysis] = result.settings;
    saveSettings();
    if (analysis != current)
        selectAnalysis(analysis);
    else
    {
        choicesNetlist = netlist;
        form->build(current, settings[current], choicesNetlist);
    }
    storeRun(std::move(result));
    showRun((int)runs.size() - 1);
    if (bringToFront != nullptr) bringToFront();
    return runs.back();
}

void AnalyticsPanel::storeRun(analytics::Result result)
{
    Run run;
    run.files = writeFiles(result);
    run.result = std::move(result);
    run.measurements.title = "Measurements";
    run.measurements.columns = { "Trace", "Measurement", "Window", "Value" };
    runs.push_back(std::move(run));
    const auto& r = runs.back().result;
    historyBox.addItem("#" + juce::String((int)runs.size()) + "  " + r.when.toString(false, true, true, true) + "  " + r.title
                           + (r.ok ? juce::String() : "  (failed)"),
                       (int)runs.size());
}

void AnalyticsPanel::showRun(int index)
{
    if (index < 0 || index >= (int)runs.size())
        return;
    shownRun = index;
    historyBox.setSelectedId(index + 1, juce::dontSendNotification);
    const auto& run = runs[(size_t)index];
    plotButtons.clear();
    for (size_t i = 0; i < run.result.plots.size(); ++i)
    {
        auto* b = plotButtons.add(new juce::TextButton(run.result.plots[i].title));
        b->setClickingTogglesState(true);
        b->setRadioGroupId(4102);
        styleButton(*b);
        const auto plotIndex = (int)i;
        b->onClick = [this, plotIndex] { showPlot(plotIndex); };
        addAndMakeVisible(b);
    }
    showPlot(0);
    report->setContent(&run.result, &run.measurements);
    measureTrace.clear(juce::dontSendNotification);
    const auto names = analytics::traceNames(run.result);
    for (int i = 0; i < names.size(); ++i) measureTrace.addItem(names[i], i + 1);
    if (!names.isEmpty()) measureTrace.setSelectedId(1, juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, run.result.ok ? faint : warning);
    statusLabel.setText(run.result.ok ? "Run #" + juce::String(index + 1) + " done in " + juce::String(run.result.seconds, 3) + " s"
                                          + (run.files.isEmpty() ? juce::String() : ";  CSV in " + juce::File(run.files[0]).getParentDirectory().getFullPathName())
                                      : "Run #" + juce::String(index + 1) + " failed: " + run.result.error,
                        juce::dontSendNotification);
    resized();
    repaint();
}

void AnalyticsPanel::showPlot(int index)
{
    shownPlot = index;
    if (shownRun < 0 || shownRun >= (int)runs.size())
        return;
    const auto& plots = runs[(size_t)shownRun].result.plots;
    for (int i = 0; i < plotButtons.size(); ++i)
        plotButtons[i]->setToggleState(i == index, juce::dontSendNotification);
    plot->setPlot(index >= 0 && index < (int)plots.size() ? &plots[(size_t)index] : nullptr);
}

void AnalyticsPanel::updateMeasureControls()
{
    using K = signal_measure::Kind;
    const auto kinds = signal_measure::allKinds();
    const auto index = measureKind.getSelectedItemIndex();
    const auto kind = index >= 0 && index < (int)kinds.size() ? kinds[(size_t)index] : K::Maximum;
    juce::String label;
    if (kind == K::ValueAt) label = "at";
    if (kind == K::WhenCrosses) label = "level";
    if (kind == K::SettlingTime) label = "band %";
    measureValueLabel.setText(label, juce::dontSendNotification);
    measureValue.setVisible(label.isNotEmpty());
    measureValueLabel.setVisible(label.isNotEmpty());
    measureValue.setTextToShowWhenEmpty(kind == K::SettlingTime ? "2" : juce::String(), faint);
}

namespace
{
juce::String measurementUnit(signal_measure::Kind kind, const analytics::Trace& trace, const juce::String& xUnit)
{
    using K = signal_measure::Kind;
    switch (kind)
    {
        case K::Minimum: case K::Maximum: case K::PeakToPeak: case K::Average: case K::Rms: case K::ValueAt: return trace.unit;
        case K::Integral: return trace.unit + "*" + xUnit;
        case K::Frequency: return "Hz";
        case K::OvershootPercent: return "%";
        case K::PhaseMargin: return juce::String(juce::CharPointer_UTF8("\xc2\xb0"));
        case K::GainMargin: return "dB";
        default: return xUnit;
    }
}
}

signal_measure::Result AnalyticsPanel::measureLatest(const juce::String& trace, const signal_measure::Request& request, juce::String& label)
{
    signal_measure::Result r;
    if (runs.empty())
    {
        r.error = "There is no Analytics result yet; run an analysis first.";
        return r;
    }
    auto& run = runs.back();
    r = analytics::measure(run.result, trace, request);
    int plotIndex = -1;
    const auto* t = analytics::findTrace(run.result, trace, &plotIndex);
    const auto xUnit = plotIndex >= 0 ? run.result.plots[(size_t)plotIndex].xUnit : juce::String();
    const auto unit = t != nullptr ? measurementUnit(request.kind, *t, xUnit) : juce::String();
    const auto window = request.from > -1e299 || request.to < 1e299
        ? eng(request.from > -1e299 ? request.from : 0.0, xUnit) + " .. " + (request.to < 1e299 ? eng(request.to, xUnit) : juce::String("end"))
        : juce::String("whole trace");
    const auto value = r.ok ? eng(r.value, unit, 6) : "-- " + juce::String(r.error);
    label = juce::String(signal_measure::kindName(request.kind)) + " of " + (t != nullptr ? t->name : trace) + " = " + value;
    run.measurements.rows.push_back({ t != nullptr ? t->name : trace, signal_measure::kindName(request.kind), window, value });
    if (shownRun != (int)runs.size() - 1)
        showRun((int)runs.size() - 1);
    report->relayout(report->getWidth());
    return r;
}

void AnalyticsPanel::addMeasurement()
{
    if (shownRun < 0 || measureTrace.getText().isEmpty())
        return;
    if (shownRun != (int)runs.size() - 1)
        showRun((int)runs.size() - 1);
    const auto kinds = signal_measure::allKinds();
    signal_measure::Request request;
    request.kind = kinds[(size_t)juce::jlimit(0, (int)kinds.size() - 1, measureKind.getSelectedItemIndex())];
    double v = 0.0;
    if (circuit_sim::parseValue(measureFrom.getText().trim().toStdString(), v)) request.from = v;
    if (circuit_sim::parseValue(measureTo.getText().trim().toStdString(), v)) request.to = v;
    if (circuit_sim::parseValue(measureValue.getText().trim().toStdString(), v))
    {
        request.at = v;
        request.level = v;
        request.bandPercent = v;
    }
    juce::String label;
    measureLatest(measureTrace.getText(), request, label);
}

juce::StringArray AnalyticsPanel::writeFiles(const analytics::Result& result) const
{
    juce::StringArray files;
    if (outputFolder == nullptr || !result.ok)
        return files;
    const auto folder = outputFolder().getChildFile("analytics")
        .getChildFile(result.when.formatted("%Y-%m-%d_%H-%M-%S") + "_" + analytics::infoFor(result.analysis).key);
    if (!folder.createDirectory())
        return files;
    auto stem = [](int index, const juce::String& title) {
        return juce::String(index).paddedLeft('0', 2) + "_" + juce::File::createLegalFileName(title).replaceCharacter(' ', '_').substring(0, 60);
    };
    for (size_t i = 0; i < result.plots.size(); ++i)
    {
        const auto file = folder.getChildFile("plot_" + stem((int)i + 1, result.plots[i].title) + ".csv");
        if (file.replaceWithText(analytics::toCsv(result.plots[i]))) files.add(file.getFullPathName());
    }
    for (size_t i = 0; i < result.tables.size(); ++i)
    {
        const auto file = folder.getChildFile("table_" + stem((int)i + 1, result.tables[i].title) + ".csv");
        if (file.replaceWithText(analytics::toCsv(result.tables[i]))) files.add(file.getFullPathName());
    }
    const auto json = folder.getChildFile("result.json");
    if (json.replaceWithText(analytics::toJson(result, files))) files.add(json.getFullPathName());
    return files;
}

void AnalyticsPanel::saveSettings() const
{
    auto* root = new juce::DynamicObject();
    for (const auto& [analysis, values] : settings)
    {
        auto* o = new juce::DynamicObject();
        for (const auto& [k, v] : values) o->setProperty(juce::Identifier(k), v);
        root->setProperty(juce::Identifier(analytics::infoFor(analysis).key), juce::var(o));
    }
    settingsFile().getParentDirectory().createDirectory();
    settingsFile().replaceWithText(juce::JSON::toString(juce::var(root), true));
}

void AnalyticsPanel::loadSettings()
{
    const auto parsed = juce::JSON::parse(settingsFile());
    const auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return;
    for (const auto& a : analytics::analyses())
        if (const auto* o = root->getProperty(juce::Identifier(a.key)).getDynamicObject())
            for (const auto& p : o->getProperties())
                settings[a.id][p.name.toString()] = p.value.toString();
}
