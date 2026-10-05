#include "PcbFab.h"

#include "PcbArtwork.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>

namespace pcb::fab
{
namespace
{
const char* const vendor = "Djehuti";
const char* const application = "Electronics Lab";
const char* const version = "0.1.0";

// ---- Plans: what each file must contain --------------------------------------

struct Prim
{
    enum class Type { Flash, Draw, Region };
    Type type = Type::Flash;
    bool round = true;
    double w = 0.0, h = 0.0;    // aperture; a draw uses a round aperture of diameter w
    std::vector<Point> pts;     // flash 1, draw 2, region contour
    bool dark = true;
    juce::String function;      // X2 aperture function
};

struct LayerPlan
{
    juce::String suffix, fileFunction, polarity;
    std::vector<Prim> prims;
};

void addArtwork(LayerPlan& plan, const Artwork& art, const juce::String& function)
{
    for (const auto& f : art.fills)
        if (f.size() >= 3)
        {
            Prim p;
            p.type = Prim::Type::Region;
            p.pts = f;
            plan.prims.push_back(p);
        }
    for (const auto& s : art.strokes)
        for (size_t i = 1; i < s.points.size(); ++i)
        {
            Prim p;
            p.type = Prim::Type::Draw;
            p.w = p.h = s.width;
            p.pts = { s.points[i - 1], s.points[i] };
            p.function = function;
            plan.prims.push_back(p);
        }
}

Prim flash(Point at, double w, double h, bool round, const juce::String& function, bool dark = true)
{
    Prim p;
    p.type = Prim::Type::Flash;
    p.round = round;
    p.w = w;
    p.h = round ? w : h;
    p.pts = { at };
    p.function = function;
    p.dark = dark;
    return p;
}

// ---- Gerber X2 writer --------------------------------------------------------

long long nm6(double mm) { return std::llround(mm * 1e6); }
juce::String num(double v) { return juce::String(v, 6); }
juce::String xy(Point p) { return "X" + juce::String(nm6(p.x)) + "Y" + juce::String(nm6(p.y)); }

juce::String apertureKey(const Prim& p)
{
    return (p.round ? "C" : "R") + num(p.w) + "x" + num(p.h) + "|" + p.function;
}

juce::String writeGerber(const LayerPlan& plan, const juce::String& date)
{
    juce::String out;
    out << "%TF.GenerationSoftware," << vendor << "," << application << "," << version << "*%\n";
    out << "%TF.CreationDate," << date << "*%\n";
    out << "%TF.SameCoordinates,Original*%\n";
    out << "%TF.FileFunction," << plan.fileFunction << "*%\n";
    out << "%TF.FilePolarity," << plan.polarity << "*%\n";
    out << "%FSLAX46Y46*%\n%MOMM*%\n";
    std::map<juce::String, int> codes;
    int next = 10;
    for (const auto& p : plan.prims)
    {
        if (p.type == Prim::Type::Region) continue;
        const auto key = apertureKey(p);
        if (codes.count(key) != 0) continue;
        if (p.function.isNotEmpty()) out << "%TA.AperFunction," << p.function << "*%\n";
        out << "%ADD" << next << (p.round ? "C," + num(p.w) : "R," + num(p.w) + "X" + num(p.h)) << "*%\n";
        if (p.function.isNotEmpty()) out << "%TD*%\n";
        codes[key] = next++;
    }
    out << "G01*\n%LPD*%\n";
    bool dark = true;
    int current = -1;
    bool haveLast = false;
    Point last;
    for (const auto& p : plan.prims)
    {
        if (p.dark != dark)
        {
            out << (p.dark ? "%LPD*%\n" : "%LPC*%\n");
            dark = p.dark;
        }
        if (p.type == Prim::Type::Region)
        {
            out << "G36*\n" << xy(p.pts[0]) << "D02*\n";
            for (size_t i = 1; i < p.pts.size(); ++i) out << xy(p.pts[i]) << "D01*\n";
            out << xy(p.pts[0]) << "D01*\nG37*\n";
            haveLast = false;
            continue;
        }
        const int code = codes[apertureKey(p)];
        if (code != current)
        {
            out << "D" << code << "*\n";
            current = code;
        }
        if (p.type == Prim::Type::Flash)
        {
            out << xy(p.pts[0]) << "D03*\n";
            haveLast = false;
        }
        else
        {
            if (!haveLast || nm6(last.x) != nm6(p.pts[0].x) || nm6(last.y) != nm6(p.pts[0].y))
                out << xy(p.pts[0]) << "D02*\n";
            out << xy(p.pts[1]) << "D01*\n";
            last = p.pts[1];
            haveLast = true;
        }
    }
    out << "M02*\n";
    return out;
}

// ---- Gerber reader (read-back check) -----------------------------------------

struct GerberRead
{
    std::vector<Prim> prims;
    juce::String fileFunction, polarity, error;
};

GerberRead readGerber(const juce::String& text)
{
    GerberRead r;
    struct Ap { bool round; double w, h; juce::String function; };
    std::map<int, Ap> aps;
    juce::String pendingFunction;
    int current = -1;
    bool dark = true, inRegion = false, formatOk = false, mm = false;
    long long x = 0, y = 0;
    std::vector<Point> contour;
    auto toMm = [](long long v) { return (double)v / 1e6; };

    auto extended = [&](const juce::String& s) {
        if (s.startsWith("FS")) formatOk = s == "FSLAX46Y46";
        else if (s == "MOMM") mm = true;
        else if (s == "LPD") dark = true;
        else if (s == "LPC") dark = false;
        else if (s.startsWith("TF.FileFunction,")) r.fileFunction = s.fromFirstOccurrenceOf(",", false, false);
        else if (s.startsWith("TF.FilePolarity,")) r.polarity = s.fromFirstOccurrenceOf(",", false, false);
        else if (s.startsWith("TA.AperFunction,")) pendingFunction = s.fromFirstOccurrenceOf(",", false, false);
        else if (s == "TD") pendingFunction.clear();
        else if (s.startsWith("ADD"))
        {
            const auto body = s.substring(3);
            const int code = body.initialSectionContainingOnly("0123456789").getIntValue();
            const auto rest = body.substring(body.initialSectionContainingOnly("0123456789").length());
            const auto shape = rest.upToFirstOccurrenceOf(",", false, false);
            const auto params = juce::StringArray::fromTokens(rest.fromFirstOccurrenceOf(",", false, false), "X", {});
            if (shape == "C" && params.size() >= 1) aps[code] = { true, params[0].getDoubleValue(), params[0].getDoubleValue(), pendingFunction };
            else if (shape == "R" && params.size() >= 2) aps[code] = { false, params[0].getDoubleValue(), params[1].getDoubleValue(), pendingFunction };
            else r.error = "unsupported aperture " + s;
        }
    };
    auto word = [&](juce::String s) {
        if (s == "G01" || s == "M02" || s.isEmpty()) return;
        if (s == "G36") { inRegion = true; contour.clear(); return; }
        if (s == "G37")
        {
            if (contour.size() >= 2 && contour.front() == contour.back()) contour.pop_back();
            Prim p;
            p.type = Prim::Type::Region;
            p.pts = contour;
            p.dark = dark;
            r.prims.push_back(p);
            inRegion = false;
            return;
        }
        if (s.startsWith("D") && s.substring(1).containsOnly("0123456789") && s.substring(1).getIntValue() >= 10)
        {
            current = s.substring(1).getIntValue();
            return;
        }
        if (s.startsWith("X") || s.startsWith("Y"))
        {
            const long long px = x, py = y;
            const int xi = s.indexOfChar('X'), yi = s.indexOfChar('Y'), di = s.indexOfChar('D');
            if (xi >= 0) x = s.substring(xi + 1, yi > xi ? yi : di).getLargeIntValue();
            if (yi >= 0) y = s.substring(yi + 1, di).getLargeIntValue();
            const int op = s.substring(di + 1).getIntValue();
            const Point here { toMm(x), toMm(y) };
            if (inRegion)
            {
                if (op == 2) contour = { here };
                else contour.push_back(here);
                return;
            }
            const auto ap = aps.find(current);
            if (ap == aps.end()) { r.error = "draw with no aperture"; return; }
            Prim p;
            p.round = ap->second.round;
            p.w = ap->second.w;
            p.h = ap->second.h;
            p.function = ap->second.function;
            p.dark = dark;
            if (op == 3) { p.type = Prim::Type::Flash; p.pts = { here }; r.prims.push_back(p); }
            else if (op == 1) { p.type = Prim::Type::Draw; p.pts = { { toMm(px), toMm(py) }, here }; r.prims.push_back(p); }
            return;
        }
        r.error = "unexpected statement " + s;
    };

    int i = 0;
    const int n = text.length();
    while (i < n)
    {
        const auto c = text[i];
        if (c == '%')
        {
            const int end = text.indexOfChar(i + 1, '%');
            if (end < 0) { r.error = "unterminated extended command"; break; }
            for (const auto& stmt : juce::StringArray::fromTokens(text.substring(i + 1, end), "*", {}))
                if (stmt.trim().isNotEmpty()) extended(stmt.trim());
            i = end + 1;
        }
        else if (c == '\n' || c == '\r' || c == ' ') ++i;
        else
        {
            const int end = text.indexOfChar(i, '*');
            if (end < 0) { r.error = "unterminated statement"; break; }
            word(text.substring(i, end).trim());
            i = end + 1;
        }
    }
    if (!formatOk) r.error = "format statement missing or not 4.6";
    if (!mm) r.error = "units are not millimetres";
    return r;
}

juce::String primKey(const Prim& p)
{
    auto q = [](double v) { return juce::String(std::llround(v * 1e4)); };
    juce::String k;
    k << (int)p.type << (p.dark ? "D" : "C") << "|";
    if (p.type != Prim::Type::Region) k << (p.round ? "C" : "R") << q(p.w) << "x" << q(p.h) << "|" << p.function << "|";
    auto pts = p.pts;
    if (p.type == Prim::Type::Draw && (pts[1].x < pts[0].x || (pts[1].x == pts[0].x && pts[1].y < pts[0].y))) std::swap(pts[0], pts[1]);
    for (const auto& pt : pts) k << q(pt.x) << "," << q(pt.y) << ";";
    return k;
}

// Compares the read-back primitives with the plan; "" when they match.
juce::String comparePrims(const std::vector<Prim>& expected, const std::vector<Prim>& actual)
{
    std::multiset<std::string> a, b;
    for (const auto& p : expected) a.insert(primKey(p).toStdString());
    for (const auto& p : actual) b.insert(primKey(p).toStdString());
    if (a == b) return {};
    int missing = 0, extra = 0;
    juce::String example;
    auto copy = b;
    for (const auto& k : a)
    {
        const auto it = copy.find(k);
        if (it == copy.end()) { if (++missing == 1) example = "missing " + juce::String(k); }
        else copy.erase(it);
    }
    extra = (int)copy.size();
    if (example.isEmpty() && !copy.empty()) example = "extra " + juce::String(*copy.begin());
    return juce::String(missing) + " object(s) missing, " + juce::String(extra) + " extra (" + example + ")";
}

// ---- Excellon ------------------------------------------------------------------

struct Hole
{
    Point at;
    double diameter = 0.0;
};

juce::String writeExcellon(const std::vector<Hole>& holes, bool plated, int layers, const juce::String& date)
{
    std::vector<double> sizes;
    for (const auto& h : holes)
        if (std::find_if(sizes.begin(), sizes.end(), [&](double s) { return std::abs(s - h.diameter) < 1e-6; }) == sizes.end())
            sizes.push_back(h.diameter);
    std::sort(sizes.begin(), sizes.end());
    juce::String out;
    out << "M48\n";
    out << "; DRILL file {" << vendor << " " << application << " " << version << "} date " << date << "\n";
    out << "; FORMAT={-:-/ absolute / metric / decimal}\n";
    out << "; #@! TF.CreationDate," << date << "\n";
    out << "; #@! TF.GenerationSoftware," << vendor << "," << application << "," << version << "\n";
    out << "; #@! TF.FileFunction," << (plated ? "Plated" : "NonPlated") << ",1," << layers << "," << (plated ? "PTH" : "NPTH") << "\n";
    out << "FMAT,2\nMETRIC\n";
    for (size_t i = 0; i < sizes.size(); ++i)
        out << "T" << (int)i + 1 << "C" << juce::String(sizes[i], 3) << "\n";
    out << "%\nG90\nG05\n";
    for (size_t i = 0; i < sizes.size(); ++i)
    {
        out << "T" << (int)i + 1 << "\n";
        for (const auto& h : holes)
            if (std::abs(h.diameter - sizes[i]) < 1e-6)
                out << "X" << juce::String(h.at.x, 4) << "Y" << juce::String(h.at.y, 4) << "\n";
    }
    out << "M30\n";
    return out;
}

std::vector<Hole> readExcellon(const juce::String& text, juce::String& error)
{
    std::vector<Hole> holes;
    std::map<int, double> tools;
    int current = -1;
    bool header = true, metric = false;
    for (auto line : juce::StringArray::fromLines(text))
    {
        line = line.trim();
        if (line.isEmpty() || line.startsWithChar(';')) continue;
        if (header)
        {
            if (line == "METRIC" || line.startsWith("METRIC,")) metric = true;
            else if (line == "%") header = false;
            else if (line.startsWithChar('T') && line.containsChar('C'))
                tools[line.substring(1).upToFirstOccurrenceOf("C", false, false).getIntValue()] = line.fromFirstOccurrenceOf("C", false, false).getDoubleValue();
            continue;
        }
        if (line.startsWithChar('T')) current = line.substring(1).getIntValue();
        else if (line.startsWithChar('X'))
        {
            if (tools.count(current) == 0) { error = "hole before a tool"; continue; }
            holes.push_back({ { line.substring(1).upToFirstOccurrenceOf("Y", false, false).getDoubleValue(),
                                line.fromFirstOccurrenceOf("Y", false, false).getDoubleValue() }, tools[current] });
        }
    }
    if (!metric) error = "not metric";
    return holes;
}

juce::String compareHoles(const std::vector<Hole>& expected, const std::vector<Hole>& actual)
{
    auto key = [](const Hole& h) {
        return std::to_string(std::llround(h.diameter * 1e3)) + "@" + std::to_string(std::llround(h.at.x * 1e4)) + "," + std::to_string(std::llround(h.at.y * 1e4));
    };
    std::multiset<std::string> a, b;
    for (const auto& h : expected) a.insert(key(h));
    for (const auto& h : actual) b.insert(key(h));
    if (a == b) return {};
    return juce::String((int)a.size()) + " holes expected, " + juce::String((int)b.size()) + " read back, contents differ";
}

// ---- IPC-D-356A ----------------------------------------------------------------

struct TestPoint
{
    bool through = false;
    juce::String net, refdes, pin;
    double drill = 0.0;
    int access = 0;   // 0 both sides, 1 top
    Point at;
    double w = 0.0, h = 0.0;
};

void put(std::string& line, int column, const std::string& text)
{
    // 1-based column, fixed width record.
    if ((int)line.size() < column - 1 + (int)text.size()) line.resize((size_t)(column - 1 + (int)text.size()), ' ');
    line.replace((size_t)(column - 1), text.size(), text);
}

std::string digits(long long v, int width)
{
    auto s = std::to_string(std::llabs(v));
    while ((int)s.size() < width) s = "0" + s;
    return s.substr(s.size() - (size_t)width);
}

std::string signedDigits(long long v, int width) { return (v < 0 ? "-" : "+") + digits(v, width); }

std::string ipcName(const juce::String& s, int width)
{
    auto t = s.removeCharacters(" ").toStdString();
    if ((int)t.size() > width) t = t.substr(0, (size_t)width);
    return t;
}

juce::String writeIpc(const std::vector<TestPoint>& points, const juce::String& name)
{
    juce::String out;
    out << "C  IPC-D-356A netlist generated by " << vendor << " " << application << " " << version << "\n";
    out << "C  " << name << "\n";
    out << "P  JOB   " << name << "\n";
    out << "P  UNITS CUST 1\n";
    out << "P  DIM   N\n";
    for (const auto& t : points)
    {
        std::string line(80, ' ');
        put(line, 1, t.through ? "317" : "327");
        put(line, 4, ipcName(t.net.isEmpty() ? juce::String("N/C") : t.net, 14));
        put(line, 21, ipcName(t.refdes, 6));
        put(line, 27, "-");
        put(line, 28, ipcName(t.pin, 4));
        if (t.through)
        {
            put(line, 33, "D" + digits(std::llround(t.drill * 1000.0), 4));
            put(line, 38, "P");
        }
        put(line, 39, "A" + digits(t.access == 0 ? 0 : 1, 2));
        put(line, 42, "X" + signedDigits(std::llround(t.at.x * 1000.0), 6));
        put(line, 50, "Y" + signedDigits(std::llround(t.at.y * 1000.0), 6));
        put(line, 58, "X" + digits(std::llround(t.w * 1000.0), 4));
        put(line, 63, "Y" + digits(std::llround(t.h * 1000.0), 4));
        put(line, 68, "R000");
        put(line, 73, "S0");
        out << juce::String(line).trimEnd() << "\n";
    }
    out << "999\n";
    return out;
}

std::vector<TestPoint> readIpc(const juce::String& text, juce::String& error)
{
    std::vector<TestPoint> points;
    bool metric = false, ended = false;
    for (const auto& raw : juce::StringArray::fromLines(text))
    {
        const auto line = raw.toStdString();
        if (line.rfind("P  UNITS CUST 1", 0) == 0) metric = true;
        if (line.rfind("999", 0) == 0) ended = true;
        if (line.size() < 74 || (line.rfind("317", 0) != 0 && line.rfind("327", 0) != 0)) continue;
        auto field = [&](int col, int width) { return juce::String(line.substr((size_t)(col - 1), (size_t)width)).trim(); };
        // Numbers carry an explicit sign; JUCE's integer parse only takes '-'.
        auto number = [&](int col, int width) { return field(col, width).removeCharacters("+").getIntValue(); };
        TestPoint t;
        t.through = line.rfind("317", 0) == 0;
        t.net = field(4, 14);
        if (t.net == "N/C") t.net.clear();
        t.refdes = field(21, 6);
        t.pin = field(28, 4);
        if (t.through) t.drill = number(34, 4) / 1000.0;
        t.access = number(40, 2);
        t.at = { number(43, 7) / 1000.0, number(51, 7) / 1000.0 };
        t.w = number(59, 4) / 1000.0;
        t.h = number(64, 4) / 1000.0;
        points.push_back(t);
    }
    if (!metric) error = "units record missing";
    if (!ended) error = "no end record";
    return points;
}

juce::String compareIpc(const std::vector<TestPoint>& expected, const std::vector<TestPoint>& actual)
{
    auto key = [](const TestPoint& t) {
        return (t.through ? "T" : "S") + ipcName(t.net, 14) + "|" + ipcName(t.refdes, 6) + "-" + ipcName(t.pin, 4) + "|"
             + std::to_string(std::llround(t.drill * 1000.0)) + "|" + std::to_string(t.access) + "|" + std::to_string(std::llround(t.at.x * 1000.0)) + ","
             + std::to_string(std::llround(t.at.y * 1000.0)) + "|" + std::to_string(std::llround(t.w * 1000.0)) + "x" + std::to_string(std::llround(t.h * 1000.0));
    };
    std::multiset<std::string> a, b;
    for (const auto& t : expected) a.insert(key(t));
    for (const auto& t : actual) b.insert(key(t));
    if (a == b) return {};
    return juce::String((int)a.size()) + " test points expected, " + juce::String((int)b.size()) + " read back, contents differ";
}

juce::String csvField(const juce::String& s)
{
    return s.containsAnyOf(",\"\n") ? "\"" + s.replace("\"", "\"\"") + "\"" : s;
}

juce::String fileSafe(const juce::String& s)
{
    auto t = s.trim().replaceCharacters(" \\/:*?\"<>|", "__________");
    return t.isEmpty() ? juce::String("board") : t;
}
}

ExportResult exportFab(const Layout& layout, const BoardDesign& board, const juce::File& folder, const juce::String& rawName,
                       bool schematicMatches, const juce::String& schematicSummary)
{
    ExportResult result;
    result.folder = folder;
    auto blocking = board.problems();
    blocking.addArray(placementProblems(layout, board));
    if (layout.parts.empty()) blocking.add("No parts on the board.");
    if (!blocking.isEmpty())
    {
        result.problems = blocking;
        result.summary = "Not exported: fix the board first.";
        return result;
    }
    if (!folder.createDirectory())
    {
        result.problems.add("Could not create " + folder.getFullPathName());
        result.summary = "Not exported.";
        return result;
    }

    const auto name = fileSafe(rawName);
    // ISO 8601 to whole seconds with the UTC offset (some CAM tools reject fractions).
    auto date = juce::Time::getCurrentTime().toISO8601(true);
    if (date.containsChar('.'))
    {
        const int dot = date.indexOfChar('.');
        int end = dot + 1;
        while (end < date.length() && juce::CharacterFunctions::isDigit(date[end])) ++end;
        date = date.substring(0, dot) + date.substring(end);
    }
    const int layers = std::max(1, board.layers);
    const auto pads = allPads(layout);
    std::vector<std::pair<juce::String, juce::String>> jobFiles; // path, function|polarity
    bool allOk = true;

    auto writeFile = [&](const juce::String& fileName, const juce::String& text) {
        const auto f = folder.getChildFile(fileName);
        if (!f.replaceWithText(text, false, false, "\n"))
        {
            result.problems.add("Could not write " + fileName);
            allOk = false;
            return false;
        }
        result.files.add(fileName);
        return true;
    };

    // ---- Gerber layers.
    std::vector<LayerPlan> plans;
    for (int l = 0; l < layers; ++l)
    {
        LayerPlan plan;
        const bool top = l == 0, bottom = l == layers - 1 && layers > 1;
        plan.suffix = top ? "F_Cu" : bottom ? "B_Cu" : "In" + juce::String(l) + "_Cu";
        plan.fileFunction = "Copper,L" + juce::String(l + 1) + "," + (top ? "Top" : bottom ? "Bot" : "Inr");
        plan.polarity = "Positive";
        if (top || bottom)
            addArtwork(plan, userArtwork(layout, top ? "F.Cu" : "B.Cu"), "NonConductor");
        for (const auto& t : layout.tracks)
            if (t.layer == l)
                for (size_t i = 1; i < t.points.size(); ++i)
                {
                    Prim p;
                    p.type = Prim::Type::Draw;
                    p.w = p.h = t.width;
                    p.pts = { t.points[i - 1], t.points[i] };
                    p.function = "Conductor";
                    plan.prims.push_back(p);
                }
        for (const auto& pad : pads)
            if (pad.drill > 0.0 || l == 0)
                plan.prims.push_back(flash(pad.centre, pad.w, pad.h, pad.round, pad.drill > 0.0 ? "ComponentPad" : "SMDPad,CuDef"));
        for (const auto& v : layout.vias)
            plan.prims.push_back(flash(v.at, v.diameter, v.diameter, true, "ViaPad"));
        plans.push_back(plan);
    }
    for (const bool top : { true, false })
    {
        LayerPlan mask;
        mask.suffix = top ? "F_Mask" : "B_Mask";
        mask.fileFunction = top ? "Soldermask,Top" : "Soldermask,Bot";
        mask.polarity = "Negative";
        for (const auto& o : maskOpenings(layout, top)) mask.prims.push_back(flash(o.centre, o.w, o.h, o.round, {}));
        plans.push_back(mask);

        LayerPlan silk;
        silk.suffix = top ? "F_Silkscreen" : "B_Silkscreen";
        silk.fileFunction = top ? "Legend,Top" : "Legend,Bot";
        silk.polarity = "Positive";
        addArtwork(silk, silkscreen(layout, top), {});
        if (!silk.prims.empty())
            for (const auto& o : silkClearAreas(layout, top)) silk.prims.push_back(flash(o.centre, o.w, o.h, o.round, {}, false));
        plans.push_back(silk);
    }
    {
        LayerPlan paste;
        paste.suffix = "F_Paste";
        paste.fileFunction = "Paste,Top";
        paste.polarity = "Positive";
        for (const auto& o : pasteOpenings(layout)) paste.prims.push_back(flash(o.centre, o.w, o.h, o.round, {}));
        plans.push_back(paste);

        LayerPlan edge;
        edge.suffix = "Edge_Cuts";
        edge.fileFunction = "Profile,NP";
        edge.polarity = "Positive";
        auto loop = [&](const std::vector<Point>& pts) {
            for (size_t i = 0; i < pts.size(); ++i)
            {
                Prim p;
                p.type = Prim::Type::Draw;
                p.w = p.h = 0.05;
                p.pts = { pts[i], pts[(i + 1) % pts.size()] };
                p.function = "Profile";
                edge.prims.push_back(p);
            }
        };
        loop(board.outline);
        for (const auto& c : board.cutouts) loop(c);
        plans.push_back(edge);
    }
    for (const auto& plan : plans)
    {
        const auto fileName = name + "-" + plan.suffix + ".gbr";
        const auto text = writeGerber(plan, date);
        if (!writeFile(fileName, text)) continue;
        jobFiles.push_back({ fileName, plan.fileFunction + "|" + plan.polarity });
        const auto back = readGerber(folder.getChildFile(fileName).loadFileAsString());
        juce::String issue = back.error;
        if (issue.isEmpty() && back.fileFunction != plan.fileFunction) issue = "file function reads back as " + back.fileFunction;
        if (issue.isEmpty()) issue = comparePrims(plan.prims, back.prims);
        if (issue.isEmpty())
            result.checks.add(fileName + ": " + juce::String((int)back.prims.size()) + " objects read back, all match the board.");
        else
        {
            result.checks.add(fileName + ": MISMATCH - " + issue);
            result.problems.add(fileName + " does not read back as the board: " + issue);
            allOk = false;
        }
    }

    // ---- Drills.
    std::vector<Hole> plated, nonPlated;
    for (const auto& pad : pads)
        if (pad.drill > 0.0) plated.push_back({ pad.centre, pad.drill });
    for (const auto& v : layout.vias) plated.push_back({ v.at, v.drill });
    for (const auto& h : board.holes) nonPlated.push_back({ h.centre, h.diameter });
    for (const bool isPlated : { true, false })
    {
        const auto& holes = isPlated ? plated : nonPlated;
        const auto fileName = name + (isPlated ? "-PTH.drl" : "-NPTH.drl");
        if (!writeFile(fileName, writeExcellon(holes, isPlated, layers, date))) continue;
        jobFiles.push_back({ fileName, juce::String(isPlated ? "Plated,1," : "NonPlated,1,") + juce::String(layers) + (isPlated ? ",PTH" : ",NPTH") + "|Positive" });
        juce::String error;
        const auto back = readExcellon(folder.getChildFile(fileName).loadFileAsString(), error);
        if (error.isEmpty()) error = compareHoles(holes, back);
        if (error.isEmpty())
            result.checks.add(fileName + ": " + juce::String((int)back.size()) + " holes read back, all match the board.");
        else
        {
            result.checks.add(fileName + ": MISMATCH - " + error);
            result.problems.add(fileName + " does not read back as the board: " + error);
            allOk = false;
        }
    }

    // ---- IPC-D-356A netlist.
    {
        std::vector<TestPoint> points;
        for (const auto& pad : pads)
        {
            TestPoint t;
            t.through = pad.drill > 0.0;
            t.net = pad.net;
            t.refdes = pad.refdes;
            t.pin = pad.number;
            t.drill = pad.drill;
            t.access = t.through ? 0 : 1;
            t.at = pad.centre;
            t.w = pad.w;
            t.h = pad.round ? pad.w : pad.h;
            points.push_back(t);
        }
        const auto fileName = name + "-netlist.ipc";
        if (writeFile(fileName, writeIpc(points, name)))
        {
            juce::String error;
            const auto back = readIpc(folder.getChildFile(fileName).loadFileAsString(), error);
            if (error.isEmpty()) error = compareIpc(points, back);
            for (const auto& pad : pads)
                if (pad.refdes.length() > 6 || pad.net.removeCharacters(" ").length() > 14)
                    result.checks.add("Warning: " + pad.refdes + "." + pad.number + " has a name longer than IPC-D-356 allows; it is shortened.");
            if (error.isEmpty())
                result.checks.add(fileName + ": " + juce::String((int)back.size()) + " test points read back, all match the board's pads and nets.");
            else
            {
                result.checks.add(fileName + ": MISMATCH - " + error);
                result.problems.add(fileName + " does not read back as the board: " + error);
                allOk = false;
            }
        }
    }

    // ---- Assembly: pick and place, BOM.
    {
        juce::String pos = "Designator,Value,Package,Mid X (mm),Mid Y (mm),Rotation,Layer\n";
        for (const auto& p : layout.parts)
            pos << csvField(p.refdes) << "," << csvField(p.value) << "," << csvField(p.footprint) << "," << juce::String(p.at.x, 4) << ","
                << juce::String(p.at.y, 4) << "," << p.rotation << ",Top\n";
        writeFile(name + "-pos.csv", pos);

        std::map<juce::String, juce::StringArray> groups;
        for (const auto& p : layout.parts) groups[p.value + "\x1f" + p.footprint].add(p.refdes);
        juce::String bom = "Comment,Designator,Footprint,Quantity\n";
        for (auto& [key, refs] : groups)
        {
            refs.sortNatural();
            bom << csvField(key.upToFirstOccurrenceOf("\x1f", false, false)) << "," << csvField(refs.joinIntoString(",")) << ","
                << csvField(key.fromFirstOccurrenceOf("\x1f", false, false)) << "," << refs.size() << "\n";
        }
        writeFile(name + "-bom.csv", bom);
        result.checks.add(name + "-pos.csv / -bom.csv: " + juce::String((int)layout.parts.size()) + " parts in " + juce::String((int)groups.size()) + " BOM lines.");
    }

    // ---- Gerber job file.
    {
        auto* root = new juce::DynamicObject();
        auto* header = new juce::DynamicObject();
        auto* gen = new juce::DynamicObject();
        gen->setProperty("Vendor", vendor);
        gen->setProperty("Application", application);
        gen->setProperty("Version", version);
        header->setProperty("GenerationSoftware", juce::var(gen));
        header->setProperty("CreationDate", date);
        root->setProperty("Header", juce::var(header));
        auto* specs = new juce::DynamicObject();
        auto* project = new juce::DynamicObject();
        project->setProperty("Name", rawName);
        project->setProperty("GUID", juce::Uuid().toDashedString());
        project->setProperty("Revision", "1");
        specs->setProperty("ProjectId", juce::var(project));
        auto* size = new juce::DynamicObject();
        const auto b = board.bounds();
        size->setProperty("X", b.getWidth());
        size->setProperty("Y", b.getHeight());
        specs->setProperty("Size", juce::var(size));
        specs->setProperty("LayerNumber", layers);
        specs->setProperty("BoardThickness", board.thickness);
        root->setProperty("GeneralSpecs", juce::var(specs));
        auto* rules = new juce::DynamicObject();
        rules->setProperty("Layers", "All");
        rules->setProperty("PadToPad", layout.rules.clearance);
        rules->setProperty("PadToTrack", layout.rules.clearance);
        rules->setProperty("TrackToTrack", layout.rules.clearance);
        rules->setProperty("MinLineWidth", layout.rules.trackWidth);
        juce::Array<juce::var> ruleList { juce::var(rules) };
        root->setProperty("DesignRules", ruleList);
        juce::Array<juce::var> fileList;
        for (const auto& [path, attrs] : jobFiles)
        {
            auto* f = new juce::DynamicObject();
            f->setProperty("Path", path);
            f->setProperty("FileFunction", attrs.upToFirstOccurrenceOf("|", false, false));
            f->setProperty("FilePolarity", attrs.fromFirstOccurrenceOf("|", false, false));
            fileList.add(juce::var(f));
        }
        root->setProperty("FilesAttributes", fileList);
        const auto fileName = name + "-job.gbrjob";
        if (writeFile(fileName, juce::JSON::toString(juce::var(root), false)))
        {
            const auto back = juce::JSON::parse(folder.getChildFile(fileName).loadFileAsString());
            int found = 0, listed = 0;
            if (const auto* list = back.getProperty("FilesAttributes", {}).getArray())
                for (const auto& f : *list)
                {
                    ++listed;
                    if (folder.getChildFile(f.getProperty("Path", "").toString()).existsAsFile()) ++found;
                }
            if (listed == (int)jobFiles.size() && found == listed && (int)back.getProperty("GeneralSpecs", {}).getProperty("LayerNumber", 0) == layers)
                result.checks.add(fileName + ": lists " + juce::String(listed) + " files, all present; " + juce::String(layers) + " copper layers.");
            else
            {
                result.checks.add(fileName + ": MISMATCH - " + juce::String(found) + " of " + juce::String(listed) + " listed files present");
                result.problems.add(fileName + " is inconsistent.");
                allOk = false;
            }
        }
    }

    for (const auto& w : artworkWarnings(layout)) result.checks.add("Warning: " + w);

    // ---- Zip of everything.
    {
        juce::ZipFile::Builder zip;
        for (const auto& f : result.files) zip.addFile(folder.getChildFile(f), 9, f);
        result.zip = folder.getChildFile(name + "-fab.zip");
        result.zip.deleteFile();
        juce::FileOutputStream stream(result.zip);
        if (!stream.openedOk() || !zip.writeToStream(stream, nullptr))
        {
            result.problems.add("Could not write the zip.");
            allOk = false;
        }
    }

    result.ok = allOk;
    juce::StringArray notReady;
    if (!layout.routed) notReady.add("the board is not routed");
    else if (layout.routedConnections < layout.connections)
        notReady.add(juce::String(layout.connections - layout.routedConnections) + " connection(s) are not routed");
    if (!layout.violations.empty()) notReady.add(juce::String((int)layout.violations.size()) + " design-rule violation(s)");
    if (!schematicMatches) notReady.add("the board does not match the schematic (" + schematicSummary + ")");
    result.readyForFab = allOk && notReady.isEmpty();
    for (const auto& n : notReady) result.problems.add("Not ready for fab: " + n + ".");
    result.summary = !allOk ? "Export FAILED its read-back check - do not send these files."
                   : result.readyForFab ? "Exported " + juce::String(result.files.size()) + " files; every file read back and matches the board. Ready for fab."
                                        : "Exported " + juce::String(result.files.size()) + " files, which read back correctly, but the board is NOT ready for fab: "
                                              + notReady.joinIntoString("; ") + ".";
    return result;
}
}
