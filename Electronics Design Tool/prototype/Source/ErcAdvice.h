#pragma once

#include <JuceHeader.h>

// Electrical-rule-check findings as data an agent (or person) can act on:
// what is wrong, where (part, pin, net), what it is connected to now, and
// which Workbench operations would correct it. The checks themselves live
// with the schematic; this turns each one into a finding with suggestions
// built from real tool names and the circuit's own nets, never from fixed
// voltages or part-specific rules.
namespace erc_advice
{
struct Finding
{
    juce::String severity;   // "ERROR", "WARN", "INFO"
    juce::String category;   // stable id such as unconnected_supply_pin
    juce::String message;
    juce::String refdes;     // the part's id in every tool ("" for circuit-wide findings)
    juce::String symbolId;
    juce::String pin;        // pin name; label for tools is refdes.pin
    int pinIndex = -1;
    juce::String net;
    juce::StringArray connections; // the part's other pins and their nets, "U1.OUT on n4"
    juce::StringArray suggestions;
};

// Supply pins by the usual naming (V+, V-, VCC, VEE, VDD, VSS, VS+, VS-):
// +1 positive rail, -1 negative rail, 0 not a supply pin.
int supplyPolarity(const juce::String& pinName);

struct Context
{
    juce::StringArray supplyNets; // named supply nets in the circuit, "+12V"
    juce::StringArray groundNets; // usually "GND"
};

// Fills `suggestions` from the finding's category and the circuit context.
void suggest(Finding& finding, const Context& context);

juce::var toVar(const Finding& finding);
juce::String markdownLine(const Finding& finding); // "- [ERROR] message Fix: ..."
}
