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

    analytics::Result run(analytics::Analysis analysis,
                          const analytics::Settings& settings,
                          const analytics::Netlist& netlist,
                          const juce::File& outputRoot);
}
