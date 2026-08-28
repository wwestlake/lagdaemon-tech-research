#pragma once

#include <JuceHeader.h>

namespace cw
{
enum class NodeCategory
{
    source,
    effect,
    sink
};

struct NodeTemplate
{
    NodeCategory category = NodeCategory::source;
    juce::String name;
    juce::String description;
    juce::String parameterLabel;
    float defaultValue = 0.5f;
};

juce::String nodeCategoryName(NodeCategory category);
juce::Colour nodeCategoryColour(NodeCategory category);
juce::Array<NodeTemplate> getStarterNodeTemplates();
} // namespace cw
