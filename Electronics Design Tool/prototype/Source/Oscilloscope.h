#pragma once

#include "SignalMeasure.h"
#include <array>
#include <string>
#include <memory>
#include <vector>

// Sample processing and presentation only. Acquisitions come from the existing
// circuit solver or fixture render; this module contains no electrical solver.
namespace oscilloscope
{
struct Record
{
    bool ok = false;
    std::string error;
    std::vector<double> time;
    std::array<std::vector<double>, 2> channel;
    std::array<bool, 2> connected { false, false };
};

struct Channel
{
    bool enabled = true, acCoupled = false;
    double voltsPerDiv = 0.5, position = 0.0, offset = 0.0, scale = 1.0;
};
struct Settings
{
    std::array<Channel, 2> channel;
    double timePerDiv = 200e-6, horizontalPosition = 0.0;
    int triggerChannel = 0;
    double triggerLevel = 0.0, triggerPosition = 0.2;
    bool rising = true, normal = false;
    bool cursors = false, markers = false;
    int cursorChannel = 0;
    double t1 = 0.0, t2 = 1e-3, v1 = 0.0, v2 = 1.0;
    int math = 0; // 0 off, 1 sum, 2 difference
    double mathScale = 1.0, mathVoltsPerDiv = 1.0;
};

struct Measurements
{
    signal_measure::Result minimum, maximum, mean, peakToPeak, rms, frequency, period;
    signal_measure::Result riseTime, fallTime, pulseWidth, dutyPercent;
    bool edgeResolutionLimited = false;
};

struct Frame
{
    bool ok = false, triggered = false, waiting = false;
    std::string error;
    double start = 0.0, stop = 0.0, triggerTime = 0.0, sampleInterval = 0.0;
    std::array<double, 2> rawMean { 0, 0 };
    std::array<std::vector<double>, 2> values; // scaled/coupled volts; offset is display-only
    std::vector<double> mathValues;
    std::array<Measurements, 2> measurements;
};
struct Display
{
    std::shared_ptr<const Record> record;
    Frame frame;
    Settings settings;
    std::string status;
    bool running = true;
};

Measurements measure(const std::vector<double>& time, const std::vector<double>& volts, double from, double to);
Frame prepare(const Record&, const Settings&);
Settings autoscale(const Record&, Settings, bool fitRecord);
// Shared coordinate mapping for traces, cursor readouts and mouse gestures.
double divisions(double volts, const Channel&);
double voltsAtDivisions(double divisions, const Channel&);
double cursorFrequency(const Settings&); // 0 when cursors coincide
std::vector<size_t> envelope(const std::vector<double>& time, const std::vector<double>& volts,
                             double from, double to, int pixels);
}
