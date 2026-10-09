#pragma once

#include <JuceHeader.h>

// What the Workbench can actually do, read from the application's own
// registries (symbol table, part parameter catalog, analysis table), so the
// agent discovers components, instruments and analyses instead of guessing.
// Nothing here is a hand-kept inventory: a part or analysis added to those
// registries appears here automatically.
namespace capability_catalog
{
struct Request
{
    juce::String section;  // "components", "instruments", "analyses", "engines", or empty for a summary of all
    juce::String query;    // optional: keep entries whose id, name, pins or parameter text contain all its words
    juce::String symbolId; // optional: one component in full detail
};

// The JSON tool result (ok:false with a reason, and close matches, for an unknown symbolId).
juce::String toJson(const Request& request);

// Every supported component id, comma separated, for tool descriptions.
juce::String componentIdList();
}
