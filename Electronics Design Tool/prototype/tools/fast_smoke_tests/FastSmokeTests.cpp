// Fast Integration Smoke Tests
// Super-fast multi-subsystem sanity check:
// - Control Panel model serialization
// - Machine Design graph instantiation
// - Circuit Solver DC nodal check
// - Schematic symbols registry
// Runs in < 100 milliseconds.

#include "ControlPanelModel.h"
#include "MachineModel.h"
#include "CircuitSolver.h"
#include "SchematicSymbols.h"

#include <JuceHeader.h>
#include <iostream>
#include <cmath>

namespace
{
struct Runner
{
    int checks = 0;
    int failures = 0;

    void check(bool condition, const std::string& label)
    {
        ++checks;
        if (condition)
            std::cout << "PASS  " << label << "\n";
        else
        {
            ++failures;
            std::cout << "FAIL  " << label << "\n";
        }
    }
};
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    Runner r;
    std::cout << "=== Fast Integration Smoke Tests ===\n";

    // 1. Control Panel Model
    sim_panel::ControlPanelLayout layout;
    layout.ensureDefaultPanel();
    r.check(layout.hasPanel("main"), "ControlPanelLayout creates default main panel");
    const auto cpJson = layout.toJson();
    r.check(cpJson.isObject(), "ControlPanelLayout serializes to JSON");
    const auto cpReload = sim_panel::ControlPanelLayout::fromJson(cpJson);
    r.check(cpReload.hasPanel("main"), "ControlPanelLayout round-trips through JSON");

    // 2. Machine Design Model
    machine_builder::MachineDesign machine;
    machine.name = "SmokeTestMachine";
    r.check(machine.assembly.instances.empty(), "MachineDesign initializes with clean assembly");
    const auto mJson = machine.toJson();
    r.check(mJson.isObject(), "MachineDesign serializes to JSON");
    const auto mReload = machine_builder::MachineDesign::fromJson(mJson);
    r.check(mReload.name == "SmokeTestMachine", "Preserves machine name through JSON");

    // 3. Circuit Solver Simple DC Check
    circuit_sim::Circuit circuit;
    const auto n1 = circuit.addNode();
    const auto n2 = circuit.addNode();
    circuit.addVoltageSource("V1", n1, 0, circuit_sim::Waveform{ circuit_sim::Waveform::Kind::Dc, 10.0 });
    circuit.addResistor("R1", n1, n2, 1000.0);
    circuit.addResistor("R2", n2, 0, 1000.0);
    const auto op = circuit_sim::solveOperatingPoint(circuit);
    r.check(op.ok, "Circuit solver converges on DC voltage divider");
    r.check(op.voltages.size() > static_cast<size_t>(n2) && std::abs(op.voltages[n2] - 5.0) < 1e-4,
            "Voltage divider node voltage equals 5.0V");

    // 4. Schematic Symbols Registry
    r.check(!schematic::supportedSymbolIds().isEmpty(), "Schematic symbols registry has supported symbols");

    std::cout << "--- Finished with " << r.checks << " checks, " << r.failures << " failures ---\n";
    return r.failures == 0 ? 0 : 1;
}
