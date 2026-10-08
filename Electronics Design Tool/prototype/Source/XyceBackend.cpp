#include "XyceBackend.h"

#include "SpiceLibrary.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace xyce_backend
{
namespace
{
using circuit_sim::Element;
using ElementType = Element::Type;

juce::String quotePath(const juce::File& f)
{
    return "\"" + f.getFullPathName().replace("\"", "\\\"") + "\"";
}

juce::String sanitize(juce::String s)
{
    s = s.trim();
    if (s.isEmpty())
        return "N";
    juce::String out;
    for (auto c : s)
    {
        if (c == '+')
            out << "P";
        else if (c == '-')
            out << "N";
        else
            out << (juce::CharacterFunctions::isLetterOrDigit(c) || c == '_' || c == '$' ? juce::String::charToString(c) : "_");
    }
    if (out[0] >= '0' && out[0] <= '9')
        out = "N" + out;
    return out;
}

juce::String value(double v)
{
    return juce::String(v, 12);
}

juce::String nodeName(const analytics::Netlist& n, circuit_sim::Node node)
{
    if (node == 0)
        return "0";
    for (const auto& net : n.nets)
        if (net.node == node)
            return sanitize(net.name);
    return "N" + juce::String(node);
}

juce::String sourceName(const Element& e, size_t index)
{
    juce::String name = sanitize(e.name);
    const auto prefix = e.type == ElementType::CurrentSource ? "I" : "V";
    if (!name.startsWithIgnoreCase(prefix))
        name = prefix + name;
    if (name == prefix)
        name << (int)index + 1;
    return name;
}

juce::String elementName(const Element& e, size_t index, const juce::String& prefix)
{
    auto name = sanitize(e.name);
    if (!name.startsWithIgnoreCase(prefix))
        name = prefix + name;
    if (name == prefix)
        name << (int)index + 1;
    return name;
}

juce::String waveformSyntax(const circuit_sim::Waveform& w)
{
    using K = circuit_sim::Waveform::Kind;
    switch (w.kind)
    {
        case K::Dc:
            return "DC " + value(w.dcValue()) + " AC " + value(w.acMagnitude != 0.0 ? w.acMagnitude : 0.0) + " " + value(w.acPhaseDegrees);
        case K::Sine:
            return "DC " + value(w.offset) + " AC " + value(w.acMagnitude != 0.0 ? w.acMagnitude : std::max(1.0, std::abs(w.amplitude)))
                + " " + value(w.acPhaseDegrees) + " SIN(" + value(w.offset) + " " + value(w.amplitude) + " " + value(w.frequency)
                + " 0 0 " + value(w.phaseDegrees) + ")";
        case K::Pulse:
            return "DC " + value(w.offset) + " PULSE(" + value(w.offset) + " " + value(w.pulsed) + " " + value(w.delay) + " "
                + value(w.rise) + " " + value(w.fall) + " " + value(w.width) + " " + value(w.period) + ")";
        case K::Pwl:
        {
            juce::String s = "DC " + value(w.dcValue()) + " PWL(";
            for (size_t i = 0; i < w.points.size(); ++i)
            {
                if (i != 0) s << " ";
                s << value(w.points[i].first) << " " << value(w.points[i].second);
            }
            s << ")";
            return s;
        }
        case K::Square:
        {
            const auto period = w.frequency > 0.0 ? 1.0 / w.frequency : 1e-3;
            const auto width = period * std::clamp(w.duty, 0.001, 0.999);
            return "DC " + value(w.offset) + " AC " + value(w.acMagnitude != 0.0 ? w.acMagnitude : std::max(1.0, std::abs(w.amplitude)))
                + " PULSE(" + value(w.offset - w.amplitude) + " " + value(w.offset + w.amplitude) + " 0 1n 1n "
                + value(width) + " " + value(period) + ")";
        }
        case K::Exp:
            return "DC " + value(w.offset) + " EXP(" + value(w.offset) + " " + value(w.pulsed) + " " + value(w.delay) + " "
                + value(w.tau1) + " " + value(w.delay2) + " " + value(w.tau2) + ")";
    }
    return "DC 0";
}

juce::String behavioralExpression(const Element& e)
{
    auto expression = juce::String(e.expression).replaceCharacters("\r\n\t", "   ").trim();
    if (expression.startsWithChar('{') && expression.endsWithChar('}'))
        expression = expression.substring(1, expression.length() - 1).trim();
    return expression;
}

juce::String modelNameFor(const Element& e)
{
    switch (e.type)
    {
        case ElementType::Diode: return "DGEN";
        case ElementType::Npn: return "NPNGEN";
        case ElementType::Pnp: return "PNPGEN";
        case ElementType::Nmos: return "NMOSGEN";
        case ElementType::Pmos: return "PMOSGEN";
        case ElementType::Njfet: return "NJFGEN";
        case ElementType::Pjfet: return "PJFGEN";
        default: break;
    }
    return {};
}

void appendModelLines(const analytics::Netlist& n, juce::String& netlist)
{
    std::set<juce::String> needed;
    for (const auto& e : n.circuit.elements())
    {
        const auto m = modelNameFor(e);
        if (m.isNotEmpty())
            needed.insert(m);
    }
    if (needed.empty())
        return;
    netlist << "\n* Generated fallback models\n";
    if (needed.count("DGEN")) netlist << ".MODEL DGEN D\n";
    if (needed.count("NPNGEN")) netlist << ".MODEL NPNGEN NPN\n";
    if (needed.count("PNPGEN")) netlist << ".MODEL PNPGEN PNP\n";
    if (needed.count("NMOSGEN")) netlist << ".MODEL NMOSGEN NMOS\n";
    if (needed.count("PMOSGEN")) netlist << ".MODEL PMOSGEN PMOS\n";
    if (needed.count("NJFGEN")) netlist << ".MODEL NJFGEN NJF\n";
    if (needed.count("PJFGEN")) netlist << ".MODEL PJFGEN PJF\n";
}

const spice_library::ModelDef* findUa741Model()
{
    static const bool initialized = [] {
        spice_library::initialize();
        return true;
    }();
    juce::ignoreUnused(initialized);

    for (const auto& name : { "UA741", "uA741", "LM741" })
        if (const auto* def = spice_library::findModel(name); def != nullptr && def->kind.equalsIgnoreCase("SUBCKT"))
            return def;

    return nullptr;
}

bool isPrimaryUa741(const analytics::Netlist& n, size_t elementIndex)
{
    for (const auto& part : n.parts)
        if (part.element == (int)elementIndex && part.symbolId == "opamp_741")
            return true;
    return false;
}

bool hasPrimaryUa741Named(const analytics::Netlist& n, const juce::String& name)
{
    for (const auto& part : n.parts)
        if (part.symbolId == "opamp_741" && part.element >= 0 && part.element < (int)n.circuit.elements().size())
            if (juce::String(n.circuit.elements()[(size_t)part.element].name) == name)
                return true;
    return false;
}

bool shouldSkipUa741Internal(const analytics::Netlist& n, const Element& e, size_t elementIndex)
{
    if (isPrimaryUa741(n, elementIndex))
        return false;

    const auto name = juce::String(e.name);
    for (const auto& suffix : { ".out", ".rp", ".cp" })
        if (name.endsWithIgnoreCase(suffix))
            return hasPrimaryUa741Named(n, name.dropLastCharacters(juce::String(suffix).length()));

    return false;
}

circuit_sim::Node ua741OutputNode(const analytics::Netlist& n, const Element& e)
{
    const auto outputStageName = juce::String(e.name) + ".out";
    for (const auto& candidate : n.circuit.elements())
        if (candidate.type == ElementType::OpAmp && juce::String(candidate.name) == outputStageName && candidate.nodes.size() >= 3)
            return candidate.nodes[2];
    return e.nodes[2];
}

bool appendElement(const analytics::Netlist& n, juce::String& netlist, std::map<int, juce::String>& voltageSourceNames,
                   std::set<juce::String>& subcircuits, const Element& e, size_t index, juce::String& error)
{
    if (shouldSkipUa741Internal(n, e, index))
        return true;

    auto nd = [&](size_t i) { return nodeName(n, e.nodes[i]); };
    switch (e.type)
    {
        case ElementType::Resistor:
        case ElementType::VariableResistor:
        case ElementType::Switch:
            netlist << elementName(e, index, "R") << " " << nd(0) << " " << nd(1) << " " << value(std::max(e.value, 1e-9)) << "\n";
            return true;
        case ElementType::Capacitor:
            netlist << elementName(e, index, "C") << " " << nd(0) << " " << nd(1) << " " << value(e.value) << "\n";
            return true;
        case ElementType::Inductor:
            netlist << elementName(e, index, "L") << " " << nd(0) << " " << nd(1) << " " << value(e.value) << "\n";
            return true;
        case ElementType::VoltageSource:
        {
            const auto name = sourceName(e, index);
            voltageSourceNames[(int)index] = name;
            netlist << name << " " << nd(0) << " " << nd(1) << " " << waveformSyntax(e.wave) << "\n";
            return true;
        }
        case ElementType::CurrentSource:
            netlist << sourceName(e, index) << " " << nd(0) << " " << nd(1) << " " << waveformSyntax(e.wave) << "\n";
            return true;
        case ElementType::BehavioralVoltageSource:
        case ElementType::BehavioralCurrentSource:
        {
            const auto expression = behavioralExpression(e);
            if (expression.isEmpty())
            {
                error = juce::String(e.name) + " has an empty behavioral source expression.";
                return false;
            }
            netlist << elementName(e, index, "B") << " " << nd(0) << " " << nd(1)
                    << " " << (e.type == ElementType::BehavioralVoltageSource ? "V" : "I")
                    << "={" << expression << "}\n";
            return true;
        }
        case ElementType::Vcvs:
            netlist << elementName(e, index, "E") << " " << nd(0) << " " << nd(1) << " " << nd(2) << " " << nd(3) << " " << value(e.value) << "\n";
            return true;
        case ElementType::Vccs:
            netlist << elementName(e, index, "G") << " " << nd(0) << " " << nd(1) << " " << nd(2) << " " << nd(3) << " " << value(e.value) << "\n";
            return true;
        case ElementType::Ccvs:
        case ElementType::Cccs:
        {
            const auto found = voltageSourceNames.find(e.control);
            if (found == voltageSourceNames.end())
            {
                error = juce::String(e.name) + " controls a source that has not been lowered to Xyce.";
                return false;
            }
            netlist << elementName(e, index, e.type == ElementType::Ccvs ? "H" : "F") << " " << nd(0) << " " << nd(1)
                    << " " << found->second << " " << value(e.value) << "\n";
            return true;
        }
        case ElementType::Diode:
            netlist << elementName(e, index, "D") << " " << nd(0) << " " << nd(1) << " DGEN\n";
            return true;
        case ElementType::Npn:
        case ElementType::Pnp:
            netlist << elementName(e, index, "Q") << " " << nd(0) << " " << nd(1) << " " << nd(2) << " " << modelNameFor(e) << "\n";
            return true;
        case ElementType::Nmos:
        case ElementType::Pmos:
            netlist << elementName(e, index, "M") << " " << nd(0) << " " << nd(1) << " " << nd(2) << " " << nd(2) << " " << modelNameFor(e) << "\n";
            return true;
        case ElementType::Njfet:
        case ElementType::Pjfet:
            netlist << elementName(e, index, "J") << " " << nd(0) << " " << nd(1) << " " << nd(2) << " " << modelNameFor(e) << "\n";
            return true;
        case ElementType::Coupling:
        {
            const auto a = elementName(n.circuit.elements()[(size_t)e.control], (size_t)e.control, "L");
            const auto b = elementName(n.circuit.elements()[(size_t)e.control2], (size_t)e.control2, "L");
            netlist << elementName(e, index, "K") << " " << a << " " << b << " " << value(e.value) << "\n";
            return true;
        }
        case ElementType::OpAmp:
        {
            if (!isPrimaryUa741(n, index))
            {
                error = juce::String(e.name) + " is an op amp with no Xyce model binding. Use opamp_741 or add an explicit model mapping.";
                return false;
            }

            const auto* model = findUa741Model();
            if (model == nullptr)
            {
                error = "UA741 model is missing from the SPICE model library. Expected a SUBCKT named UA741.";
                return false;
            }

            subcircuits.insert(model->rawText);
            // UA741 subckt pins: 1=IN+, 2=IN-, 3=V+, 4=V-, 5=OUT.
            netlist << elementName(e, index, "X") << " "
                    << nd(0) << " "
                    << nd(1) << " "
                    << nd(3) << " "
                    << nd(4) << " "
                    << nodeName(n, ua741OutputNode(n, e)) << " "
                    << model->name << "\n";
            return true;
        }
    }
    error = juce::String(e.name) + " is not supported by the Xyce backend.";
    return false;
}

juce::String printList(const analytics::Netlist& n, bool currents)
{
    juce::String s;
    for (const auto& net : n.nets)
        if (net.node != 0)
            s << " V(" << nodeName(n, net.node) << ")";
    if (currents)
        for (const auto& e : n.circuit.elements())
            if (e.type == ElementType::VoltageSource)
                s << " I(" << sourceName(e, 0).dropLastCharacters(sourceName(e, 0).length()) << ")"; // not used
    return s;
}

juce::String netlistFor(analytics::Analysis analysis, const analytics::Settings& s, const analytics::Netlist& n, juce::String& error)
{
    juce::String out;
    out << "* Djehuti Workbench Xyce backend netlist\n";
    out << "* Generated from canonical Workbench Analytics netlist\n\n";
    std::map<int, juce::String> voltageSourceNames;
    std::set<juce::String> subcircuits;
    juce::String body;
    for (size_t i = 0; i < n.circuit.elements().size(); ++i)
        if (!appendElement(n, body, voltageSourceNames, subcircuits, n.circuit.elements()[i], i, error))
            return {};
    if (!subcircuits.empty())
    {
        out << "\n* SPICE subcircuit models\n";
        for (const auto& subcircuit : subcircuits)
            out << subcircuit.trim() << "\n";
    }
    appendModelLines(n, out);
    out << "\n" << body;

    auto netPrints = [&] {
        juce::String p;
        for (const auto& net : n.nets)
            if (net.node != 0)
                p << " V(" << nodeName(n, net.node) << ")";
        return p.isEmpty() ? juce::String(" V(0)") : p;
    };

    if (analysis == analytics::Analysis::OperatingPoint)
    {
        out << "\n.OP\n.PRINT DC" << netPrints();
    }
    else if (analysis == analytics::Analysis::Transient)
    {
        const auto stop = s.count("stop") != 0 && !s.at("stop").equalsIgnoreCase("auto") ? s.at("stop") : "10m";
        const auto step = s.count("step") != 0 && !s.at("step").equalsIgnoreCase("auto") ? s.at("step") : "10u";
        out << "\n.TRAN " << step << " " << stop << "\n.PRINT TRAN" << netPrints();
    }
    else if (analysis == analytics::Analysis::Ac)
    {
        const auto start = s.count("start") != 0 ? s.at("start") : "10";
        const auto stop = s.count("stop") != 0 ? s.at("stop") : "100k";
        const auto points = s.count("points") != 0 ? s.at("points") : "50";
        out << "\n.AC DEC " << points << " " << start << " " << stop << "\n.PRINT AC";
        for (const auto& net : n.nets)
            if (net.node != 0)
                out << " VM(" << nodeName(n, net.node) << ") VP(" << nodeName(n, net.node) << ")";
    }
    else
    {
        error = "The Xyce backend currently supports Operating Point, Transient and AC/Bode. Select Internal Solver for "
            + analytics::infoFor(analysis).title + " until its Xyce adapter is implemented.";
        return {};
    }

    out << "\n.END\n";
    return out;
}

juce::Array<juce::StringArray> readPrn(const juce::File& file)
{
    auto splitWhitespace = [](const juce::String& text) {
        juce::StringArray tokens;
        juce::String current;
        for (auto c : text)
        {
            if (juce::CharacterFunctions::isWhitespace(c))
            {
                if (current.isNotEmpty())
                {
                    tokens.add(current);
                    current.clear();
                }
            }
            else
            {
                current << juce::String::charToString(c);
            }
        }
        if (current.isNotEmpty())
            tokens.add(current);
        return tokens;
    };

    juce::StringArray lines;
    file.readLines(lines);
    juce::Array<juce::StringArray> rows;
    for (const auto& line : lines)
    {
        const auto t = line.trim();
        if (t.isEmpty() || t.startsWithIgnoreCase("End of"))
            continue;
        rows.add(splitWhitespace(t));
    }
    return rows;
}

bool parseNumber(const juce::String& s, double& valueOut)
{
    valueOut = s.getDoubleValue();
    return s.containsAnyOf("0123456789");
}

analytics::Result resultFromPrn(analytics::Analysis analysis, const analytics::Settings& settings, const juce::File& prn,
                                const analytics::Netlist& n, double seconds)
{
    analytics::Result r;
    r.ok = true;
    r.analysis = analysis;
    r.title = analytics::infoFor(analysis).title + " - Xyce";
    r.settings = settings;
    r.when = juce::Time::getCurrentTime();
    r.seconds = seconds;
    const auto rows = readPrn(prn);
    if (rows.size() < 2)
    {
        r.ok = false;
        r.error = "Xyce produced no parseable .prn data: " + prn.getFullPathName();
        return r;
    }
    const auto header = rows[0];
    if (analysis == analytics::Analysis::OperatingPoint)
    {
        analytics::Table table;
        table.title = "Xyce operating point";
        table.columns = { "Signal", "Value" };
        for (int c = 1; c < header.size() && c < rows[1].size(); ++c)
            table.rows.push_back({ header[c], analytics::formatNumber(rows[1][c].getDoubleValue(), header[c].startsWithIgnoreCase("V(") ? "V" : "A", 7) });
        r.tables.push_back(std::move(table));
        r.summary = "Xyce operating point completed; " + juce::String(header.size() - 1) + " signal(s).";
        return r;
    }

    analytics::Plot plot;
    plot.kind = analytics::Plot::Kind::Lines;
    plot.title = analysis == analytics::Analysis::Ac ? "Xyce AC response" : "Xyce transient";
    plot.xLabel = analysis == analytics::Analysis::Ac ? "Frequency" : "Time";
    plot.xUnit = analysis == analytics::Analysis::Ac ? "Hz" : "s";
    plot.yLabel = analysis == analytics::Analysis::Ac ? "Magnitude / phase" : "Voltage";
    plot.yUnit = analysis == analytics::Analysis::Ac ? "" : "V";
    plot.logX = analysis == analytics::Analysis::Ac;
    const bool hasDomainColumn = header.size() > 1
        && (header[1].equalsIgnoreCase("FREQ") || header[1].equalsIgnoreCase("TIME"));
    const int xColumn = hasDomainColumn ? 1 : 0;
    const int firstTraceColumn = hasDomainColumn ? 2 : 1;
    for (int c = firstTraceColumn; c < header.size(); ++c)
    {
        analytics::Trace trace;
        trace.name = header[c];
        trace.unit = header[c].startsWithIgnoreCase("V(") || header[c].startsWithIgnoreCase("VM(") ? "V" : header[c].startsWithIgnoreCase("VP(") ? "deg" : "";
        for (int rix = 1; rix < rows.size(); ++rix)
        {
            if (rows[rix].size() <= c)
                continue;
            trace.x.push_back(rows[rix][xColumn].getDoubleValue());
            trace.y.push_back(rows[rix][c].getDoubleValue());
        }
        plot.traces.push_back(std::move(trace));
    }
    r.plots.push_back(std::move(plot));
    r.summary = "Xyce " + analytics::infoFor(analysis).title + " completed; " + juce::String(rows.size() - 1) + " sample(s).";
    juce::ignoreUnused(n);
    return r;
}

juce::String processOutput(juce::ChildProcess& process)
{
    juce::MemoryOutputStream output;
    while (process.isRunning())
    {
        output << process.readAllProcessOutput();
        juce::Thread::sleep(20);
    }
    output << process.readAllProcessOutput();
    return output.toString();
}
}

juce::String engineName(Engine engine)
{
    return engine == Engine::Xyce ? "Xyce" : "Internal Solver";
}

juce::File configuredExecutable()
{
    const auto config = juce::File::getCurrentWorkingDirectory().getChildFile("config").getChildFile("xyce.local.json");
    if (config.existsAsFile())
    {
        const auto parsed = juce::JSON::parse(config);
        const auto path = parsed.getProperty("xyceExe", {}).toString();
        if (path.isNotEmpty())
            return juce::File(path);
    }
    for (const auto& p : { "C:\\Program Files\\XyceNF_7.10\\bin\\Xyce.exe",
                           "C:\\Program Files\\Xyce 7.10 NORAD\\bin\\Xyce.exe",
                           "C:\\Program Files\\Xyce\\bin\\Xyce.exe" })
        if (juce::File(p).existsAsFile())
            return juce::File(p);
    return {};
}

bool isConfigured(juce::String& detail)
{
    const auto exe = configuredExecutable();
    if (!exe.existsAsFile())
    {
        detail = "Xyce executable is not configured. Run tools/xyce/setup_xyce.ps1.";
        return false;
    }
    detail = exe.getFullPathName();
    return true;
}

analytics::Result run(analytics::Analysis analysis, const analytics::Settings& settings, const analytics::Netlist& netlist,
                      const juce::File& outputRoot)
{
    analytics::Result failed;
    failed.analysis = analysis;
    failed.title = analytics::infoFor(analysis).title + " - Xyce";
    failed.settings = settings;
    failed.when = juce::Time::getCurrentTime();

    if (netlist.circuit.elements().empty())
    {
        failed.error = "The diagram has nothing to simulate.";
        return failed;
    }

    juce::String detail;
    if (!isConfigured(detail))
    {
        failed.error = detail;
        return failed;
    }

    juce::String netlistError;
    const auto netlistText = netlistFor(analysis, settings, netlist, netlistError);
    if (netlistText.isEmpty())
    {
        failed.error = netlistError;
        return failed;
    }

    auto root = outputRoot.exists() ? outputRoot : juce::File::getCurrentWorkingDirectory().getChildFile("sim").getChildFile("xyce").getChildFile("runs");
    auto runDir = root.getChildFile("xyce_backend").getChildFile(juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S"));
    runDir.createDirectory();
    const auto cir = runDir.getChildFile("generated.cir");
    if (!cir.replaceWithText(netlistText))
    {
        failed.error = "Could not write Xyce netlist: " + cir.getFullPathName();
        return failed;
    }

    const auto started = juce::Time::getMillisecondCounterHiRes();
    juce::ChildProcess process;
    const auto command = quotePath(configuredExecutable()) + " " + quotePath(cir);
    if (!process.start(command))
    {
        failed.error = "Could not start Xyce process: " + command;
        return failed;
    }
    const auto stdoutText = processOutput(process);
    runDir.getChildFile("xyce_stdout.txt").replaceWithText(stdoutText);
    const auto exitCode = process.getExitCode();
    if (exitCode != 0)
    {
        failed.error = "Xyce failed with exit code " + juce::String(exitCode) + ". See " + runDir.getChildFile("xyce_stdout.txt").getFullPathName();
        return failed;
    }

    auto prn = cir.getSiblingFile(cir.getFileName() + ".prn");
    if (!prn.existsAsFile())
    {
        const auto prns = runDir.findChildFiles(juce::File::findFiles, false, cir.getFileName() + "*.prn");
        if (!prns.isEmpty())
            prn = prns[0];
    }
    if (!prn.existsAsFile())
    {
        failed.error = "Xyce completed but did not produce " + prn.getFullPathName();
        return failed;
    }
    auto result = resultFromPrn(analysis, settings, prn, netlist, (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0);
    result.warnings.add("Backend: Xyce external process (" + configuredExecutable().getFullPathName() + ")");
    result.warnings.add("Netlist: " + cir.getFullPathName());
    return result;
}
}
