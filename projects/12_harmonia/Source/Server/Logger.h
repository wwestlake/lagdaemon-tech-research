#pragma once

#include <juce_core/juce_core.h>

// Undefine Windows macros that conflict with our enum values
#ifdef ERROR
  #undef ERROR
#endif
#ifdef DEBUG
  #undef DEBUG
#endif

namespace Harmonia {
namespace Server {

class Logger {
public:
    enum class Level { Debug, Info, Warn, Error };

    static void log(Level level, const juce::String& msg);
    static void setLogFile(const juce::String& path);

    static void debug(const juce::String& msg);
    static void info(const juce::String& msg);
    static void warn(const juce::String& msg);
    static void error(const juce::String& msg);

private:
    static juce::CriticalSection lock_;
    static juce::File            logFile_;
    static bool                  logToFile_;
    static Level                 minLevel_;
};

} // namespace Server
} // namespace Harmonia
