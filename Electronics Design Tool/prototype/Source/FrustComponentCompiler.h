#pragma once

#include "FrustComponent.h"

#include <juce_core/juce_core.h>

namespace frust_component
{

juce::File programFileFor(const Definition& definition,
                          const juce::File& definitionFile = {},
                          const juce::File& projectFolder = {});

bool compileProgramFile(const Definition& definition,
                        const juce::File& programFile,
                        juce::String& error,
                        juce::String* source = nullptr,
                        juce::StringArray* notes = nullptr);

}
