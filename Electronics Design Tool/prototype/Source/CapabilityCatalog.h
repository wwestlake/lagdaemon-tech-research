#pragma once

#include <JuceHeader.h>

#include <vector>

// What the Workbench can actually do, read from the application's own
// registries (symbol table, part parameter catalog, analysis table), so the
// agent discovers components, instruments and analyses instead of guessing.
// Nothing here is a hand-kept inventory: a part or analysis added to those
// registries appears here automatically.
namespace capability_catalog
{
// A programmable component definition in the open project (the Workbench
// fills these: the catalog has no project of its own).
struct ProgrammableDefinition
{
    juce::String name;
    juce::StringArray pins;      // pin names, in pin order
    juce::StringArray instances; // refdes of its instances in the open diagram
};

struct Request
{
    juce::String section;  // "components", "instruments", "analyses", "engines", "programmable", or empty for a summary of all
    juce::String query;    // optional: search terms (space or comma separated), each matched separately; see searchRule in the result
    juce::String symbolId; // optional: one component in full detail
    // Programmable components (filled by the Workbench): whether a project is
    // open, its definitions, and the Node Designer's component node types
    // ({type, title, inputs, outputs, parameters} as node_program_node_types
    // reports them).
    bool projectOpen = false;
    std::vector<ProgrammableDefinition> programmable;
    juce::var componentNodeTypes;
};

// The JSON tool result (ok:false with a reason, and close matches, for an unknown symbolId).
juce::String toJson(const Request& request);

// Every supported component id, comma separated, for tool descriptions.
juce::String componentIdList();

// Registered component ids each name refers to (by id, display name, the id
// without a qualifier such as _generic, or a common synonym). Empty ids: no
// registered part is called that.
struct TermResolution
{
    juce::String name;
    juce::StringArray ids;
};
std::vector<TermResolution> resolve(const juce::StringArray& names);

// Why a capability-gap claim that parts are missing is false (the registry
// has them), or empty when nothing claimed missing exists. `components`
// names the parts claimed missing; without it, component-category claims are
// read from the description and needed capability.
juce::String checkGapClaim(const juce::String& category, const juce::String& description, const juce::String& neededCapability,
                           const juce::StringArray& components);
}
