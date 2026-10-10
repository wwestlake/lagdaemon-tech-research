#include "FrustEngine.h"

#include <frust_plugin_host/FrustPluginHost.h>
#include <frust_plugin_host/FrustPluginHostSource.h>
#include <CompilerApi.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>

namespace frust_engine
{
namespace
{
// The plugin host keeps its application identity and host-function table per
// process, so loads happen one at a time.
std::mutex& loadMutex()
{
    static std::mutex m;
    return m;
}

double millisecondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

std::string frustStringLiteral(const std::string& text)
{
    std::string literal = "\"";
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
            literal += '\\';
        literal += c;
    }
    return literal + "\"";
}

Diagnostic convert(const ::frust::Diagnostic& d, const std::string& unitName, int lineOffset)
{
    Diagnostic out;
    out.error = d.severity == ::frust::Diagnostic::Severity::Error;
    out.file = d.file.empty() && d.line > 0 ? unitName : d.file;
    out.line = d.line;
    if (out.file == unitName && out.line > lineOffset)
        out.line -= lineOffset;
    out.column = d.column;
    out.message = d.message;
    return out;
}

::frust::CompileRequest requestFor(const std::string& unitName, const std::string& source)
{
    ::frust::CompileRequest request;
    request.sources.push_back({ unitName, source });
    return request;
}

// Lines a running script logs; a script runs on one thread.
thread_local std::vector<std::string>* scriptLog = nullptr;

extern "C" std::int64_t djehuti_frust_log(const char* text)
{
    if (scriptLog != nullptr)
        std::cout << (text != nullptr ? text : "") << "\n";   // into the captured output, in order
    return 0;
}

// Math the generated audio DSP needs (Frust has no exp or log of its own).
extern "C" double djehuti_dsp_exp(double x) { return std::exp(x); }
extern "C" double djehuti_dsp_log(double x) { return std::log(x); }

// The Frust compiler emits a call to frust_dbg_tick(line, column) before every
// expression (its IDE debugger hook). The app does not step through Frust code,
// so it is a no-op; without it no compiled unit links.
extern "C" void djehuti_frust_dbg_tick(int, int) {}

void ensureHostSetup()
{
    static std::once_flag once;
    std::call_once(once, [] {
        frust_plugin_host_set_application_identity("djehuti-electronics-lab");
        frust_plugin_register_host_function("frust_dbg_tick", reinterpret_cast<void*>(&djehuti_frust_dbg_tick));
        frust_plugin_register_host_function("djehuti_frust_log", reinterpret_cast<void*>(&djehuti_frust_log));
        frust_plugin_register_host_function("djehuti_dsp_exp", reinterpret_cast<void*>(&djehuti_dsp_exp));
        frust_plugin_register_host_function("djehuti_dsp_log", reinterpret_cast<void*>(&djehuti_dsp_log));
    });
}

Result checkWithOffset(const std::string& unitName, const std::string& source, int lineOffset)
{
    Result result;
    auto request = requestFor(unitName, source);
    request.emitObject = false;
    const auto start = std::chrono::steady_clock::now();
    const auto compiled = ::frust::Compile(request);
    result.compileMs = millisecondsSince(start);
    for (const auto& d : compiled.diagnostics)
        result.diagnostics.push_back(convert(d, unitName, lineOffset));
    result.compiled = compiled.ok;
    result.ok = compiled.ok;
    return result;
}
}

std::string Diagnostic::text() const
{
    std::string s = file.empty() ? std::string("frust") : file;
    if (line > 0) s += ":" + std::to_string(line);
    if (column > 0) s += ":" + std::to_string(column);
    return s + ": " + (error ? "error: " : "warning: ") + message;
}

std::string Result::report() const
{
    std::string s;
    for (const auto& d : diagnostics)
        s += d.text() + "\n";
    if (!error.empty())
        s += error + "\n";
    return s;
}

std::string manifestLine(const std::string& name, const std::string& description, const std::vector<std::string>& hostFunctions)
{
    std::string id;
    for (const char c : name)
        id += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') ? c : '_';
    std::string required;
    for (const auto& fn : hostFunctions)
        required += std::string(required.empty() ? "" : ",") + "{\"name\":\"" + fn + "\"}";
    const std::string json = "{\"name\":\"" + (id.empty() ? std::string("unit") : id) + "\",\"version\":\"0.1.0\",\"description\":"
                           + frustStringLiteral(description)
                           + (required.empty() ? std::string() : ",\"requiredHostFunctions\":[" + required + "]") + "}";
    return "manifest " + frustStringLiteral(json) + ";\n";
}

Result check(const std::string& unitName, const std::string& source)
{
    return checkWithOffset(unitName, source, 0);
}

Engine::Engine()
{
    ensureHostSetup();
}

Engine::~Engine()
{
    unloadAll();
}

Result Engine::load(const std::string& key, const std::string& source)
{
    const auto unitName = key + ".frust";
    auto result = checkWithOffset(unitName, source, 0);
    if (!result.ok)
        return result;
    const auto start = std::chrono::steady_clock::now();
    FrustPluginHandle handle = nullptr;
    {
        std::lock_guard<std::mutex> loading(loadMutex());
        handle = frust_plugin_host::loadFromSource(requestFor(unitName, source));
        if (handle == nullptr)
        {
            const char* why = frust_plugin_last_error();
            result.error = why != nullptr && *why != '\0' ? why : "The Frust unit compiled but could not be loaded.";
        }
    }
    result.compileMs += millisecondsSince(start);
    if (handle == nullptr)
    {
        result.ok = false;
        return result;
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (auto found = units.find(key); found != units.end())
        frust_plugin_unload(found->second);
    units[key] = handle;
    return result;
}

void* Engine::function(const std::string& key, const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto found = units.find(key);
    return found != units.end() ? frust_plugin_get_fn(found->second, name.c_str()) : nullptr;
}

void Engine::unload(const std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (auto found = units.find(key); found != units.end())
    {
        frust_plugin_unload(found->second);
        units.erase(found);
    }
}

void Engine::unloadAll()
{
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& [key, handle] : units)
        frust_plugin_unload(handle);
    units.clear();
}

bool Engine::isLoaded(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return units.count(key) != 0;
}

Result runScript(const std::string& script)
{
    ensureHostSetup();
    const std::string unitName = "script.frust";
    const std::string header = manifestLine("electronics_lab_script", "A script run in Djehuti Electronics Lab.", { "djehuti_frust_log" })
                             + "extern fn djehuti_frust_log(text: String) -> i64;\n"
                               "pub fn print_line(text: String) -> i64 = { djehuti_frust_log(text) }\n";
    constexpr int headerLines = 3;
    const auto source = header + script;

    auto result = checkWithOffset(unitName, source, headerLines);
    if (!result.ok)
        return result;

    FrustPluginHandle handle = nullptr;
    const auto start = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> loading(loadMutex());
        handle = frust_plugin_host::loadFromSource(requestFor(unitName, source));
        if (handle == nullptr)
        {
            const char* why = frust_plugin_last_error();
            result.error = why != nullptr && *why != '\0' ? why : "The script compiled but could not be loaded.";
        }
    }
    result.compileMs += millisecondsSince(start);
    if (handle == nullptr)
    {
        result.ok = false;
        return result;
    }

    using RunFn = const char* (*)();
    auto* run = reinterpret_cast<RunFn>(frust_plugin_get_fn(handle, "run"));
    if (run == nullptr)
    {
        result.ok = false;
        result.error = "The script has no `pub fn run() -> String`; define it, and what it returns is reported.";
        frust_plugin_unload(handle);
        return result;
    }
    // The FRust runtime prints (frust_print_str, which node programs' Print
    // nodes call) to standard output; while the script runs that is captured,
    // with its print_line lines in order, as the script's output.
    std::vector<std::string> lines;
    std::ostringstream printed;
    auto* consoleBuffer = std::cout.rdbuf(printed.rdbuf());
    scriptLog = &lines;
    const auto runStart = std::chrono::steady_clock::now();
    const char* returned = run();
    result.runMs = millisecondsSince(runStart);
    scriptLog = nullptr;
    std::cout.rdbuf(consoleBuffer);
    result.output += printed.str();
    if (returned != nullptr)
        result.output += returned;
    frust_plugin_unload(handle);
    result.ok = true;
    return result;
}
}
