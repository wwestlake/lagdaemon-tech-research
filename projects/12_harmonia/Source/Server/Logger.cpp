#include "Logger.h"
#include <iostream>

namespace Harmonia {
namespace Server {

juce::CriticalSection Logger::lock_;
juce::File            Logger::logFile_;
bool                  Logger::logToFile_ = false;
Logger::Level         Logger::minLevel_  = Logger::Level::Debug;

void Logger::setLogFile(const juce::String& path) {
    juce::ScopedLock sl(lock_);
    logFile_ = juce::File(path);
    logToFile_ = true;
    if (logFile_.existsAsFile())
        logFile_.deleteFile();
    logFile_.create();
}

void Logger::log(Level level, const juce::String& msg) {
    if (level < minLevel_) return;

    juce::String timestamp = juce::Time::getCurrentTime().formatted("[%Y-%m-%d %H:%M:%S] ");
    juce::String levelStr;
    juce::String colorCode;
    const juce::String resetCode = "\033[0m";

    switch (level) {
        case Level::Debug: levelStr = "[DEBUG] "; colorCode = "\033[90m"; break;
        case Level::Info:  levelStr = "[INFO]  "; colorCode = "\033[32m"; break;
        case Level::Warn:  levelStr = "[WARN]  "; colorCode = "\033[33m"; break;
        case Level::Error: levelStr = "[ERROR] "; colorCode = "\033[31m"; break;
        default:           levelStr = "[?]     "; colorCode = "";          break;
    }

    juce::String fullMsg = timestamp + levelStr + msg;

    {
        juce::ScopedLock sl(lock_);
        std::cout << colorCode << fullMsg.toStdString() << resetCode << std::endl;
        if (logToFile_)
            logFile_.appendText(fullMsg + "\n");
    }
}

void Logger::debug(const juce::String& msg) { log(Level::Debug, msg); }
void Logger::info(const juce::String& msg)  { log(Level::Info,  msg); }
void Logger::warn(const juce::String& msg)  { log(Level::Warn,  msg); }
void Logger::error(const juce::String& msg) { log(Level::Error, msg); }

} // namespace Server
} // namespace Harmonia
