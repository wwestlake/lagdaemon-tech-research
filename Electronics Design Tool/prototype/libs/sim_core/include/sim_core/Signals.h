#pragma once

// Typed signals on the bus (architecture section 06, decision F7).
//
// Kinds: continuous (a real quantity with a unit, read held or extrapolated
// within a step), discrete (an integer, boolean or real that changes only at
// events), and event (timestamped occurrences with a payload). A role marks
// what a signal is for: ordinary model data, commands from tests, the UI or
// the agent, read-only measurements, or fault injection points (the fault
// mechanics themselves arrive in phase P3).

#include "sim_core/Time.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sim
{
enum class ValueType { Real, Integer, Boolean };
enum class SignalKind { Continuous, Discrete, Event };
enum class SignalRole { Normal, Command, Measurement, Fault };
enum class InputPolicy { Hold, Extrapolate };

const char* toString(ValueType t);
const char* toString(SignalKind k);
const char* toString(SignalRole r);

struct Value
{
    ValueType type = ValueType::Real;
    double real = 0.0;
    std::int64_t integer = 0; // Integer, and Boolean as 0/1

    static Value ofReal(double v) { Value x; x.type = ValueType::Real; x.real = v; return x; }
    static Value ofInteger(std::int64_t v) { Value x; x.type = ValueType::Integer; x.integer = v; return x; }
    static Value ofBoolean(bool v) { Value x; x.type = ValueType::Boolean; x.integer = v ? 1 : 0; return x; }

    double asReal() const { return type == ValueType::Real ? real : (double)integer; }
    bool asBoolean() const { return type == ValueType::Real ? real != 0.0 : integer != 0; }

    // Bitwise equality (NaN equal to the same NaN, +0 different from -0):
    // what determinism and initialisation convergence need.
    bool identical(const Value& o) const
    {
        return type == o.type && integer == o.integer && std::memcmp(&real, &o.real, sizeof real) == 0;
    }
};

struct EventPayload
{
    std::int64_t code = 0;
    double value = 0.0;
};

// A participant's input or output port.
struct PortSpec
{
    std::string name;
    SignalKind kind = SignalKind::Continuous;
    ValueType type = ValueType::Real;
    std::string unit = "1";
    SignalRole role = SignalRole::Normal;

    // Inputs
    bool required = true;          // an unbound required input refuses configuration
    Value defaultValue;            // the value an unbound optional input reads
    InputPolicy policy = InputPolicy::Extrapolate; // continuous inputs
    Tick maxExtrapolation = 0;     // warn when reading further ahead of the source (0 = no limit)

    // Outputs
    int derivatives = 0;           // continuous outputs: 0, 1 or 2 time derivatives supplied
    std::vector<std::string> dependsOn; // inputs this output depends on with no delay (direct feedthrough)

    static PortSpec continuous(std::string name, std::string unit, int derivatives = 0)
    {
        PortSpec p;
        p.name = std::move(name);
        p.unit = std::move(unit);
        p.derivatives = derivatives;
        return p;
    }
    static PortSpec discrete(std::string name, ValueType type, std::string unit = "1")
    {
        PortSpec p;
        p.name = std::move(name);
        p.kind = SignalKind::Discrete;
        p.type = type;
        p.unit = std::move(unit);
        p.defaultValue.type = type;
        return p;
    }
    static PortSpec event(std::string name)
    {
        PortSpec p;
        p.name = std::move(name);
        p.kind = SignalKind::Event;
        return p;
    }
};
}
