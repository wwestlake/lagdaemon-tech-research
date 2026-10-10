#pragma once

#include <JuceHeader.h>

#include "Analytics.h"

namespace xyce_backend
{
    enum class Engine
    {
        Xyce,
        InternalSolver
    };

    juce::String engineName(Engine engine);
    juce::File configuredExecutable();
    bool isConfigured(juce::String& detail);

    // The lines of Xyce's output that explain a failure (errors, fatal
    // messages, warnings), in Xyce's own words, repeated lines once, at most
    // maxLines; the log's last lines when nothing matches.
    juce::StringArray diagnosticLines(const juce::String& output, int maxLines = 8);
    // A short explanation for an unambiguous failure, using the circuit's node names; empty otherwise.
    juce::String explainFailure(const juce::StringArray& lines, const analytics::Netlist& netlist);
    // Node names exactly as the generated netlist writes them (what V(name) must use).
    juce::StringArray nodeNames(const analytics::Netlist& netlist);
    // Node names referenced by V(...) in a behavioral expression that are not nodes of the circuit.
    juce::StringArray unresolvedNodes(const juce::String& expression, const analytics::Netlist& netlist);
    // Why the circuit cannot be written as a Xyce netlist (model bindings, missing library models), or empty.
    juce::String netlistProblem(const analytics::Netlist& netlist);

    analytics::Result run(analytics::Analysis analysis,
                          const analytics::Settings& settings,
                          const analytics::Netlist& netlist,
                          const juce::File& outputRoot);
}
