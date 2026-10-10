#pragma once

// Diagnostics are part of the engine's contract (architecture section 15):
// they stay compiled into optimised builds and every failure says which
// participant, what time and why.
//
// Diagnostics that depend on the wall clock (timing: speed, overruns, lag)
// are marked so; they are reported live and in the run's side data but kept
// out of the deterministic recording, which must replay bit for bit.

#include "sim_core/Time.h"

#include <functional>
#include <string>
#include <vector>

namespace sim
{
enum class Severity { Info, Warning, Error };
enum class Category { Configuration, Lifecycle, Convergence, Numerical, Timing, Synchronisation, Events };

const char* toString(Severity s);
const char* toString(Category c);

struct Diagnostic
{
    SimTime time;
    Severity severity = Severity::Info;
    Category category = Category::Lifecycle;
    std::string participant; // empty for the scheduler itself
    std::string message;
    bool wallClockDependent = false;
};

class DiagnosticLog
{
public:
    void add(Diagnostic d)
    {
        if (listener) listener(d);
        entries.push_back(std::move(d));
    }
    const std::vector<Diagnostic>& all() const { return entries; }
    std::size_t count(Severity s) const
    {
        std::size_t n = 0;
        for (const auto& d : entries) n += d.severity == s;
        return n;
    }
    void clear() { entries.clear(); }
    std::function<void(const Diagnostic&)> listener;

private:
    std::vector<Diagnostic> entries;
};
}
