#include "SignalGraph.h"

namespace cw
{
namespace
{
const juce::Colour sourceColour { 0xff4fd3ff };
const juce::Colour effectColour { 0xffb67bff };
const juce::Colour sinkColour { 0xff69e3a4 };

juce::Array<NodeTemplate> makeTemplates()
{
    juce::Array<NodeTemplate> templates;
    templates.add({ NodeCategory::source, "File Player", "Streams a file into the graph.", "Path", 0.5f });
    templates.add({ NodeCategory::source, "Mic In", "Live input from the audio device.", "Gain", 0.7f });
    templates.add({ NodeCategory::source, "App Loopback", "Captures audio from another app.", "Mix", 0.6f });
    templates.add({ NodeCategory::source, "Oscillator", "Continuous generated tone source.", "Level", 0.0f });
    templates.add({ NodeCategory::effect, "Drive", "Soft saturation and color.", "Amount", 0.2f });
    templates.add({ NodeCategory::effect, "Filter", "Shapes brightness and body.", "Cutoff", 0.55f });
    templates.add({ NodeCategory::effect, "Delay", "Repeating spatial smear.", "Time", 0.18f });
    templates.add({ NodeCategory::effect, "Modulator", "LFO / automation target.", "Rate", 0.42f });
    templates.add({ NodeCategory::effect, "VST Host", "Loads a third-party VST into the patch chain.", "Mix", 0.5f });
    templates.add({ NodeCategory::sink, "Speakers", "Main output to your audio device.", "Level", 1.0f });
    templates.add({ NodeCategory::sink, "File Capture", "Records to disk or a render target.", "Format", 0.5f });
    return templates;
}
}

juce::String nodeCategoryName(NodeCategory category)
{
    switch (category)
    {
        case NodeCategory::source: return "Source";
        case NodeCategory::effect: return "Effect";
        case NodeCategory::sink: return "Sink";
    }

    return "Node";
}

juce::Colour nodeCategoryColour(NodeCategory category)
{
    switch (category)
    {
        case NodeCategory::source: return sourceColour;
        case NodeCategory::effect: return effectColour;
        case NodeCategory::sink: return sinkColour;
    }

    return juce::Colours::white;
}

juce::Array<NodeTemplate> getStarterNodeTemplates()
{
    return makeTemplates();
}
} // namespace cw
