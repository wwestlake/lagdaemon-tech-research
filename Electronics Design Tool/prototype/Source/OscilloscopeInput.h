#pragma once
#include "CircuitSolver.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace oscilloscope
{
inline bool isInputSetting(const std::string& key)
{
    return key == "ch1_impedance" || key == "ch2_impedance"
        || key == "ch1_impedance_enabled" || key == "ch2_impedance_enabled";
}

// Strict resistance units, using the existing engineering-value parser. For
// this control M always means mega, independently of the SPICE capital-M option.
inline bool parseInputOhms(std::string text, double& ohms)
{
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; }), text.end());
    size_t pos = 0;
    try { std::stod(text, &pos); } catch (...) { return false; }
    auto suffix = text.substr(pos);
    std::string lower = suffix;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    size_t prefix = 0;
    if (lower.rfind("meg", 0) == 0) prefix = 3;
    else if (!suffix.empty() && std::string("TGMkKmunpf").find(suffix[0]) != std::string::npos) prefix = 1;
    const auto unit = lower.substr(prefix);
    if (!unit.empty() && unit != "ohm" && unit != "ohms" && unit != "r" && unit != "\xce\xa9") return false;
    auto value = text.substr(0, pos) + suffix.substr(0, prefix);
    if (prefix == 1 && suffix[0] == 'M') value = text.substr(0, pos) + "Meg";
    return circuit_sim::parseValue(value, ohms) && std::isfinite(ohms) && ohms > 0.0 && std::isfinite(1.0 / ohms);
}

// Both hosts add the same ordinary resistor to their existing solver netlist.
// Off adds no element at all. Trace visibility/coupling never affect loading.
inline int addInputLoad(circuit_sim::Circuit& circuit, const std::string& name,
                        circuit_sim::Node signal, circuit_sim::Node reference, bool enabled, double ohms)
{
    if (!enabled || signal == reference) return -1;
    const auto index = circuit.addResistor(name, signal, reference, ohms);
    circuit.elements()[(size_t)index].noiseless = true;
    return index;
}
}
