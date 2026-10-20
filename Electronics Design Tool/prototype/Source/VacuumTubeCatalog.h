#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace parts
{

struct VacuumTubeSpec
{
    juce::String id;
    juce::String name;
    juce::String subcategory;
    juce::String classification;
    juce::String construction;
    juce::String pinout;
    juce::String heater;
    juce::String maxRatings;
    juce::String typicalOperation;
    juce::String provenance;
    bool simulated = false;
};

const VacuumTubeSpec* findVacuumTubeSpec(const juce::String& symbolId);
const std::vector<VacuumTubeSpec>& getAllVacuumTubeSpecs();
bool isVacuumTube(const juce::String& symbolId);

} // namespace parts
