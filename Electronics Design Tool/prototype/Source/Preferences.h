#pragma once

#include <JuceHeader.h>

#include <functional>
#include <vector>

// Per-user preferences: one table of settings (key, category, label,
// description, kind, choices, default) that the Preferences window, the
// agent's preference tools, and the code reading them all share. Saved to
// %APPDATA%\DjehutiElectronicsLab\preferences.json.
namespace prefs
{
enum class Kind { Toggle, Choice, Folder };

struct Setting
{
    juce::String key;          // stable id, e.g. "layout.supply_symbols"
    juce::String category;     // "Layout", "Display", ...
    juce::String label;
    juce::String description;
    Kind kind = Kind::Toggle;
    juce::StringArray options; // Choice
    juce::String defaultValue; // Toggle: "true"/"false"
};

const std::vector<Setting>& all();
const Setting* find(const juce::String& key);
juce::StringArray categories();

juce::String get(const juce::String& key);
bool isOn(const juce::String& key);
bool set(const juce::String& key, const juce::String& value, juce::String& error);

// Called after any change (on the message thread).
int addListener(std::function<void(const juce::String& key)> listener);
void removeListener(int id);
}
