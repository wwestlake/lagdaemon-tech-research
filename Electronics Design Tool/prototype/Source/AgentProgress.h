#pragma once

#include <JuceHeader.h>

#include <map>
#include <set>
#include <string>

// Detects an agent run that has stopped making progress: it keeps making the
// same tool call (same tool, equivalent arguments) and getting materially the
// same result back, with nothing new happening in between. This is not a
// call budget: any call whose result has not been seen before in the run is
// progress and resets the count, so long productive runs are unaffected.
namespace agent_progress
{
// The result with volatile fields (paths, times, durations, request ids)
// removed and re-serialised, so two runs of the same check compare equal.
inline juce::var stripVolatile(const juce::var& v)
{
    static const char* volatileKeys[] { "path", "file", "dir", "time", "when", "duration", "seconds", "timestamp", "requestid", "runid" };
    if (auto* object = v.getDynamicObject())
    {
        auto* copy = new juce::DynamicObject();
        for (const auto& prop : object->getProperties())
        {
            const auto key = prop.name.toString().toLowerCase();
            bool skip = false;
            for (auto k : volatileKeys)
                skip = skip || key.contains(k);
            if (!skip)
                copy->setProperty(prop.name, stripVolatile(prop.value));
        }
        return juce::var(copy);
    }
    if (auto* array = v.getArray())
    {
        juce::Array<juce::var> copy;
        for (const auto& item : *array)
            copy.add(stripVolatile(item));
        return copy;
    }
    return v;
}

inline juce::String normalise(const juce::String& text)
{
    const auto parsed = juce::JSON::parse(text);
    if (parsed.isObject() || parsed.isArray())
        return juce::JSON::toString(stripVolatile(parsed), true);
    return text.trim();
}

class NoProgressGuard
{
public:
    explicit NoProgressGuard(int repeatsToStop = 3) : limit(repeatsToStop) {}

    // Records one completed call; true when the run should stop.
    bool record(const juce::String& tool, const juce::String& argumentsJson, const juce::String& result)
    {
        const auto signature = (tool + "\n" + normalise(argumentsJson) + "\n" + normalise(result)).toStdString();
        if (seen.insert(signature).second)
        {
            counts.clear(); // something new happened
            counts[signature] = 1;
            return false;
        }
        const auto n = ++counts[signature];
        if (n < limit)
            return false;
        lastTool = tool;
        repeats = n;
        return true;
    }

    juce::String reason() const
    {
        return "Stopped for lack of progress: " + lastTool + " was called " + juce::String(repeats)
             + " times with the same arguments and returned the same result each time, and nothing else changed in between. "
               "Act on what the result reports (change the circuit or settings) before calling it again.";
    }

private:
    int limit;
    std::set<std::string> seen;
    std::map<std::string, int> counts;
    juce::String lastTool;
    int repeats = 0;
};
}
