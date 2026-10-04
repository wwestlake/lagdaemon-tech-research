// Headless schematic layout preview and quality check.
//
// Runs the real SchematicLayout + SchematicRouter on reference circuits,
// renders each result to a PNG with the real symbol art, and prints
// readability metrics (crossings between different nets, collinear
// overlaps, wire segments cutting symbols). Usage:
//
//   schematic_preview.exe <output-folder>

#include <JuceHeader.h>

#include "../../Source/SchematicLayout.h"
#include "../../Source/SchematicRouter.h"
#include "../../Source/SchematicSymbols.h"

#include <crtdbg.h>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>

namespace
{
using P = juce::Point<float>;
using namespace schematic;

struct PartSpec
{
    juce::String symbolId;
    juce::String refdes;
    juce::String value;
    std::vector<juce::String> pinNets; // in symbol pin order; "" = unconnected
};

struct Circuit
{
    juce::String name;
    std::vector<PartSpec> parts;
};

std::vector<Circuit> referenceCircuits()
{
    std::vector<Circuit> circuits;

    circuits.push_back({ "rc_lowpass", {
        { "ac_voltage_source", "V1", "1 @ 1k", { "in", "0" } },
        { "resistor", "R1", "1k", { "in", "out" } },
        { "capacitor", "C1", "100n", { "out", "0" } },
        { "oscilloscope_2ch", "SCOPE1", "", { "in", "out", "0" } },
    } });

    circuits.push_back({ "push_pull", {
        { "ac_voltage_source", "V1", "0.25 @ 1k", { "vin", "0" } },
        { "capacitor", "C1", "10u", { "vin", "drive" } },
        { "resistor", "R1", "2.2k", { "+12V", "b1" } },
        { "diode", "D1", "1N4148", { "b1", "drive" } },
        { "diode", "D2", "1N4148", { "drive", "b2" } },
        { "resistor", "R2", "2.2k", { "b2", "-12V" } },
        { "npn", "Q1", "generic_npn", { "b1", "+12V", "e1" } },
        { "pnp", "Q2", "generic_pnp", { "b2", "e2", "-12V" } },
        { "resistor", "R3", "0.47", { "e1", "out" } },
        { "resistor", "R4", "0.47", { "out", "e2" } },
        { "resistor", "RL1", "8", { "out", "0" } },
        { "voltage_source", "V2", "12", { "+12V", "0" } },
        { "voltage_source", "V3", "12", { "0", "-12V" } },
        { "oscilloscope_2ch", "SCOPE1", "", { "vin", "out", "0" } },
    } });

    circuits.push_back({ "inverting_opamp", {
        { "ac_voltage_source", "V1", "0.1 @ 1k", { "in", "0" } },
        { "resistor", "R1", "10k", { "in", "inv" } },
        { "resistor", "R2", "100k", { "inv", "out" } },
        { "opamp_741", "U1", "uA741", { "0", "inv", "out", "+15V", "-15V" } },
        { "resistor", "RL1", "10k", { "out", "0" } },
        { "voltage_source", "V2", "15", { "+15V", "0" } },
        { "voltage_source", "V3", "15", { "0", "-15V" } },
    } });

    circuits.push_back({ "common_emitter", {
        { "ac_voltage_source", "V1", "10m @ 1k", { "sig", "0" } },
        { "capacitor", "C1", "10u", { "sig", "base" } },
        { "resistor", "R1", "47k", { "+12V", "base" } },
        { "resistor", "R2", "10k", { "base", "0" } },
        { "npn", "Q1", "2N3904", { "base", "col", "emit" } },
        { "resistor", "RC", "4.7k", { "+12V", "col" } },
        { "resistor", "RE", "1k", { "emit", "0" } },
        { "capacitor_polarized", "CE", "100u", { "emit", "0" } },
        { "capacitor", "C2", "10u", { "col", "out" } },
        { "resistor", "RL", "10k", { "out", "0" } },
        { "voltage_source", "V2", "12", { "+12V", "0" } },
    } });

    // The inside of an "Output Stage" sub-diagram: port bubbles stand in
    // for the block pins.
    circuits.push_back({ "output_stage_block", {
        { "block_port", "IN", "IN", { "b1" } },
        { "block_port", "IN2", "IN2", { "b2" } },
        { "block_port", "OUT", "OUT", { "out" } },
        { "npn", "Q1", "generic_npn", { "b1", "+12V", "e1" } },
        { "pnp", "Q2", "generic_pnp", { "b2", "e2", "-12V" } },
        { "resistor", "R3", "0.47", { "e1", "out" } },
        { "resistor", "R4", "0.47", { "out", "e2" } },
        { "resistor", "RL1", "8", { "out", "0" } },
    } });

    // A nested sheet: the emitter network inside the output stage.
    circuits.push_back({ "emitter_network_block", {
        { "block_port", "IN", "IN", { "e1" } },
        { "block_port", "IN2", "IN2", { "e2" } },
        { "block_port", "OUT", "OUT", { "out" } },
        { "resistor", "R3", "0.47", { "e1", "out" } },
        { "resistor", "R4", "0.47", { "out", "e2" } },
        { "resistor", "RL1", "8", { "out", "0" } },
    } });

    circuits.push_back({ "battery_led", {
        { "battery", "BT1", "9", { "+9V", "0" } },
        { "resistor", "R1", "470", { "+9V", "a" } },
        { "led", "D1", "red", { "a", "0" } },
    } });

    return circuits;
}

struct Placed
{
    juce::String symbolId;
    juce::String refdes;
    juce::String label;   // value text, or net name for power symbols
    P position;
    int rotation = 0;
};

struct Wire
{
    routing::Endpoint a;
    routing::Endpoint b;
    int net = -1;
};

routing::Obstacle obstacleFor(const Placed& placed)
{
    const auto symbol = symbolFor(placed.symbolId);
    routing::Obstacle obstacle;
    obstacle.bounds = rotateBounds(extentBounds(symbol), placed.rotation).translated(placed.position.x, placed.position.y);
    for (int p = 0; p < (int)symbol.pins.size(); ++p)
        obstacle.pins.push_back({ placed.position + rotateOffset(symbol.pins[(size_t)p].offset, placed.rotation),
                                  rotateOffset(pinLeadDirection(symbol, p), placed.rotation) });
    return obstacle;
}

struct Metrics
{
    int crossings = 0;
    int overlaps = 0;
    int bodyHits = 0;
    int bends = 0;
    int unrouted = 0;
    int offGrid = 0;
};

bool horizontal(P a, P b) { return std::abs(a.y - b.y) < 0.5f; }

Metrics measure(const std::vector<routing::Polyline>& routes, const std::vector<Wire>& wires,
                const std::vector<routing::Obstacle>& obstacles)
{
    Metrics m;
    struct Seg { P a, b; int net; int wire; };
    std::vector<Seg> segs;
    for (size_t w = 0; w < routes.size(); ++w)
    {
        const auto& r = routes[w];
        if (r.size() < 2) { ++m.unrouted; continue; }
        m.bends += (int)r.size() - 2;
        for (size_t i = 1; i < r.size(); ++i)
        {
            segs.push_back({ r[i - 1], r[i], wires[w].net, (int)w });
            for (auto q : { r[i - 1], r[i] })
                if (std::fmod(std::abs(q.x), gridSize) > 0.5f || std::fmod(std::abs(q.y), gridSize) > 0.5f)
                    ++m.offGrid;
        }
    }

    for (size_t i = 0; i < segs.size(); ++i)
    {
        for (const auto& o : obstacles)
        {
            const auto box = juce::Rectangle<float>(segs[i].a, segs[i].b).expanded(0.25f);
            if (box.intersects(o.bounds.reduced(1.0f)))
                ++m.bodyHits;
        }
        for (size_t j = i + 1; j < segs.size(); ++j)
        {
            const auto& s = segs[i];
            const auto& t = segs[j];
            if (s.net == t.net)
                continue;
            const bool sh = horizontal(s.a, s.b), th = horizontal(t.a, t.b);
            if (sh == th)
            {
                const bool sameLine = sh ? std::abs(s.a.y - t.a.y) < 0.5f : std::abs(s.a.x - t.a.x) < 0.5f;
                if (!sameLine) continue;
                const auto s0 = sh ? std::min(s.a.x, s.b.x) : std::min(s.a.y, s.b.y);
                const auto s1 = sh ? std::max(s.a.x, s.b.x) : std::max(s.a.y, s.b.y);
                const auto t0 = sh ? std::min(t.a.x, t.b.x) : std::min(t.a.y, t.b.y);
                const auto t1 = sh ? std::max(t.a.x, t.b.x) : std::max(t.a.y, t.b.y);
                if (std::min(s1, t1) - std::max(s0, t0) > 0.5f)
                    ++m.overlaps;
            }
            else
            {
                const auto& hseg = sh ? s : t;
                const auto& vseg = sh ? t : s;
                const auto x = vseg.a.x, y = hseg.a.y;
                if (x > std::min(hseg.a.x, hseg.b.x) + 0.5f && x < std::max(hseg.a.x, hseg.b.x) - 0.5f
                    && y > std::min(vseg.a.y, vseg.b.y) + 0.5f && y < std::max(vseg.a.y, vseg.b.y) - 0.5f)
                    ++m.crossings;
            }
        }
    }
    return m;
}

void render(const juce::File& file, const std::vector<Placed>& placed, const std::vector<routing::Polyline>& routes,
            const std::vector<Wire>& wires, const std::vector<P>& junctions, const std::vector<routing::Obstacle>& obstacles)
{
    juce::Rectangle<float> bounds;
    for (size_t i = 0; i < obstacles.size(); ++i)
        bounds = i == 0 ? obstacles[i].bounds : bounds.getUnion(obstacles[i].bounds);
    for (const auto& r : routes)
        for (const auto& p : r)
            bounds = bounds.getUnion(juce::Rectangle<float>(p.x - 1.0f, p.y - 1.0f, 2.0f, 2.0f));
    bounds = bounds.expanded(96.0f);

    juce::Image image(juce::Image::ARGB, (int)bounds.getWidth(), (int)bounds.getHeight(), true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff0e141a));
    g.addTransform(juce::AffineTransform::translation(-bounds.getX(), -bounds.getY()));

    g.setColour(juce::Colour(0xff1b2630));
    for (float x = std::floor(bounds.getX() / gridSize) * gridSize; x < bounds.getRight(); x += gridSize)
        for (float y = std::floor(bounds.getY() / gridSize) * gridSize; y < bounds.getBottom(); y += gridSize)
            g.fillRect(x - 0.5f, y - 0.5f, 1.5f, 1.5f);

    g.setColour(juce::Colour(0xfff4d35e));
    for (const auto& r : routes)
        for (size_t i = 1; i < r.size(); ++i)
            g.drawLine(r[i - 1].x, r[i - 1].y, r[i].x, r[i].y, 2.2f);

    // Junction dots: junctions with 3+ wires, pins with 2+ wires.
    std::map<std::pair<int, int>, int> pinDegree;
    std::map<int, int> junctionDegree;
    for (const auto& w : wires)
        for (const auto* e : { &w.a, &w.b })
        {
            if (e->isPin()) ++pinDegree[{ e->obstacle, e->pin }];
            if (e->isJunction()) ++junctionDegree[e->junction];
        }
    g.setColour(juce::Colour(0xffffc857));
    for (const auto& [j, degree] : junctionDegree)
        if (degree >= 3)
            g.fillEllipse(junctions[(size_t)j].x - 4.0f, junctions[(size_t)j].y - 4.0f, 8.0f, 8.0f);
    for (const auto& [pin, degree] : pinDegree)
        if (degree >= 2)
        {
            const auto p = obstacles[(size_t)pin.first].pins[(size_t)pin.second].position;
            g.fillEllipse(p.x - 4.0f, p.y - 4.0f, 8.0f, 8.0f);
        }

    for (const auto& part : placed)
    {
        const auto symbol = symbolFor(part.symbolId);
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(juce::degreesToRadians((float)part.rotation))
                           .translated(part.position.x, part.position.y));
        drawSymbolArt(g, symbol, {});
        g.restoreState();

        const auto box = rotateBounds(symbol.bounds, part.rotation).translated(part.position.x, part.position.y);
        g.setFont(juce::Font(12.0f));
        if (part.symbolId == "ground")
            continue;
        if (part.symbolId == "block_port")
        {
            const auto bubble = rotateBounds(portBubbleRect(), part.rotation).translated(part.position.x, part.position.y);
            g.setColour(juce::Colour(0xffffc857));
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            g.drawText(part.label, bubble.toNearestInt(), juce::Justification::centred);
            continue;
        }
        if (part.symbolId == "net_label")
        {
            g.setColour(juce::Colour(0xff78dcca));
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(part.label, juce::Rectangle<float>(part.position.x + 16.0f, part.position.y - 26.0f, 96.0f, 14.0f).toNearestInt(),
                       juce::Justification::centredLeft);
            continue;
        }
        if (part.symbolId == "power_port")
        {
            g.setColour(juce::Colour(0xffffc857));
            const auto down = normalizedRotation(part.rotation) == 180;
            g.drawText(part.label, juce::Rectangle<float>(box.getCentreX() - 40.0f, down ? box.getBottom() + 2.0f : box.getY() - 16.0f, 80.0f, 14.0f).toNearestInt(),
                       juce::Justification::centred);
            continue;
        }
        const auto labels = labelRectsFor(symbol, part.rotation);
        g.setColour(juce::Colour(0xff93a7b0));
        g.drawText(part.refdes, labels.refdes.translated(part.position.x, part.position.y).toNearestInt(), labels.justification);
        g.setColour(juce::Colour(0xffdce9ee));
        g.drawText(part.label, labels.value.translated(part.position.x, part.position.y).toNearestInt(), labels.justification);
    }

    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(image, out);
}

void runCircuit(const Circuit& circuit, const juce::File& outDir)
{
    // Nets: "0" ground, names starting with + or - are supplies.
    std::map<juce::String, int> netIndex;
    std::vector<layout::Net> nets;
    std::vector<layout::Part> parts;
    for (const auto& spec : circuit.parts)
    {
        layout::Part part;
        part.refdes = spec.refdes;
        part.symbol = symbolFor(spec.symbolId);
        if (spec.symbolId == "block_port")
        {
            const auto output = spec.refdes.startsWith("OUT");
            part.pinnedColumn = output ? 1 : -1;
            part.fixedRotation = output ? 180 : 0;
        }
        jassert(part.symbol.isValid());
        for (const auto& name : spec.pinNets)
        {
            if (name.isEmpty()) { part.pinNets.push_back(-1); continue; }
            if (netIndex.count(name) == 0)
            {
                layout::Net net;
                net.name = name;
                net.kind = name == "0" ? layout::NetKind::Ground
                         : (name.startsWith("+") || name.startsWith("-")) ? layout::NetKind::Supply
                         : layout::NetKind::Signal;
                netIndex[name] = (int)nets.size();
                nets.push_back(net);
            }
            part.pinNets.push_back(netIndex[name]);
        }
        parts.push_back(part);
    }

    const auto result = layout::layoutSchematic(parts, nets, gridSize);

    std::vector<Placed> placed;
    for (size_t i = 0; i < parts.size(); ++i)
        placed.push_back({ circuit.parts[i].symbolId, circuit.parts[i].refdes, circuit.parts[i].value,
                           result.positions[i], result.rotations[i] });
    std::vector<Wire> wires;
    std::map<int, std::vector<int>> labelsOnNet; // net -> placed index of circuit-side labels
    for (const auto& marker : result.markers)
    {
        const auto index = (int)placed.size();
        placed.push_back({ marker.symbolId, {}, marker.netName, marker.position, marker.rotation });
        if (marker.onNet)
            labelsOnNet[marker.net].push_back(index);
        else
            wires.push_back({ routing::Endpoint::forPin(index, 0), routing::Endpoint::forPin(marker.part, marker.pin),
                              parts[(size_t)marker.part].pinNets[(size_t)marker.pin] });
    }

    std::vector<routing::Obstacle> obstacles;
    for (const auto& p : placed)
        obstacles.push_back(obstacleFor(p));

    std::vector<routing::NetTerminals> signalNets;
    std::vector<int> signalNetIndex;
    for (int n = 0; n < (int)nets.size(); ++n)
    {
        if (nets[(size_t)n].kind != layout::NetKind::Signal)
            continue;
        routing::NetTerminals terminals;
        for (int i = 0; i < (int)parts.size(); ++i)
        {
            if (isInstrumentSymbol(parts[(size_t)i].symbol.id))
                continue; // instruments connect through their labels
            for (int p = 0; p < (int)parts[(size_t)i].pinNets.size(); ++p)
                if (parts[(size_t)i].pinNets[(size_t)p] == n)
                    terminals.terminals.push_back(routing::Endpoint::forPin(i, p));
        }
        for (int label : labelsOnNet[n])
            terminals.terminals.push_back(routing::Endpoint::forPin(label, 0));
        if (terminals.terminals.size() >= 2)
        {
            signalNets.push_back(terminals);
            signalNetIndex.push_back(n);
        }
    }

    std::vector<P> junctions;
    const auto trees = routing::routeNetTrees(obstacles, signalNets, gridSize);
    for (size_t t = 0; t < trees.size(); ++t)
    {
        if (juce::SystemStats::getEnvironmentVariable("SCHEMATIC_PREVIEW_DEBUG", {}).isNotEmpty())
            std::cout << "  net " << nets[(size_t)signalNetIndex[t]].name << ": terminals=" << signalNets[t].terminals.size()
                      << " edges=" << trees[t].edges.size() << " junctions=" << trees[t].junctions.size() << std::endl;
        const auto base = (int)junctions.size();
        for (const auto& j : trees[t].junctions)
            junctions.push_back(j);
        for (const auto& edge : trees[t].edges)
        {
            auto fix = [base](routing::Endpoint e) { if (e.isJunction()) e.junction += base; return e; };
            wires.push_back({ fix(edge.a), fix(edge.b), signalNetIndex[t] });
        }
    }

    routing::Problem problem;
    problem.obstacles = obstacles;
    problem.junctions = junctions;
    for (const auto& w : wires)
        problem.connections.push_back({ w.a, w.b, w.net });
    const auto routes = routing::routeConnections(problem, gridSize, &junctions);

    const auto m = measure(routes, wires, obstacles);
    std::cout << circuit.name << ": parts=" << parts.size() << " powerSymbols=" << result.markers.size()
              << " wires=" << wires.size() << " junctions=" << junctions.size()
              << " crossings=" << m.crossings << " overlaps=" << m.overlaps << " bodyHits=" << m.bodyHits
              << " bends=" << m.bends << " unrouted=" << m.unrouted << " offGrid=" << m.offGrid << std::endl;

    render(outDir.getChildFile(circuit.name + ".png"), placed, routes, wires, junctions, obstacles);
}
}

int main(int argc, char* argv[])
{
    // Headless: report assertion failures on stderr, never as dialogs.
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);

    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto outDir = juce::File(argc > 1 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    for (const auto& circuit : referenceCircuits())
        runCircuit(circuit, outDir);
    return 0;
}
