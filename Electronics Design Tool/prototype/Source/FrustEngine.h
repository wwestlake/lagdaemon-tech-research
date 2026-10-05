#pragma once

// The embedded Frust compiler. The app writes Frust source, this compiles it
// in memory (LLVM JIT through frust_plugin_host; nothing is written to disk and
// no program is started) and hands back the compiled functions to call.

#include <map>
#include <mutex>
#include <string>
#include <vector>

struct FrustPluginHandleImpl;

namespace frust_engine
{
struct Diagnostic
{
    bool error = true;
    std::string file;
    int line = 0;    // 1-based, 0 when unknown
    int column = 0;  // 1-based, 0 when unknown
    std::string message;

    std::string text() const; // "file:line:col: error: message"
};

struct Result
{
    bool ok = false;        // compiled (and, for runScript, ran)
    bool compiled = false;
    std::vector<Diagnostic> diagnostics;
    std::string output;     // what a script's run() returned
    std::string error;      // a failure that is not a compile diagnostic
    double compileMs = 0.0;
    double runMs = 0.0;

    std::string report() const; // diagnostics and error as text, one per line
};

// The manifest line every compiled unit starts with.
std::string manifestLine(const std::string& name, const std::string& description,
                         const std::vector<std::string>& hostFunctions = {});

// Compile-only check of a complete unit (manifest line included).
Result check(const std::string& unitName, const std::string& source);

// Compiled units, each by key; functions are found by name. Loading a key
// again replaces it. Thread-safe; loading is serialized (the plugin host's
// state is process-wide).
class Engine
{
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    Result load(const std::string& key, const std::string& source);
    void* function(const std::string& key, const std::string& name) const;
    void unload(const std::string& key);
    void unloadAll();
    bool isLoaded(const std::string& key) const;

private:
    std::map<std::string, FrustPluginHandleImpl*> units;
    mutable std::mutex mutex;
};

// A script: Frust defining `pub fn run() -> String`. The manifest line is
// added in front (diagnostics are counted in the script as written); it is
// compiled, run once, and unloaded. `print_line(text: String) -> i64` adds a
// line to the output.
Result runScript(const std::string& script);
}
