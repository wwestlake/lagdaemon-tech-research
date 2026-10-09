#include "SignalMeasure.h"

#include <algorithm>
#include <cmath>

namespace signal_measure
{
namespace
{
struct Window
{
    std::vector<double> x, y, p;
};

double interpolate(double x0, double y0, double x1, double y1, double x)
{
    return x1 != x0 ? y0 + (y1 - y0) * (x - x0) / (x1 - x0) : y0;
}

Window window(const Request& r, const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& phase)
{
    Window w;
    if (x.size() < 2 || y.size() < 2 || r.from > r.to)
        return w;
    auto addAt = [&](double xv) {
        if (xv < x.front() || xv > x.back())
            return;
        for (size_t i = 1; i < x.size() && i < y.size(); ++i)
            if (xv <= x[i])
            {
                w.x.push_back(xv);
                w.y.push_back(interpolate(x[i - 1], y[i - 1], x[i], y[i], xv));
                if (phase.size() == x.size())
                    w.p.push_back(interpolate(x[i - 1], phase[i - 1], x[i], phase[i], xv));
                return;
            }
    };
    const auto from = std::max(r.from, x.front());
    const auto to = std::min(r.to, x.back());
    if (from > to)
        return w;
    addAt(from);
    for (size_t i = 0; i < x.size() && i < y.size(); ++i)
        if (x[i] > from && x[i] < to)
        {
            w.x.push_back(x[i]);
            w.y.push_back(y[i]);
            if (i < phase.size()) w.p.push_back(phase[i]);
        }
    if (to > from)
        addAt(to);
    return w;
}

// x of the nth crossing of `level`; false if there are fewer.
bool crossing(const std::vector<double>& x, const std::vector<double>& y, double level, int nth, Edge edge, double& out)
{
    int count = 0;
    for (size_t i = 1; i < y.size(); ++i)
    {
        const auto a = y[i - 1] - level, b = y[i] - level;
        const bool rising = a < 0.0 && b >= 0.0, falling = a > 0.0 && b <= 0.0;
        if ((edge == Edge::Rising && rising) || (edge == Edge::Falling && falling) || (edge == Edge::Either && (rising || falling)))
            if (++count == nth)
            {
                out = interpolate(y[i - 1], x[i - 1], y[i], x[i], level);
                return true;
            }
    }
    return false;
}

double trapezoid(const std::vector<double>& x, const std::vector<double>& y)
{
    double sum = 0.0;
    for (size_t i = 1; i < x.size(); ++i)
        sum += 0.5 * (y[i] + y[i - 1]) * (x[i] - x[i - 1]);
    return sum;
}

Result fail(const std::string& error) { Result r; r.error = error; return r; }
Result done(double value) { Result r; r.ok = true; r.value = value; return r; }

// Step response initial and final values (first and last sample).
Result edgeTime(const Window& w, double lowPercent, double highPercent, bool rising)
{
    const auto initial = w.y.front(), final = w.y.back();
    const auto step = final - initial;
    if (std::abs(step) < 1e-15 || (rising ? step < 0.0 : step > 0.0))
        return fail(rising ? "The trace does not rise from its first to its last sample." : "The trace does not fall from its first to its last sample.");
    double t1 = 0.0, t2 = 0.0;
    const auto e = rising ? Edge::Rising : Edge::Falling;
    if (!crossing(w.x, w.y, initial + step * lowPercent / 100.0, 1, e, t1) || !crossing(w.x, w.y, initial + step * highPercent / 100.0, 1, e, t2))
        return fail("The trace does not cross both threshold levels.");
    return done(t2 - t1);
}
}

Result measure(const Request& r, const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& phase)
{
    const auto w = window(r, x, y, phase);
    if (w.x.size() < 2)
        return fail("Fewer than two samples in the measurement window.");
    const auto [minIt, maxIt] = std::minmax_element(w.y.begin(), w.y.end());
    const auto span = w.x.back() - w.x.front();

    switch (r.kind)
    {
        case Kind::Minimum: return done(*minIt);
        case Kind::Maximum: return done(*maxIt);
        case Kind::PeakToPeak: return done(*maxIt - *minIt);
        case Kind::PeakX: return done(w.x[(size_t)(maxIt - w.y.begin())]);
        case Kind::Average: return span > 0.0 ? done(trapezoid(w.x, w.y) / span) : fail("The window has zero width.");
        case Kind::Rms:
        {
            std::vector<double> sq(w.y.size());
            for (size_t i = 0; i < w.y.size(); ++i) sq[i] = w.y[i] * w.y[i];
            return span > 0.0 ? done(std::sqrt(trapezoid(w.x, sq) / span)) : fail("The window has zero width.");
        }
        case Kind::Integral: return done(trapezoid(w.x, w.y));
        case Kind::ValueAt:
        {
            if (r.at < w.x.front() || r.at > w.x.back())
                return fail("The point is outside the trace.");
            for (size_t i = 1; i < w.x.size(); ++i)
                if (r.at <= w.x[i])
                    return done(interpolate(w.x[i - 1], w.y[i - 1], w.x[i], w.y[i], r.at));
            return done(w.y.back());
        }
        case Kind::WhenCrosses:
        {
            double out = 0.0;
            return crossing(w.x, w.y, r.level, std::max(1, r.nth), r.edge, out) ? done(out) : fail("The trace does not cross that level that many times.");
        }
        case Kind::RiseTime: return edgeTime(w, r.lowPercent, r.highPercent, true);
        case Kind::FallTime: return edgeTime(w, r.lowPercent, r.highPercent, false);
        case Kind::OvershootPercent:
        {
            const auto initial = w.y.front(), final = w.y.back(), step = final - initial;
            if (std::abs(step) < 1e-15)
                return fail("The trace has no step from its first to its last sample.");
            const auto beyond = step > 0.0 ? *maxIt - final : final - *minIt;
            return done(std::max(0.0, beyond) / std::abs(step) * 100.0);
        }
        case Kind::SettlingTime:
        {
            const auto initial = w.y.front(), final = w.y.back(), step = std::abs(final - initial);
            if (step < 1e-15)
                return fail("The trace has no step from its first to its last sample.");
            const auto band = step * r.bandPercent / 100.0;
            for (size_t i = w.y.size(); i-- > 0;)
                if (std::abs(w.y[i] - final) > band)
                    return done((i + 1 < w.x.size() ? w.x[i + 1] : w.x[i]) - w.x.front());
            return done(0.0);
        }
        case Kind::Frequency:
        case Kind::Period:
        {
            const auto mid = 0.5 * (*maxIt + *minIt);
            std::vector<double> crossings;
            double t = 0.0;
            for (int n = 1; crossing(w.x, w.y, mid, n, Edge::Rising, t); ++n)
                crossings.push_back(t);
            if (crossings.size() < 2 || *maxIt - *minIt < 1e-15)
                return fail("Fewer than two full cycles in the window.");
            const auto period = (crossings.back() - crossings.front()) / (double)(crossings.size() - 1);
            return done(r.kind == Kind::Period ? period : 1.0 / period);
        }
        case Kind::Bandwidth3dB:
        case Kind::LowerCorner3dB:
        case Kind::UpperCorner3dB:
        {
            const auto peak = *maxIt;
            const auto peakIndex = (size_t)(maxIt - w.y.begin());
            const auto level = peak - 3.0103;
            double low = w.x.front(), high = w.x.back();
            bool lowFound = false, highFound = false;
            for (size_t i = peakIndex; i > 0; --i)
                if (w.y[i - 1] < level) { low = std::exp(interpolate(w.y[i - 1], std::log(w.x[i - 1]), w.y[i], std::log(w.x[i]), level)); lowFound = true; break; }
            for (size_t i = peakIndex + 1; i < w.y.size(); ++i)
                if (w.y[i] < level) { high = std::exp(interpolate(w.y[i - 1], std::log(w.x[i - 1]), w.y[i], std::log(w.x[i]), level)); highFound = true; break; }
            if (r.kind == Kind::LowerCorner3dB)
                return lowFound ? done(low) : fail("No lower -3 dB point inside the sweep (the response is flat down to the start).");
            if (r.kind == Kind::UpperCorner3dB)
                return highFound ? done(high) : fail("No upper -3 dB point inside the sweep (the response is flat up to the stop).");
            if (!lowFound && !highFound)
                return fail("The response stays within 3 dB of its peak over the whole sweep.");
            return done(high - (lowFound ? low : 0.0));
        }
        case Kind::UnityGainFrequency:
        case Kind::PhaseMargin:
        {
            for (size_t i = 1; i < w.y.size(); ++i)
                if (w.y[i - 1] >= 0.0 && w.y[i] < 0.0)
                {
                    const auto f = std::exp(interpolate(w.y[i - 1], std::log(w.x[i - 1]), w.y[i], std::log(w.x[i]), 0.0));
                    if (r.kind == Kind::UnityGainFrequency)
                        return done(f);
                    if (w.p.size() != w.y.size())
                        return fail("Phase margin needs the phase trace.");
                    const auto ph = interpolate(std::log(w.x[i - 1]), w.p[i - 1], std::log(w.x[i]), w.p[i], std::log(f));
                    auto margin = 180.0 + ph;
                    while (margin > 180.0) margin -= 360.0;
                    while (margin < -180.0) margin += 360.0;
                    return done(margin);
                }
            return fail("The gain does not cross 0 dB going down in the window.");
        }
        case Kind::GainMargin:
        {
            if (w.p.size() != w.y.size())
                return fail("Gain margin needs the phase trace.");
            for (size_t i = 1; i < w.p.size(); ++i)
            {
                const auto a = w.p[i - 1] + 180.0, b = w.p[i] + 180.0;
                if (a > 0.0 && b <= 0.0)
                {
                    const auto f = std::exp(interpolate(a, std::log(w.x[i - 1]), b, std::log(w.x[i]), 0.0));
                    const auto g = interpolate(std::log(w.x[i - 1]), w.y[i - 1], std::log(w.x[i]), w.y[i], std::log(f));
                    return done(-g);
                }
            }
            return fail("The phase does not cross -180 degrees in the window.");
        }
    }
    return fail("Unknown measurement.");
}

const char* kindName(Kind kind)
{
    switch (kind)
    {
        case Kind::Minimum: return "Minimum";
        case Kind::Maximum: return "Maximum";
        case Kind::PeakToPeak: return "Peak to peak";
        case Kind::Average: return "Average";
        case Kind::Rms: return "RMS";
        case Kind::Integral: return "Integral";
        case Kind::ValueAt: return "Value at";
        case Kind::WhenCrosses: return "When crosses";
        case Kind::RiseTime: return "Rise time";
        case Kind::FallTime: return "Fall time";
        case Kind::OvershootPercent: return "Overshoot %";
        case Kind::SettlingTime: return "Settling time";
        case Kind::Frequency: return "Frequency";
        case Kind::Period: return "Period";
        case Kind::PeakX: return "Position of peak";
        case Kind::Bandwidth3dB: return "-3 dB bandwidth";
        case Kind::LowerCorner3dB: return "Lower -3 dB corner";
        case Kind::UpperCorner3dB: return "Upper -3 dB corner";
        case Kind::UnityGainFrequency: return "Unity-gain frequency";
        case Kind::PhaseMargin: return "Phase margin";
        case Kind::GainMargin: return "Gain margin";
    }
    return "";
}

std::vector<Kind> allKinds()
{
    return { Kind::Minimum, Kind::Maximum, Kind::PeakToPeak, Kind::Average, Kind::Rms, Kind::Integral,
             Kind::ValueAt, Kind::WhenCrosses, Kind::RiseTime, Kind::FallTime, Kind::OvershootPercent,
             Kind::SettlingTime, Kind::Frequency, Kind::Period, Kind::PeakX, Kind::Bandwidth3dB,
             Kind::LowerCorner3dB, Kind::UpperCorner3dB, Kind::UnityGainFrequency, Kind::PhaseMargin, Kind::GainMargin };
}
}
