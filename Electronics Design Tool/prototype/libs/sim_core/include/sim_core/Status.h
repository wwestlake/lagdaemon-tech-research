#pragma once

#include <string>
#include <utility>

namespace sim
{
// The result of an operation that can fail: ok, or a message saying what went
// wrong and where (participant, port, time). Failures are never silent.
struct Status
{
    bool ok = true;
    std::string message;

    static Status success() { return {}; }
    static Status failure(std::string text) { return { false, std::move(text) }; }
    explicit operator bool() const { return ok; }
};
}
