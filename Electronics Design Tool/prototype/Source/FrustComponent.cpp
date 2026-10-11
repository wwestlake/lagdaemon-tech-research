#include "FrustComponent.h"

#include <frust_plugin_host/FrustPluginHost.h>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace frust_component
{
juce::String roleName(PinRole role)
{
    switch (role)
    {
        case PinRole::Input: return "input";
        case PinRole::VoltageOutput: return "voltage_output";
        case PinRole::CurrentOutput: return "current_output";
    }
    return "input";
}

bool parseRole(const juce::String& text, PinRole& role)
{
    const auto t = text.trim().toLowerCase();
    if (t == "input" || t == "in") role = PinRole::Input;
    else if (t == "voltage_output" || t == "output" || t == "out") role = PinRole::VoltageOutput;
    else if (t == "current_output") role = PinRole::CurrentOutput;
    else return false;
    return true;
}

int Definition::pinIndex(const juce::String& id) const
{
    for (int i = 0; i < (int)pins.size(); ++i)
        if (pins[(size_t)i].id == id)
            return i;
    return -1;
}

std::vector<schematic::BlockPort> Definition::ports() const
{
    std::vector<schematic::BlockPort> list;
    for (const auto& p : pins)
        list.push_back({ p.id, p.side, p.order });
    return list;
}

void Definition::setLayout(const std::vector<schematic::BlockPort>& ports)
{
    for (size_t i = 0; i < pins.size() && i < ports.size(); ++i)
    {
        pins[i].side = ports[i].side;
        pins[i].order = ports[i].order;
    }
}

juce::var Definition::compilerContext() const
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> pinNames, parameterNames, stateNames;
    for (const auto& p : pins) pinNames.add(p.id);
    for (const auto& p : parameters) parameterNames.add(p.name);
    for (const auto& s : state) stateNames.add(s.name);
    o->setProperty("name", name);
    o->setProperty("pins", pinNames);
    o->setProperty("parameters", parameterNames);
    o->setProperty("state", stateNames);
    return juce::var(o);
}

bool isValidName(const juce::String& name)
{
    if (name.isEmpty() || !juce::CharacterFunctions::isLetter(name[0]))
        return false;
    return name.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
}

juce::String problemsWith(const Definition& d)
{
    juce::StringArray problems;
    if (!isValidName(d.name))
        problems.add("The component name '" + d.name + "' must start with a letter and use letters, digits and _.");
    if (d.pins.empty())
        problems.add("The component has no pins.");
    auto names = [&problems](const juce::String& what, const juce::StringArray& list) {
        juce::StringArray seen;
        for (const auto& n : list)
        {
            if (!isValidName(n))
                problems.add("The " + what + " name '" + n + "' must start with a letter and use letters, digits and _.");
            else if (seen.contains(n))
                problems.add("Two " + what + "s are named '" + n + "'.");
            seen.add(n);
        }
    };
    juce::StringArray pinIds, params, states;
    for (const auto& p : d.pins) pinIds.add(p.id);
    for (const auto& p : d.parameters) params.add(p.name);
    for (const auto& s : d.state) states.add(s.name);
    names("pin", pinIds);
    names("parameter", params);
    names("state variable", states);
    for (const auto& p : d.pins)
        if (p.reference.isNotEmpty() && (p.reference == p.id || !pinIds.contains(p.reference)))
            problems.add("Pin " + p.id + "'s reference must be another pin of the component (or empty for ground), not '" + p.reference + "'.");
    if (!(d.outputResistance > 0.0))
        problems.add("The output resistance must be greater than 0 ohms.");
    return problems.joinIntoString("\n");
}

juce::var toVar(const Definition& d)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("schemaVersion", 1);
    o->setProperty("kind", "frust_component");
    o->setProperty("name", d.name);
    o->setProperty("description", d.description);
    juce::Array<juce::var> pins, params, states;
    for (const auto& p : d.pins)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty("id", p.id);
        po->setProperty("role", roleName(p.role));
        po->setProperty("side", schematic::pinSideName(p.side));
        po->setProperty("order", p.order);
        if (p.reference.isNotEmpty())
            po->setProperty("reference", p.reference);
        pins.add(juce::var(po));
    }
    for (const auto& p : d.parameters)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty("name", p.name);
        po->setProperty("default", p.defaultValue);
        params.add(juce::var(po));
    }
    for (const auto& s : d.state)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty("name", s.name);
        so->setProperty("initial", s.initial);
        states.add(juce::var(so));
    }
    o->setProperty("pins", pins);
    o->setProperty("parameters", params);
    o->setProperty("state", states);
    o->setProperty("outputResistance", d.outputResistance);
    o->setProperty("program", d.programFile);
    return juce::var(o);
}

bool fromVar(const juce::var& v, Definition& d, juce::String& error)
{
    if (!v.isObject() || v.getProperty("kind", {}).toString() != "frust_component")
    {
        error = "Not a FRust component definition.";
        return false;
    }
    if ((int)v.getProperty("schemaVersion", 1) > 1)
    {
        error = "The component definition was saved by a newer Workbench (schema version "
              + v.getProperty("schemaVersion", 1).toString() + ").";
        return false;
    }
    d = {};
    d.name = v.getProperty("name", {}).toString();
    d.description = v.getProperty("description", {}).toString();
    d.outputResistance = (double)v.getProperty("outputResistance", 1.0);
    d.programFile = v.getProperty("program", {}).toString();
    if (const auto* pins = v.getProperty("pins", {}).getArray())
        for (const auto& p : *pins)
        {
            Pin pin;
            pin.id = p.getProperty("id", {}).toString();
            if (!parseRole(p.getProperty("role", "input").toString(), pin.role))
            {
                error = "Pin " + pin.id + " has an unknown role '" + p.getProperty("role", {}).toString() + "'.";
                return false;
            }
            schematic::PinSide side;
            pin.side = schematic::parsePinSide(p.getProperty("side", {}).toString(), side) ? side
                     : pin.role == PinRole::Input ? schematic::PinSide::Left : schematic::PinSide::Right;
            pin.order = p.hasProperty("order") ? (int)p.getProperty("order", -1) : -1;
            pin.reference = p.getProperty("reference", {}).toString();
            d.pins.push_back(pin);
        }
    if (const auto* params = v.getProperty("parameters", {}).getArray())
        for (const auto& p : *params)
            d.parameters.push_back({ p.getProperty("name", {}).toString(), (double)p.getProperty("default", 0.0) });
    if (const auto* states = v.getProperty("state", {}).getArray())
        for (const auto& s : *states)
            d.state.push_back({ s.getProperty("name", {}).toString(), (double)s.getProperty("initial", 0.0) });
    return true;
}

bool load(const juce::File& file, Definition& d, juce::String& error)
{
    if (!file.existsAsFile())
    {
        error = "No component definition " + file.getFullPathName();
        return false;
    }
    juce::var v;
    const auto parsed = juce::JSON::parse(file.loadFileAsString(), v);
    if (parsed.failed())
    {
        error = file.getFileName() + " is not valid JSON: " + parsed.getErrorMessage();
        return false;
    }
    return fromVar(v, d, error);
}

bool save(const juce::File& file, const Definition& d, juce::String& error)
{
    file.getParentDirectory().createDirectory();
    if (!file.replaceWithText(juce::JSON::toString(toVar(d), false)))
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Running a component's program
// ---------------------------------------------------------------------------

using ComputeFn = void (*)();

struct Library::Compiled
{
    Library* owner = nullptr;
    std::string key;     // what was compiled (definition + program)
    std::string unit;    // the engine unit holding the code
    ComputeFn compute = nullptr;

    ~Compiled()
    {
        if (owner != nullptr)
            owner->engine.unload(unit);
    }
};

namespace
{
class Device;

// The evaluation the running program belongs to (set around compute()).
struct Context
{
    Device* device = nullptr;
    double t = 0.0, h = 0.0;
    const std::vector<double>* pinVoltages = nullptr;
};
thread_local Context* current = nullptr;

// A pin's electrical model (see FrustComponent.h).
struct PinModel
{
    double volts = 0.0;   // V: driven voltage behind R
    double ohms = 0.0;    // R: <= 0 = open (no branch)
    double amps = 0.0;    // I: driven current into the circuit
};

class Device final : public circuit_sim::ProgrammableDevice
{
public:
    Device(std::shared_ptr<Library::Compiled> codeIn, const Definition& d, std::vector<double> params, juce::String nameIn)
        : code(std::move(codeIn)), parameters(std::move(params)), name(std::move(nameIn))
    {
        for (const auto& p : d.pins)
        {
            roles.push_back(p.role);
            references.push_back(p.reference.isNotEmpty() ? d.pinIndex(p.reference) : -1);
            PinModel m;
            if (p.role == PinRole::VoltageOutput)
                m.ohms = d.outputResistance;
            initialModels.push_back(m);
        }
        for (const auto& s : d.state)
            initialState.push_back(s.initial);
        reset();
    }

    void reset() override
    {
        committedState = initialState;
        nextState = initialState;
        committedModels = initialModels;
        trialModels = initialModels;
        committedCurrents.assign(roles.size(), 0.0);
        trialCurrents = committedCurrents;
    }

    bool evaluate(double t, double h, const std::vector<double>& pinVoltages, std::vector<double>& currents, std::string& error) override
    {
        // A trial: state reads see the committed state; writes go to
        // nextState; each pin starts from its last accepted model. Nothing
        // here changes what accept() has committed.
        nextState = committedState;
        trialModels = committedModels;
        Context context { this, t, h, &pinVoltages };
        current = &context;
        code->compute();
        current = nullptr;

        currents.assign(roles.size(), 0.0);
        for (size_t p = 0; p < roles.size(); ++p)
        {
            const auto& m = trialModels[p];
            const auto ref = references[p];
            const auto across = pinVoltages[p] - (ref >= 0 ? pinVoltages[(size_t)ref] : 0.0);
            const auto i = m.amps + (m.ohms > 0.0 ? (m.volts - across) / m.ohms : 0.0);
            if (!std::isfinite(i))
            {
                error = "pin " + std::to_string(p) + " (V = " + std::to_string(m.volts) + ", R = " + std::to_string(m.ohms)
                      + ", I = " + std::to_string(m.amps) + ") gives a non-finite current at t = " + std::to_string(t) + " s";
                return false;
            }
            currents[p] += i;
            if (ref >= 0)
                currents[(size_t)ref] -= i;
        }
        trialCurrents = currents;
        return true;
    }

    void accept(double) override
    {
        committedState = nextState;
        committedModels = trialModels;
        committedCurrents = trialCurrents;
    }

    // A whole copy of the device's state, committed and trial (the system
    // simulator's rollback, decision G1): every value is a double.
    bool saveState(std::vector<std::uint8_t>& out) const override
    {
        std::vector<double> values;
        auto put = [&values](const std::vector<double>& v) { values.push_back((double)v.size()); values.insert(values.end(), v.begin(), v.end()); };
        auto putModels = [&values](const std::vector<PinModel>& models) {
            values.push_back((double)models.size());
            for (const auto& m : models) { values.push_back(m.volts); values.push_back(m.ohms); values.push_back(m.amps); }
        };
        put(committedState);
        put(nextState);
        putModels(committedModels);
        putModels(trialModels);
        put(committedCurrents);
        put(trialCurrents);
        out.resize(values.size() * sizeof(double));
        if (!values.empty())
            std::memcpy(out.data(), values.data(), out.size());
        return true;
    }

    bool restoreState(const std::vector<std::uint8_t>& in) override
    {
        if (in.size() % sizeof(double) != 0)
            return false;
        std::vector<double> values(in.size() / sizeof(double));
        if (!values.empty())
            std::memcpy(values.data(), in.data(), in.size());
        size_t at = 0;
        auto get = [&](std::vector<double>& v, size_t expected) {
            if (at >= values.size() || (size_t)values[at] != expected || at + 1 + expected > values.size()) return false;
            v.assign(values.begin() + (std::ptrdiff_t)at + 1, values.begin() + (std::ptrdiff_t)(at + 1 + expected));
            at += 1 + expected;
            return true;
        };
        auto getModels = [&](std::vector<PinModel>& models) {
            const auto expected = roles.size();
            if (at >= values.size() || (size_t)values[at] != expected || at + 1 + 3 * expected > values.size()) return false;
            ++at;
            models.resize(expected);
            for (auto& m : models) { m.volts = values[at]; m.ohms = values[at + 1]; m.amps = values[at + 2]; at += 3; }
            return true;
        };
        std::vector<double> cs, ns, cc, tc;
        std::vector<PinModel> cm, tm;
        if (!get(cs, initialState.size()) || !get(ns, initialState.size()) || !getModels(cm) || !getModels(tm)
            || !get(cc, roles.size()) || !get(tc, roles.size()) || at != values.size())
            return false;
        committedState = std::move(cs);
        nextState = std::move(ns);
        committedModels = std::move(cm);
        trialModels = std::move(tm);
        committedCurrents = std::move(cc);
        trialCurrents = std::move(tc);
        return true;
    }

    // Host-function access (the program runs on this thread, in evaluate).
    double voltage(int pin) const
    {
        const auto& v = *current->pinVoltages;
        return pin >= 0 && pin < (int)v.size() ? v[(size_t)pin] : 0.0;
    }
    // The current the pin drove into the circuit at the last accepted step.
    double pinCurrent(int pin) const { return pin >= 0 && pin < (int)committedCurrents.size() ? committedCurrents[(size_t)pin] : 0.0; }
    double parameter(int index) const { return index >= 0 && index < (int)parameters.size() ? parameters[(size_t)index] : 0.0; }
    double state(int index) const { return index >= 0 && index < (int)committedState.size() ? committedState[(size_t)index] : 0.0; }
    void setState(int index, double value) { if (index >= 0 && index < (int)nextState.size()) nextState[(size_t)index] = value; }
    // pc_drive: the role's natural quantity (current for a current output, else voltage).
    void drive(int pin, double value)
    {
        if (pin < 0 || pin >= (int)trialModels.size()) return;
        if (roles[(size_t)pin] == PinRole::CurrentOutput)
            trialModels[(size_t)pin].amps = value;
        else
            trialModels[(size_t)pin].volts = value;
    }
    void driveCurrent(int pin, double amps) { if (pin >= 0 && pin < (int)trialModels.size()) trialModels[(size_t)pin].amps = amps; }
    void setResistance(int pin, double ohms)
    {
        if (pin >= 0 && pin < (int)trialModels.size())
            trialModels[(size_t)pin].ohms = std::isfinite(ohms) && ohms > 0.0 ? ohms : 0.0;
    }

private:
    std::shared_ptr<Library::Compiled> code;
    std::vector<PinRole> roles;
    std::vector<int> references;
    std::vector<double> parameters, initialState, committedState, nextState;
    std::vector<PinModel> initialModels, committedModels, trialModels;
    std::vector<double> committedCurrents, trialCurrents;
    juce::String name;
};

extern "C" double djehuti_pc_v(std::int64_t pin) { return current != nullptr ? current->device->voltage((int)pin) : 0.0; }
extern "C" double djehuti_pc_i(std::int64_t pin) { return current != nullptr ? current->device->pinCurrent((int)pin) : 0.0; }
extern "C" double djehuti_pc_time() { return current != nullptr ? current->t : 0.0; }
extern "C" double djehuti_pc_dt() { return current != nullptr ? current->h : 0.0; }
extern "C" double djehuti_pc_param(std::int64_t index) { return current != nullptr ? current->device->parameter((int)index) : 0.0; }
extern "C" double djehuti_pc_state(std::int64_t index) { return current != nullptr ? current->device->state((int)index) : 0.0; }
extern "C" double djehuti_pc_set_state(std::int64_t index, double value)
{
    if (current != nullptr) current->device->setState((int)index, value);
    return value;
}
extern "C" double djehuti_pc_drive(std::int64_t pin, double value)
{
    if (current != nullptr) current->device->drive((int)pin, value);
    return value;
}
extern "C" double djehuti_pc_drive_current(std::int64_t pin, double amps)
{
    if (current != nullptr) current->device->driveCurrent((int)pin, amps);
    return amps;
}
extern "C" double djehuti_pc_set_resistance(std::int64_t pin, double ohms)
{
    if (current != nullptr) current->device->setResistance((int)pin, ohms);
    return ohms;
}
}

const std::vector<std::string>& hostFunctionNames()
{
    static const std::vector<std::string> names { "djehuti_pc_v", "djehuti_pc_i", "djehuti_pc_time", "djehuti_pc_dt",
                                                  "djehuti_pc_param", "djehuti_pc_state", "djehuti_pc_set_state", "djehuti_pc_drive",
                                                  "djehuti_pc_drive_current", "djehuti_pc_set_resistance" };
    return names;
}

void registerHostFunctions()
{
    static std::once_flag once;
    std::call_once(once, [] {
        frust_plugin_register_host_function("djehuti_pc_v", reinterpret_cast<void*>(&djehuti_pc_v));
        frust_plugin_register_host_function("djehuti_pc_i", reinterpret_cast<void*>(&djehuti_pc_i));
        frust_plugin_register_host_function("djehuti_pc_time", reinterpret_cast<void*>(&djehuti_pc_time));
        frust_plugin_register_host_function("djehuti_pc_dt", reinterpret_cast<void*>(&djehuti_pc_dt));
        frust_plugin_register_host_function("djehuti_pc_param", reinterpret_cast<void*>(&djehuti_pc_param));
        frust_plugin_register_host_function("djehuti_pc_state", reinterpret_cast<void*>(&djehuti_pc_state));
        frust_plugin_register_host_function("djehuti_pc_set_state", reinterpret_cast<void*>(&djehuti_pc_set_state));
        frust_plugin_register_host_function("djehuti_pc_drive", reinterpret_cast<void*>(&djehuti_pc_drive));
        frust_plugin_register_host_function("djehuti_pc_drive_current", reinterpret_cast<void*>(&djehuti_pc_drive_current));
        frust_plugin_register_host_function("djehuti_pc_set_resistance", reinterpret_cast<void*>(&djehuti_pc_set_resistance));
    });
}

Library& Library::instance()
{
    // Never destroyed: simulations may still hold devices at exit.
    static auto* library = new Library();
    return *library;
}

Library::Library()
{
    registerHostFunctions();
}

bool Library::isCompiled(const juce::String& name, const std::string& key) const
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto found = compiled.find(name);
    return found != compiled.end() && found->second->key == key;
}

bool Library::compile(const Definition& d, const std::string& frustSource, const std::string& key, juce::String& error)
{
    if (isCompiled(d.name, key))
        return true;
    std::string unit;
    {
        std::lock_guard<std::mutex> lock(mutex);
        unit = "component_" + d.name.toStdString() + "_" + std::to_string(nextUnit++);
    }
    std::vector<std::string> hostFunctions = hostFunctionNames();
    const auto source = frust_engine::manifestLine(unit, "FRust programmable component " + d.name.toStdString(), hostFunctions) + frustSource;
    const auto result = engine.load(unit, source);
    if (!result.ok)
    {
        error = "Component " + d.name + " does not compile: " + juce::String(result.report()).trim();
        return false;
    }
    auto* fn = engine.function(unit, "compute");
    if (fn == nullptr)
    {
        engine.unload(unit);
        error = "Component " + d.name + ": the compiled program has no compute().";
        return false;
    }
    auto entry = std::make_shared<Compiled>();
    entry->owner = this;
    entry->key = key;
    entry->unit = unit;
    entry->compute = reinterpret_cast<ComputeFn>(fn);
    std::lock_guard<std::mutex> lock(mutex);
    compiled[d.name] = entry; // devices made from the old code keep it until they are released
    return true;
}

std::shared_ptr<circuit_sim::ProgrammableDevice> Library::makeDevice(const Definition& d,
                                                                     const std::map<juce::String, double>& values,
                                                                     const juce::String& instanceName, juce::String& error) const
{
    std::shared_ptr<Compiled> code;
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = compiled.find(d.name);
        if (found != compiled.end())
            code = found->second;
    }
    if (code == nullptr)
    {
        error = "Component " + d.name + " has not been compiled.";
        return nullptr;
    }
    std::vector<double> params;
    for (const auto& p : d.parameters)
    {
        const auto found = values.find(p.name);
        params.push_back(found != values.end() ? found->second : p.defaultValue);
    }
    return std::make_shared<Device>(code, d, std::move(params), instanceName);
}
}
