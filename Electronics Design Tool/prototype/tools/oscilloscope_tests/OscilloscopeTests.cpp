#include "Oscilloscope.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* name) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; } };
    auto near = [&](double actual, double expected, double tolerance, const char* name) { check(std::abs(actual - expected) <= tolerance, name); };
    constexpr double pi = 3.14159265358979323846;
    oscilloscope::Record r; r.ok = true; r.connected = { true, true };
    for (int i = 0; i <= 10000; ++i)
    {
        const double t = i * 1e-6;
        r.time.push_back(t); r.channel[0].push_back(250 + 0.2 * std::sin(2*pi*1000*t));
        r.channel[1].push_back(100 * std::sin(2*pi*1000*t));
    }
    auto m = oscilloscope::measure(r.time, r.channel[0], 0, .01);
    near(m.mean.value, 250, 1e-8, "250 V bias is measured");
    near(m.rms.value, std::sqrt(250*250 + .2*.2/2), 1e-8, "Total RMS includes DC component");
    near(m.peakToPeak.value, .4, 1e-8, "Small AC on high DC bias");
    near(m.frequency.value, 1000, .01, "Frequency from sampled threshold crossings");
    near(m.period.value, .001, 1e-8, "Period from sampled data");
    const double sineRise = (std::asin(.8) - std::asin(-.8)) / (2*pi*1000);
    near(m.riseTime.value, sineRise, 2e-6, "Sine rise time is observed 10-90 percent");
    near(m.fallTime.value, sineRise, 2e-6, "Sine fall time is observed 90-10 percent");
    near(m.pulseWidth.value, .0005, 2e-6, "Sine positive pulse width at midrange");
    near(m.dutyPercent.value, 50, .05, "Sine duty cycle");
    oscilloscope::Settings s; s.channel[0].acCoupled = true; s.channel[1].voltsPerDiv = 100;
    auto frame = oscilloscope::prepare(r, s);
    check(frame.ok && frame.triggered, "AC coupling permits meaningful zero-level trigger on biased input");
    near(frame.rawMean[0], 250, 1e-8, "Raw DC bias remains available in AC coupling");
    near(*std::max_element(frame.values[0].begin(), frame.values[0].end()), .2, 1e-8, "Digital AC coupling removes record mean");
    s.channel[0].scale = 10; frame = oscilloscope::prepare(r, s);
    near(*std::max_element(frame.values[0].begin(), frame.values[0].end()), 2, 1e-7, "Probe-equivalent scaling uses sample volts");
    s.triggerLevel = 1000; s.normal = true; frame = oscilloscope::prepare(r, s);
    check(frame.waiting && !frame.triggered, "Normal trigger does not manufacture a crossing");
    s.normal = false; check(!oscilloscope::prepare(r, s).waiting, "Auto trigger presents unmatched actual data");
    s.triggerLevel = 0; s.channel[0].scale = 1; s.horizontalPosition = .006;
    frame = oscilloscope::prepare(r, s);
    check(frame.start > .005 && frame.start < .009, "Pan navigates retained data");
    s.horizontalPosition = 0; s.triggerPosition = .5; frame = oscilloscope::prepare(r, s);
    near((frame.triggerTime-frame.start)/(s.timePerDiv*10), .5, .001, "Trigger screen position uses real pretrigger data");
    s.math = 2; s.mathScale = .5; frame = oscilloscope::prepare(r, s);
    near(frame.mathValues[250], .5*(frame.values[0][250]-frame.values[1][250]), 1e-12, "Waveform subtraction and gain use aligned samples");
    s.channel[0].offset = 250; s.channel[0].voltsPerDiv = .05; s.channel[0].position = 1;
    near(oscilloscope::divisions(250.1, s.channel[0]), 3, 1e-9, "High DC display offset and position mapping");
    near(oscilloscope::voltsAtDivisions(3, s.channel[0]), 250.1, 1e-9, "Voltage cursor inverse mapping is exact");
    s.t1 = .001; s.t2 = .003; near(oscilloscope::cursorFrequency(s), 500, 1e-9, "Cursor separation frequency");
    s.t2 = s.t1; check(oscilloscope::cursorFrequency(s) == 0, "Coincident cursors do not produce infinity");
    auto scaled = oscilloscope::autoscale(r, {}, false);
    near(scaled.channel[0].offset, 250, 1e-9, "Autoscale centers small AC on high DC bias");
    check(scaled.channel[1].voltsPerDiv >= 200.0/6, "Autoscale supports high-voltage range");
    near(scaled.timePerDiv, .0003, 1e-8, "Autoscale shows three measured periods");
    auto fit = oscilloscope::autoscale(r, {}, true);
    near(fit.timePerDiv*10, .01, 1e-12, "Fit shows the full record");
    auto dc = r; std::fill(dc.channel[0].begin(), dc.channel[0].end(), 250);
    m = oscilloscope::measure(dc.time, dc.channel[0], 0, .01);
    check(!m.frequency.ok && !m.riseTime.ok && !m.dutyPercent.ok, "DC timing measurements report unavailable");
    auto pulse = r;
    for (size_t i=0; i<pulse.time.size(); ++i) pulse.channel[0][i] = std::fmod(pulse.time[i]+.0001,.001)<.00025 ? 5.0 : 0.0;
    m=oscilloscope::measure(pulse.time,pulse.channel[0],0,.01);
    near(m.dutyPercent.value,25,.15,"25 percent pulse duty");
    check(m.edgeResolutionLimited,"Discontinuous sampled edge is flagged as sample-limited");
    auto impulse = r; std::fill(impulse.channel[0].begin(),impulse.channel[0].end(),0); impulse.channel[0][5237]=900;
    const auto indices=oscilloscope::envelope(impulse.time,impulse.channel[0],0,.01,100);
    check(std::find(indices.begin(),indices.end(),5237)!=indices.end(),"Display reduction preserves narrow spike extrema");
    check(indices.size()<=202,"Display point count is bounded by pixel budget");
    auto bad=r; bad.time[20]=bad.time[19]; check(!oscilloscope::prepare(bad,{}).ok,"Repeated timestamps rejected");
    bad=r; bad.channel[0][20]=std::numeric_limits<double>::quiet_NaN(); check(!oscilloscope::prepare(bad,{}).ok,"Nonfinite samples rejected");
    auto invalidSettings = s; invalidSettings.channel[0].scale = std::numeric_limits<double>::infinity();
    check(!oscilloscope::prepare(r,invalidSettings).ok,"Nonfinite input scaling rejected");
    std::vector<double> denseTime, denseVolts;
    for (int i=0;i<=100000;++i) { denseTime.push_back(i*1e-6); denseVolts.push_back(std::sin(2*pi*100000*i*1e-6)); }
    m=oscilloscope::measure(denseTime,denseVolts,0,.1);
    near(m.frequency.value,100000,0.01,"Dense record frequency uses all crossings in one pass");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
