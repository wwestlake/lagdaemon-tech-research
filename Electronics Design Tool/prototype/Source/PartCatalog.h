#pragma once

#include <JuceHeader.h>

#include <functional>
#include <vector>

// What each part type's editable properties are: label, kind of control,
// unit, choices, default, and where the value is stored on the part. The
// properties pane builds its controls from this, the simulator reads part
// values through it, and the agent's tools validate against it.
namespace parts
{
enum class Kind
{
    Quantity, // number with engineering prefix and unit (4.7k, 10u)
    Text,     // free text (model name, net name)
    Choice,   // one of `options`
    Toggle,   // on/off; options = { offText, onText }
    Fraction  // 0..1 slider (potentiometer wiper)
};

enum class Storage { Value, Frequency, BusName, Family, ManufacturerPart, Param };

struct ParamSpec
{
    juce::String key;        // stable id used by tools and saved files
    juce::String label;
    Kind kind = Kind::Text;
    juce::String unit;       // Quantity: ohm, F, H, V, A, Hz, deg, ...
    juce::StringArray options;
    juce::String defaultValue;
    Storage storage = Storage::Param;
    juce::String help;
    juce::String showWhen;   // "waveform=Pulse|Exp": shown only when that property has one of those values
};

// True when `spec` applies given the part's other values (see showWhen).
bool isShown(const ParamSpec& spec, const std::function<juce::String(const juce::String&)>& valueOf);

const std::vector<ParamSpec>& paramsFor(const juce::String& symbolId);
const ParamSpec* findParam(const juce::String& symbolId, const juce::String& key);

// Human name of a part type ("Resistor", "NPN Transistor").
juce::String displayName(const juce::String& symbolId);

// Checks a value against its spec; on failure `error` explains why.
bool validate(const ParamSpec& spec, const juce::String& value, juce::String& error);
}
