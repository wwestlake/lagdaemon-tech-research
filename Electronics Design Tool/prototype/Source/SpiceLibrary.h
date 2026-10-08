#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>

namespace spice_library
{
    struct ModelDef
    {
        juce::String name;
        juce::String kind; // "NPN", "PNP", "NJF", "PJF", "NMOS", "PMOS", "D", "SUBCKT"
        juce::String rawText;
        std::map<juce::String, juce::String> parameters;
    };

    void initialize();
    const ModelDef* findModel(const juce::String& name);
    std::vector<juce::String> availableModelsFor(const juce::String& kind);
    juce::String resolveModelText(const juce::String& name);
}
