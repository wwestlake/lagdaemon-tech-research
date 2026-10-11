// FRust programmable components and the common external pin layout, without
// a window: real component programs (built in the Node Designer, compiled by
// the node compiler and the embedded FRust compiler) solved in circuits by
// the internal solver, with the expected values worked out by hand.

#include <JuceHeader.h>

#include "../../Source/Analytics.h"
#include "../../Source/CircuitSolver.h"
#include "../../Source/FrustComponent.h"
#include "../../Source/NodeDesignerPanel.h"
#include "../../Source/SchematicSymbols.h"
#include "../../Source/XyceBackend.h"

#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;
int checks = 0;

void check(bool ok, const juce::String& name, const juce::String& detail = {})
{
    ++checks;
    if (!ok) ++failures;
    std::printf("%s  %s%s\n", ok ? "PASS" : "FAIL", name.toRawUTF8(), ok || detail.isEmpty() ? "" : ("  -- " + detail).toRawUTF8());
    std::fflush(stdout);
}

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

// Builds and compiles a component's program; returns false with the error.
bool compileComponent(const frust_component::Definition& d, const std::function<void(NodeDesignerPanel&, juce::String&)>& build,
                      juce::String& error, juce::String* sourceOut = nullptr)
{
    error.clear();
    NodeDesignerPanel panel;
    panel.newGraph("node_graph", false);
    panel.setComponentContext(d.compilerContext());
    build(panel, error);
    if (error.isNotEmpty())
        return false;
    juce::String message;
    if (!panel.compileProgram(message))
    {
        error = message;
        return false;
    }
    if (sourceOut != nullptr)
        *sourceOut = panel.generatedProgramSource();
    return frust_component::Library::instance().compile(d, panel.generatedProgramSource().toStdString(),
                                                        d.name.toStdString() + std::to_string(juce::Random::getSystemRandom().nextInt()), error);
}

void add(NodeDesignerPanel& p, const juce::String& type, const juce::String& id, juce::String& error, const juce::String& text = {})
{
    juce::String e;
    if (p.addNodeOfType(type, 0, 0, id, e).isEmpty()) { error << "add " << id << ": " << e << "\n"; return; }
    if (text.isNotEmpty() && !p.setNodeParameter(id, "text", text, e)) error << "text " << id << ": " << e << "\n";
}

void wire(NodeDesignerPanel& p, const juce::String& from, const juce::String& fromPin, const juce::String& to, const juce::String& toPin, juce::String& error)
{
    juce::String e;
    if (p.connect(from, fromPin, to, toPin, e).isEmpty()) error << "wire " << from << "." << fromPin << " -> " << to << "." << toPin << ": " << e << "\n";
}

double voltageAt(const circuit_sim::TransientResult& r, circuit_sim::Node n, double t)
{
    for (size_t k = 0; k < r.time.size(); ++k)
        if (r.time[k] >= t - 1e-12)
            return r.voltages[k][(size_t)n];
    return r.voltages.empty() ? 0.0 : r.voltages.back()[(size_t)n];
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::String error;

    std::printf("-- common pin layout --\n");
    {
        // A classic two-sided block lays out exactly as before (pins two grid
        // steps apart, on the 24 px grid, body 144 wide).
        std::vector<schematic::BlockPort> legacy { { "IN", false }, { "OUT", true }, { "EN", false } };
        const auto old = schematic::blockSymbol(legacy);
        check(old.bounds.getWidth() == 144.0f && old.pins[0].offset == juce::Point<float>(-96.0f, -24.0f)
                  && old.pins[2].offset == juce::Point<float>(-96.0f, 24.0f) && old.pins[1].offset == juce::Point<float>(96.0f, 0.0f),
              "a saved left/right block keeps its old geometry");

        auto ports = legacy;
        schematic::placePort(ports, 1, schematic::PinSide::Top, 0);
        schematic::placePort(ports, 0, schematic::PinSide::Bottom, 0);
        schematic::placePort(ports, 2, schematic::PinSide::Bottom, 0); // EN before IN on the bottom
        const auto laid = schematic::blockSymbol(ports);
        bool onGrid = true;
        for (const auto& pin : laid.pins)
            onGrid &= std::fmod(std::abs(pin.offset.x), 24.0f) == 0.0f && std::fmod(std::abs(pin.offset.y), 24.0f) == 0.0f;
        check(laid.pins.size() == 3 && laid.pins[0].name == "IN" && laid.pins[1].name == "OUT" && laid.pins[2].name == "EN",
              "pin index still names the same terminal after rearranging (IN, OUT, EN)");
        check(laid.pins[1].offset.y < laid.bounds.getY() && laid.pins[0].offset.y > laid.bounds.getBottom()
                  && laid.pins[2].offset.y > laid.bounds.getBottom(),
              "OUT is above the body; IN and EN below it");
        check(laid.pins[2].offset.x < laid.pins[0].offset.x, "EN comes before IN along the bottom (order 0 is leftmost)");
        check(onGrid, "every pin end is on the 24 px grid");
        for (int k = 0; k < 3; ++k)
        {
            const auto d = schematic::pinLeadDirection(laid, k);
            check((k == 1 && d.y < 0.0f) || (k != 1 && d.y > 0.0f), "pin " + laid.pins[(size_t)k].name + " leads away from its side");
        }
        std::vector<schematic::BlockPort> many;
        for (int k = 0; k < 6; ++k) many.push_back({ "SUPPLY_RAIL_" + juce::String(k), schematic::PinSide::Top });
        const auto wide = schematic::blockSymbol(many);
        check(wide.bounds.getWidth() >= 6 * 48.0f - 24.0f, "six top pins widen the body to keep them two grid steps apart");
    }

    std::printf("-- a component drives a circuit --\n");
    frust_component::Definition amp;
    amp.name = "Doubler";
    amp.pins = { { "IN", frust_component::PinRole::Input }, { "OUT", frust_component::PinRole::VoltageOutput } };
    amp.parameters = { { "gain", 2.0 } };
    amp.outputResistance = 1.0;
    juce::String source;
    const bool ampOk = compileComponent(amp, [](NodeDesignerPanel& p, juce::String& e) {
        add(p, "pc_pin_voltage", "vin", e, "IN");
        add(p, "pc_parameter", "k", e, "gain");
        add(p, "mul", "scaled", e);
        add(p, "pc_drive", "drive_out", e, "OUT");
        wire(p, "vin", "volts", "scaled", "a", e);
        wire(p, "k", "value", "scaled", "b", e);
        wire(p, "scaled", "product", "drive_out", "value", e);
    }, error, &source);
    check(ampOk, "a component program (OUT = gain * V(IN)) compiles", error + "\n" + source);
    if (ampOk)
    {
        // 1.5 V into IN; OUT drives 1 k to ground through its 1 ohm output
        // resistance: V(OUT) = 2 * 1.5 * 1000 / 1001 = 2.997 V.
        circuit_sim::Circuit c;
        const auto in = c.addNode(), out = c.addNode();
        circuit_sim::Waveform dc;
        dc.offset = 1.5;
        c.addVoltageSource("V1", in, 0, dc);
        c.addResistor("RL", out, 0, 1000.0);
        auto device = frust_component::Library::instance().makeDevice(amp, {}, "U1", error);
        c.addProgrammable("U1", { in, out }, device);
        const auto op = circuit_sim::solveOperatingPoint(c);
        check(op.ok && near(op.voltages[(size_t)out], 3.0 * 1000.0 / 1001.0, 1e-6),
              "operating point: V(OUT) = 2.997 V (the program's output loaded by the circuit)",
              juce::String(op.error) + " V(OUT)=" + juce::String(op.ok ? op.voltages[(size_t)out] : 0.0, 6));

        // The instance's own gain: 3 -> V(OUT) = 4.4955 V.
        auto device3 = frust_component::Library::instance().makeDevice(amp, { { "gain", 3.0 } }, "U2", error);
        circuit_sim::Circuit c3;
        const auto in3 = c3.addNode(), out3 = c3.addNode();
        c3.addVoltageSource("V1", in3, 0, dc);
        c3.addResistor("RL", out3, 0, 1000.0);
        c3.addProgrammable("U2", { in3, out3 }, device3);
        const auto op3 = circuit_sim::solveOperatingPoint(c3);
        check(op3.ok && near(op3.voltages[(size_t)out3], 4.5 * 1000.0 / 1001.0, 1e-6), "an instance's parameter value (gain 3) is used",
              juce::String(op3.ok ? op3.voltages[(size_t)out3] : 0.0, 6));
    }

    std::printf("-- state: a toggle flip-flop, two instances --\n");
    frust_component::Definition toggle;
    toggle.name = "Toggle";
    toggle.pins = { { "CLK", frust_component::PinRole::Input }, { "Q", frust_component::PinRole::VoltageOutput } };
    toggle.parameters = { { "threshold", 2.5 }, { "high", 5.0 } };
    toggle.state = { { "q", 0.0 }, { "was_high", 0.0 } };
    toggle.outputResistance = 1.0;
    const bool toggleOk = compileComponent(toggle, [](NodeDesignerPanel& p, juce::String& e) {
        // high = V(CLK) > threshold (1/0); rising = high - was_high > 0.5;
        // q' = rising ? high_level - q : q. State: q, was_high.
        add(p, "pc_pin_voltage", "clk", e, "CLK");
        add(p, "pc_parameter", "th", e, "threshold");
        add(p, "pc_parameter", "hi_level", e, "high");
        add(p, "gt", "is_high", e);
        add(p, "literal_f64", "one", e, "1.0");
        add(p, "literal_f64", "zero", e, "0.0");
        add(p, "if", "hi_num", e);
        add(p, "pc_state_get", "was", e, "was_high");
        add(p, "sub", "edge", e);
        add(p, "literal_f64", "half", e, "0.5");
        add(p, "gt", "rising", e);
        add(p, "pc_state_get", "q", e, "q");
        add(p, "sub", "flipped", e);
        add(p, "if", "q_next", e);
        add(p, "pc_state_set", "keep_q", e, "q");
        add(p, "pc_state_set", "keep_was", e, "was_high");
        add(p, "pc_drive", "drive_out", e, "Q");
        wire(p, "clk", "volts", "is_high", "a", e);
        wire(p, "th", "value", "is_high", "b", e);
        wire(p, "is_high", "is greater", "hi_num", "cond", e);
        wire(p, "one", "value", "hi_num", "then", e);
        wire(p, "zero", "value", "hi_num", "else", e);
        wire(p, "hi_num", "value", "edge", "a", e);
        wire(p, "was", "value", "edge", "b", e);
        wire(p, "edge", "difference", "rising", "a", e);
        wire(p, "half", "value", "rising", "b", e);
        wire(p, "hi_level", "value", "flipped", "a", e);
        wire(p, "q", "value", "flipped", "b", e);
        wire(p, "rising", "is greater", "q_next", "cond", e);
        wire(p, "flipped", "difference", "q_next", "then", e);
        wire(p, "q", "value", "q_next", "else", e);
        wire(p, "q_next", "value", "keep_q", "value", e);
        wire(p, "hi_num", "value", "keep_was", "value", e);
        wire(p, "q_next", "value", "drive_out", "value", e);
    }, error, &source);
    check(toggleOk, "a stateful program (toggle on each rising clock edge) compiles", error + "\n" + source);
    if (toggleOk)
    {
        // Two toggles: A clocked every 1 ms, B every 2 ms (pulses 0 -> 5 V,
        // rising at 0.1 ms + n * period). Each toggles on its own rising edges.
        circuit_sim::Circuit c;
        const auto clkA = c.addNode(), clkB = c.addNode(), qA = c.addNode(), qB = c.addNode();
        auto clock = [](double period) {
            circuit_sim::Waveform w;
            w.kind = circuit_sim::Waveform::Kind::Pulse;
            w.offset = 0.0; w.pulsed = 5.0; w.delay = 0.1e-3; w.rise = 1e-6; w.fall = 1e-6; w.width = period / 2; w.period = period;
            return w;
        };
        c.addVoltageSource("VA", clkA, 0, clock(1e-3));
        c.addVoltageSource("VB", clkB, 0, clock(2e-3));
        c.addResistor("RA", qA, 0, 10e3);
        c.addResistor("RB", qB, 0, 10e3);
        c.addProgrammable("UA", { clkA, qA }, frust_component::Library::instance().makeDevice(toggle, {}, "UA", error));
        c.addProgrammable("UB", { clkB, qB }, frust_component::Library::instance().makeDevice(toggle, {}, "UB", error));
        const auto r = circuit_sim::solveTransient(c, 4.5e-3, 10e-6);
        check(r.ok, "transient with two toggles solves", juce::String(r.error));
        if (r.ok)
        {
            const double level = 5.0 * 10e3 / (10e3 + 1.0);
            // A rises at 0.1, 1.1, 2.1, 3.1, 4.1 ms -> Q_A = 5, 0, 5, 0, 5.
            const double expectA[] = { 0.0, level, 0.0, level, 0.0, level };
            const double atA[] = { 0.05e-3, 0.6e-3, 1.6e-3, 2.6e-3, 3.6e-3, 4.4e-3 };
            bool okA = true;
            juce::String seenA;
            for (int k = 0; k < 6; ++k)
            {
                const auto v = voltageAt(r, qA, atA[k]);
                seenA << juce::String(v, 3) << " ";
                okA &= near(v, expectA[k], 1e-3);
            }
            check(okA, "Q_A toggles once per rising edge: 0, 5, 0, 5, 0, 5 V (Newton's trial evaluations did not advance it)", seenA);
            // B rises at 0.1, 2.1, 4.1 ms -> Q_B = 5, 0, 5.
            const double atB[] = { 0.05e-3, 1.6e-3, 2.6e-3, 4.4e-3 };
            const double expectB[] = { 0.0, level, 0.0, level };
            bool okB = true;
            juce::String seenB;
            for (int k = 0; k < 4; ++k)
            {
                const auto v = voltageAt(r, qB, atB[k]);
                seenB << juce::String(v, 3) << " ";
                okB &= near(v, expectB[k], 1e-3);
            }
            check(okB, "Q_B (half the clock rate) keeps its own state: 0, 5, 0, 5 V", seenB);
        }

        // The state protocol directly: trial evaluations never change the
        // committed state; only accept() does; reset() restores it.
        auto device = frust_component::Library::instance().makeDevice(toggle, {}, "UT", error);
        std::vector<double> outputs;
        std::string evalError;
        device->reset();
        for (int k = 0; k < 5; ++k)
            device->evaluate(1e-3, 1e-6, { 5.0, 0.0 }, outputs, evalError); // repeated trials, CLK high
        check(near(outputs[1], 5.0, 1e-12), "a trial at a rising edge drives Q to 5 V (5 A into a 0 V pin through its 1 ohm)", juce::String(outputs[1]));
        device->evaluate(1e-3, 1e-6, { 5.0, 0.0 }, outputs, evalError);
        check(near(outputs[1], 5.0, 1e-12), "repeating the same trial (no accept) gives the same Q: the state did not advance");
        device->evaluate(1e-3, 1e-6, { 0.0, 0.0 }, outputs, evalError); // a rejected step's trial
        device->evaluate(1e-3, 1e-6, { 5.0, 0.0 }, outputs, evalError);
        device->accept(1e-3);
        device->evaluate(2e-3, 1e-6, { 5.0, 0.0 }, outputs, evalError);
        check(near(outputs[1], 5.0, 1e-12), "after accepting the edge, CLK still high: no second toggle (Q stays 5 V)");
        device->evaluate(2e-3, 1e-6, { 0.0, 0.0 }, outputs, evalError);
        device->accept(2e-3);
        device->evaluate(3e-3, 1e-6, { 5.0, 0.0 }, outputs, evalError);
        check(near(outputs[1], 0.0, 1e-12), "the next accepted rising edge toggles Q back to 0 V");
        device->reset();
        device->evaluate(0.0, 0.0, { 0.0, 0.0 }, outputs, evalError);
        check(near(outputs[1], 0.0, 1e-12), "reset() restores the initial state");

        // State snapshots (system simulator rollback): a saved state, then an
        // accepted edge, then restoring: the edge is gone.
        std::vector<std::uint8_t> snapshot;
        const bool saved = device->saveState(snapshot);
        device->evaluate(1e-3, 1e-6, { 5.0, 0.0 }, outputs, evalError);
        device->accept(1e-3); // Q -> 5 V committed
        const bool restored = device->restoreState(snapshot);
        device->evaluate(2e-3, 1e-6, { 0.0, 0.0 }, outputs, evalError);
        check(saved && restored && near(outputs[1], 0.0, 1e-12), "restoreState() takes back an accepted toggle (Q 0 V again)", juce::String(outputs[1]));
        check(!device->restoreState(std::vector<std::uint8_t>(5, 0)), "a malformed snapshot is refused");

        // The resumable stepper with the two toggles: save at 1.6 ms, run to
        // 4.5 ms, restore, run again: the same waveform bit for bit, because
        // the devices' toggled state goes back with the solver's.
        circuit_sim::TransientStepper stepper(c);
        const auto grid = circuit_sim::TransientStepper::timeGrid(c, 4.5e-3, 10e-6);
        bool ok = stepper.init();
        size_t n = 0;
        for (; n < grid.size() && grid[n] <= 1.6e-3 && ok; ++n)
            ok = stepper.step(grid[n]);
        circuit_sim::TransientStepper::State mid;
        ok = ok && stepper.saveState(mid);
        auto runRest = [&](std::vector<double>& wave) {
            for (size_t k = n; k < grid.size() && ok; ++k)
            {
                ok = stepper.step(grid[k]);
                wave.push_back(stepper.voltage(qA));
                wave.push_back(stepper.voltage(qB));
            }
        };
        std::vector<double> first, second;
        runRest(first);
        ok = ok && stepper.restoreState(mid);
        runRest(second);
        const double level = 5.0 * 10e3 / (10e3 + 1.0);
        check(ok && first == second && first.size() >= 2 && near(first[first.size() - 2], level, 1e-3) && near(first.back(), level, 1e-3),
              "stepper save at 1.6 ms, restore, re-step: identical waveform with the toggles' state restored (Q_A, Q_B end at 5 V)",
              juce::String(stepper.error()) + " samples " + juce::String((int)first.size()));
    }

    std::printf("-- pin electrical models set by the program --\n");
    {
        circuit_sim::Waveform ten;
        ten.offset = 10.0;
        // Input impedance: the program gives IN a resistance (parameter rin)
        // to ground. 10 V through 1 k into IN with rin = 1 k: V(IN) = 5 V.
        frust_component::Definition load;
        load.name = "Load";
        load.pins = { { "IN", frust_component::PinRole::Input } };
        load.parameters = { { "rin", 1000.0 } };
        const bool loadOk = compileComponent(load, [](NodeDesignerPanel& p, juce::String& e) {
            add(p, "pc_parameter", "r", e, "rin");
            add(p, "pc_set_resistance", "zin", e, "IN");
            wire(p, "r", "value", "zin", "ohms", e);
        }, error);
        check(loadOk, "an input-impedance program compiles", error);
        if (loadOk)
        {
            circuit_sim::Circuit c;
            const auto src = c.addNode(), in = c.addNode();
            c.addVoltageSource("V1", src, 0, ten);
            c.addResistor("RS", src, in, 1000.0);
            c.addProgrammable("U1", { in }, frust_component::Library::instance().makeDevice(load, {}, "U1", error));
            const auto op = circuit_sim::solveOperatingPoint(c);
            check(op.ok && near(op.voltages[(size_t)in], 5.0, 1e-6), "programmed input impedance 1 k loads the source: V(IN) = 5.000 V",
                  juce::String(op.ok ? op.voltages[(size_t)in] : 0.0, 6) + " " + juce::String(op.error));
            // A pin with no programmed resistance draws nothing: V(IN) = 10 V.
            frust_component::Definition open = load;
            open.name = "OpenIn";
            juce::String e2;
            compileComponent(open, [](NodeDesignerPanel& p, juce::String& e) { add(p, "pc_pin_voltage", "v", e, "IN"); }, e2);
            circuit_sim::Circuit c2;
            const auto s2 = c2.addNode(), i2 = c2.addNode();
            c2.addVoltageSource("V1", s2, 0, ten);
            c2.addResistor("RS", s2, i2, 1000.0);
            c2.addResistor("RX", i2, 0, 1e9); // the meter-like path the bare input otherwise lacks
            c2.addProgrammable("U1", { i2 }, frust_component::Library::instance().makeDevice(open, {}, "U1", e2));
            const auto op2 = circuit_sim::solveOperatingPoint(c2);
            check(op2.ok && near(op2.voltages[(size_t)i2], 10.0, 1e-4), "an input with no programmed load draws no current: V(IN) = 10 V",
                  juce::String(op2.ok ? op2.voltages[(size_t)i2] : 0.0, 6));
        }

        // Output impedance set by the program (100 ohm) behind the doubled
        // input: 2 * 1.5 V into 100 ohm load -> V(OUT) = 3 * 100 / 200 = 1.5 V.
        frust_component::Definition soft;
        soft.name = "SoftDoubler";
        soft.pins = { { "IN", frust_component::PinRole::Input }, { "OUT", frust_component::PinRole::VoltageOutput } };
        const bool softOk = compileComponent(soft, [](NodeDesignerPanel& p, juce::String& e) {
            add(p, "pc_pin_voltage", "vin", e, "IN");
            add(p, "literal_f64", "two", e, "2.0");
            add(p, "mul", "v2", e);
            add(p, "pc_drive", "drive_out", e, "OUT");
            add(p, "literal_f64", "rout", e, "100");
            add(p, "pc_set_resistance", "zout", e, "OUT");
            wire(p, "vin", "volts", "v2", "a", e);
            wire(p, "two", "value", "v2", "b", e);
            wire(p, "v2", "product", "drive_out", "value", e);
            wire(p, "rout", "value", "zout", "ohms", e);
        }, error);
        check(softOk, "an output-impedance program compiles", error);
        if (softOk)
        {
            circuit_sim::Circuit c;
            const auto in = c.addNode(), out = c.addNode();
            circuit_sim::Waveform v15;
            v15.offset = 1.5;
            c.addVoltageSource("V1", in, 0, v15);
            c.addResistor("RL", out, 0, 100.0);
            c.addProgrammable("U1", { in, out }, frust_component::Library::instance().makeDevice(soft, {}, "U1", error));
            const auto op = circuit_sim::solveOperatingPoint(c);
            check(op.ok && near(op.voltages[(size_t)out], 1.5, 1e-6), "programmed 100 ohm output impedance into 100 ohm: V(OUT) = 1.500 V",
                  juce::String(op.ok ? op.voltages[(size_t)out] : 0.0, 6));
        }

        // A current output: 1 mA out of a node (I = -1 mA into the circuit)
        // fed by 10 V through 1 k: V = 10 - 1 = 9 V.
        frust_component::Definition sink;
        sink.name = "Sink";
        sink.pins = { { "P", frust_component::PinRole::CurrentOutput } };
        const bool sinkOk = compileComponent(sink, [](NodeDesignerPanel& p, juce::String& e) {
            add(p, "literal_f64", "i", e, "-0.001");
            add(p, "pc_drive", "d", e, "P");
            wire(p, "i", "value", "d", "value", e);
        }, error);
        if (sinkOk)
        {
            circuit_sim::Circuit c;
            const auto src = c.addNode(), node = c.addNode();
            c.addVoltageSource("V1", src, 0, ten);
            c.addResistor("RS", src, node, 1000.0);
            c.addProgrammable("U1", { node }, frust_component::Library::instance().makeDevice(sink, {}, "U1", error));
            const auto op = circuit_sim::solveOperatingPoint(c);
            check(op.ok && near(op.voltages[(size_t)node], 9.0, 1e-6), "a programmed 1 mA current sink pulls the node to 9.000 V",
                  juce::String(op.ok ? op.voltages[(size_t)node] : 0.0, 6));
        }
        else
            check(false, "a current-output program compiles", error);

        // Two terminals: A's branch returns through B (a floating
        // programmable resistor, R = parameter). 10 V - 1 k - [A R B] - 1 k - GND
        // with R = 2 k: I = 10 / 4 k = 2.5 mA, V(A) = 7.5 V, V(B) = 2.5 V.
        frust_component::Definition res;
        res.name = "ProgResistor";
        res.pins = { { "A", frust_component::PinRole::Input, "B" }, { "B", frust_component::PinRole::Input } };
        res.parameters = { { "ohms", 2000.0 } };
        const bool resOk = compileComponent(res, [](NodeDesignerPanel& p, juce::String& e) {
            add(p, "pc_parameter", "r", e, "ohms");
            add(p, "pc_set_resistance", "z", e, "A");
            wire(p, "r", "value", "z", "ohms", e);
        }, error);
        check(resOk && frust_component::problemsWith(res).isEmpty(), "a two-terminal (floating) programmable resistor compiles", error);
        if (resOk)
        {
            circuit_sim::Circuit c;
            const auto src = c.addNode(), a = c.addNode(), b = c.addNode();
            c.addVoltageSource("V1", src, 0, ten);
            c.addResistor("R1", src, a, 1000.0);
            c.addResistor("R2", b, 0, 1000.0);
            c.addProgrammable("U1", { a, b }, frust_component::Library::instance().makeDevice(res, {}, "U1", error));
            const auto op = circuit_sim::solveOperatingPoint(c);
            check(op.ok && near(op.voltages[(size_t)a], 7.5, 1e-6) && near(op.voltages[(size_t)b], 2.5, 1e-6),
                  "current enters at A and returns at B: V(A) = 7.500 V, V(B) = 2.500 V",
                  juce::String(op.ok ? op.voltages[(size_t)a] : 0.0, 6) + " / " + juce::String(op.ok ? op.voltages[(size_t)b] : 0.0, 6));
        }

        // Behaviour that depends on the pin voltage, solved by Newton: a
        // square-law load I = k V^2 drawn from IN (k = 1 mA/V^2), fed by
        // 10 V through 1 k. 1000 * 1e-3 V^2 + V - 10 = 0 -> V = (-1 + sqrt(41)) / 2 = 2.7016 V.
        frust_component::Definition square;
        square.name = "SquareLaw";
        square.pins = { { "IN", frust_component::PinRole::CurrentOutput } };
        const bool sqOk = compileComponent(square, [](NodeDesignerPanel& p, juce::String& e) {
            add(p, "pc_pin_voltage", "v", e, "IN");
            add(p, "mul", "v2", e);
            add(p, "literal_f64", "k", e, "-0.001");
            add(p, "mul", "i", e);
            add(p, "pc_drive_current", "d", e, "IN");
            wire(p, "v", "volts", "v2", "a", e);
            wire(p, "v", "volts", "v2", "b", e);
            wire(p, "v2", "product", "i", "a", e);
            wire(p, "k", "value", "i", "b", e);
            wire(p, "i", "product", "d", "amps", e);
        }, error);
        check(sqOk, "a voltage-dependent (square-law) current program compiles", error);
        if (sqOk)
        {
            circuit_sim::Circuit c;
            const auto src = c.addNode(), node = c.addNode();
            c.addVoltageSource("V1", src, 0, ten);
            c.addResistor("RS", src, node, 1000.0);
            c.addProgrammable("U1", { node }, frust_component::Library::instance().makeDevice(square, {}, "U1", error));
            const auto op = circuit_sim::solveOperatingPoint(c);
            const double expected = (-1.0 + std::sqrt(41.0)) / 2.0;
            check(op.ok && near(op.voltages[(size_t)node], expected, 1e-5),
                  "Newton solves the voltage-dependent pin with the circuit: V = 2.7016 V (in " + juce::String(op.iterations) + " iterations)",
                  juce::String(op.ok ? op.voltages[(size_t)node] : 0.0, 6) + " " + juce::String(op.error));
        }
    }

    std::printf("-- Xyce --\n");
    {
        analytics::Netlist n;
        const auto a = n.circuit.addNode();
        circuit_sim::Waveform dc;
        dc.offset = 1.0;
        n.circuit.addVoltageSource("V1", a, 0, dc);
        n.circuit.addProgrammable("U7", { a }, frust_component::Library::instance().makeDevice(amp, {}, "U7", error));
        const auto problem = xyce_backend::netlistProblem(n);
        check(problem.contains("U7") && problem.contains("programmable") && problem.contains("internal solver"),
              "Xyce refuses a circuit with a programmable component, naming it", problem);
    }

    std::printf("-- diagnostics --\n");
    {
        frust_component::Definition bad;
        bad.name = "Bad";
        bad.pins = { { "IN", frust_component::PinRole::Input } };
        juce::String e2;
        const bool compiled = compileComponent(bad, [](NodeDesignerPanel& p, juce::String& e) { add(p, "pc_pin_voltage", "v", e, "NOPE"); }, e2);
        check(!compiled && e2.contains("no pin 'NOPE'") && e2.contains("IN"), "a node naming a pin the component lacks is a compile error naming the pins", e2);
        check(frust_component::problemsWith(bad).isEmpty(), "a valid definition has no problems");
        bad.pins.push_back({ "IN", frust_component::PinRole::Input });
        check(frust_component::problemsWith(bad).contains("Two pins"), "duplicate pin names are refused", frust_component::problemsWith(bad));
    }

    std::printf("%d checks, %s\n", checks, failures == 0 ? "ALL PASSED" : (juce::String(failures) + " FAILED").toRawUTF8());
    return failures == 0 ? 0 : 1;
}
