#include "PcbLayout.h"

#include <djehuti_route/drc.h>
#include <djehuti_route/router.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace dr = djehuti::route;

namespace pcb
{
namespace
{
// ---- Footprint library ------------------------------------------------------

PadDef smd(const juce::String& n, double x, double y, double w, double h) { return { n, {}, x, y, w, h, false, 0.0 }; }
PadDef tht(const juce::String& n, double x, double y, double d, double drill) { return { n, {}, x, y, d, d, true, drill }; }

std::vector<Footprint> makeFootprints()
{
    std::vector<Footprint> f;
    auto add = [&](const juce::String& id, const juce::String& name, const juce::String& desc, std::vector<PadDef> pads, double cw, double ch) {
        f.push_back({ id, name, desc, std::move(pads), cw, ch });
    };
    add("0805", "0805 (2012 metric)", "SMD chip, 2.0 x 1.25 mm body: resistors, capacitors, small inductors, LEDs.",
        { smd("1", -0.95, 0, 1.0, 1.3), smd("2", 0.95, 0, 1.0, 1.3) }, 3.2, 1.9);
    add("1206", "1206 (3216 metric)", "SMD chip, 3.2 x 1.6 mm body: higher power or voltage resistors and capacitors, inductors, fuses.",
        { smd("1", -1.5, 0, 1.2, 1.8), smd("2", 1.5, 0, 1.2, 1.8) }, 4.4, 2.4);
    add("SOD-123", "SOD-123", "SMD diode package, pad 1 cathode.",
        { smd("1", -1.6, 0, 0.9, 1.2), smd("2", 1.6, 0, 0.9, 1.2) }, 4.6, 2.0);
    add("Axial-P10.16", "Axial, 10.16 mm pitch", "Through-hole axial lead part lying flat (1/4 W resistor, DO-41 diode, glass fuse), pad 1 cathode for diodes.",
        { tht("1", -5.08, 0, 1.6, 0.8), tht("2", 5.08, 0, 1.6, 0.8) }, 12.4, 3.0);
    add("Disc-P5.0", "Ceramic disc, 5 mm pitch", "Through-hole ceramic or film capacitor.",
        { tht("1", -2.5, 0, 1.6, 0.8), tht("2", 2.5, 0, 1.6, 0.8) }, 7.0, 3.5);
    add("Radial-D5-P2.0", "Radial electrolytic, 5 mm diameter", "Through-hole electrolytic capacitor, 2 mm lead pitch, pad 1 positive.",
        { tht("1", -1.0, 0, 1.6, 0.8), tht("2", 1.0, 0, 1.6, 0.8) }, 5.6, 5.6);
    add("Inductor-Radial-D8-P5.0", "Radial inductor, 8 mm", "Through-hole drum-core inductor, 5 mm lead pitch.",
        { tht("1", -2.5, 0, 1.8, 1.0), tht("2", 2.5, 0, 1.8, 1.0) }, 8.6, 8.6);
    add("LED-THT-3mm", "LED, 3 mm through-hole", "T-1 LED, 2.54 mm pitch, pad 1 cathode.",
        { tht("1", -1.27, 0, 1.8, 0.9), tht("2", 1.27, 0, 1.8, 0.9) }, 4.2, 4.0);
    add("SOT-23", "SOT-23", "SMD 3-pin transistor package.",
        { smd("1", -0.95, -1.1, 0.6, 0.9), smd("2", 0.95, -1.1, 0.6, 0.9), smd("3", 0, 1.1, 0.6, 0.9) }, 3.4, 3.6);
    add("SOT-23-5", "SOT-23-5", "SMD 5-pin package (single logic gate: 1 A, 2 B, 3 GND, 4 Y, 5 VCC).",
        { smd("1", -0.95, -1.2, 0.6, 0.9), smd("2", 0, -1.2, 0.6, 0.9), smd("3", 0.95, -1.2, 0.6, 0.9),
          smd("4", 0.95, 1.2, 0.6, 0.9), smd("5", -0.95, 1.2, 0.6, 0.9) }, 3.4, 3.8);
    add("TO-92", "TO-92, 2.54 mm pitch", "Through-hole small-signal transistor, leads formed to 2.54 mm.",
        { tht("1", -2.54, 0, 1.5, 0.8), tht("2", 0, 0, 1.5, 0.8), tht("3", 2.54, 0, 1.5, 0.8) }, 7.0, 4.5);
    add("TO-220-3", "TO-220, vertical", "Through-hole power transistor, 2.54 mm pitch.",
        { tht("1", -2.54, 0, 1.9, 1.1), tht("2", 0, 0, 1.9, 1.1), tht("3", 2.54, 0, 1.9, 1.1) }, 10.6, 5.0);
    {
        std::vector<PadDef> soic, dip;
        for (int k = 0; k < 4; ++k)
        {
            soic.push_back(smd(juce::String(k + 1), -2.7, 1.905 - 1.27 * k, 1.55, 0.6));
            dip.push_back(tht(juce::String(k + 1), -3.81, 3.81 - 2.54 * k, 1.6, 0.8));
        }
        for (int k = 0; k < 4; ++k)
        {
            soic.push_back(smd(juce::String(k + 5), 2.7, -1.905 + 1.27 * k, 1.55, 0.6));
            dip.push_back(tht(juce::String(k + 5), 3.81, -3.81 + 2.54 * k, 1.6, 0.8));
        }
        add("SOIC-8", "SOIC-8", "SMD 8-pin, 1.27 mm pitch (single op amp: 2 IN-, 3 IN+, 4 V-, 6 OUT, 7 V+).", soic, 7.4, 5.4);
        add("DIP-8", "DIP-8", "Through-hole 8-pin, 2.54 mm pitch, 7.62 mm rows.", dip, 10.0, 10.6);
    }
    add("Header-1x2-P2.54", "Pin header 1x2", "2.54 mm pin header: off-board connections (supplies, signal inputs, batteries).",
        { tht("1", 0, 1.27, 1.7, 1.0), tht("2", 0, -1.27, 1.7, 1.0) }, 3.0, 5.6);
    add("Header-1x3-P2.54", "Pin header 1x3", "2.54 mm pin header.",
        { tht("1", 0, 2.54, 1.7, 1.0), tht("2", 0, 0, 1.7, 1.0), tht("3", 0, -2.54, 1.7, 1.0) }, 3.0, 8.1);
    add("Trimmer-3296W", "Trimmer, 3296W", "Through-hole multiturn trimmer, pins 1, wiper, 3 in line.",
        { tht("1", -2.54, 0, 1.6, 0.8), tht("2", 0, 0, 1.6, 0.8), tht("3", 2.54, 0, 1.6, 0.8) }, 10.0, 5.0);
    add("Trimmer-Cap-P5.0", "Trimmer capacitor", "Through-hole trimmer capacitor, 5 mm pitch.",
        { tht("1", -2.5, 0, 1.6, 0.8), tht("2", 2.5, 0, 1.6, 0.8) }, 7.0, 5.5);
    add("Switch-THT-P5.0", "Switch, 2-pin through-hole", "Through-hole toggle or push switch, 5 mm pitch.",
        { tht("1", -2.5, 0, 1.8, 1.0), tht("2", 2.5, 0, 1.8, 1.0) }, 7.0, 4.0);
    add("Slide-SPDT-P2.54", "Slide switch SPDT", "Through-hole slide switch, common in the middle.",
        { tht("1", -2.54, 0, 1.6, 0.9), tht("2", 0, 0, 1.6, 0.9), tht("3", 2.54, 0, 1.6, 0.9) }, 9.0, 4.5);
    add("Relay-4pin", "Relay, SPST 4-pin", "Through-hole signal relay: coil 1-2, contact 3-4.",
        { tht("1", -3.81, 2.54, 1.8, 1.0), tht("2", -3.81, -2.54, 1.8, 1.0), tht("3", 3.81, 2.54, 1.8, 1.0), tht("4", 3.81, -2.54, 1.8, 1.0) }, 12.5, 8.5);
    add("Transformer-4pin", "Transformer, 4-pin", "Through-hole small transformer or coupled inductor: primary 1-2, secondary 3-4.",
        { tht("1", -5.08, 3.81, 1.9, 1.1), tht("2", -5.08, -3.81, 1.9, 1.1), tht("3", 5.08, 3.81, 1.9, 1.1), tht("4", 5.08, -3.81, 1.9, 1.1) }, 14.0, 12.0);
    add("TestPoint-THT", "Test point", "Through-hole test point loop.",
        { tht("1", 0, 0, 1.8, 1.0) }, 2.6, 2.6);
    return f;
}

// Which footprints fit which symbol, and which pin each pad carries
// ("1=B,2=E,3=C"). The first entry is the default.
struct Fit
{
    juce::String footprint, pins;
};

const std::map<juce::String, std::vector<Fit>>& fits()
{
    static const std::map<juce::String, std::vector<Fit>> table = [] {
        std::map<juce::String, std::vector<Fit>> t;
        const Fit two0805 { "0805", "1=1,2=2" }, two1206 { "1206", "1=1,2=2" };
        t["resistor"] = { two0805, two1206, { "Axial-P10.16", "1=1,2=2" } };
        t["potentiometer"] = { { "Trimmer-3296W", "1=1,2=W,3=2" } };
        t["capacitor"] = { two0805, two1206, { "Disc-P5.0", "1=1,2=2" } };
        t["capacitor_polarized"] = { { "Radial-D5-P2.0", "1=+,2=-" }, { "1206", "1=+,2=-" } };
        t["variable_capacitor"] = { { "Trimmer-Cap-P5.0", "1=1,2=2" } };
        t["inductor"] = { two1206, two0805, { "Inductor-Radial-D8-P5.0", "1=1,2=2" } };
        t["coupled_inductor"] = { { "Transformer-4pin", "1=1A,2=1B,3=2A,4=2B" } };
        t["transformer"] = { { "Transformer-4pin", "1=P1,2=P2,3=S1,4=S2" } };
        for (const auto* d : { "diode", "zener_diode", "schottky_diode" })
            t[d] = { { "SOD-123", "1=K,2=A" }, { "Axial-P10.16", "1=K,2=A" } };
        t["led"] = { { "0805", "1=K,2=A" }, { "LED-THT-3mm", "1=K,2=A" } };
        for (const auto* s : { "battery", "voltage_source", "ac_voltage_source", "current_source", "ac_current_source" })
            t[s] = { { "Header-1x2-P2.54", "1=+,2=-" } };
        t["signal_source"] = { { "Header-1x2-P2.54", "1=OUT,2=REF" } };
        t["opamp_741"] = { { "SOIC-8", "2=IN-,3=IN+,4=V-,6=OUT,7=V+" }, { "DIP-8", "2=IN-,3=IN+,4=V-,6=OUT,7=V+" } };
        t["npn"] = t["pnp"] = { { "SOT-23", "1=B,2=E,3=C" }, { "TO-92", "1=E,2=B,3=C" } };
        t["nmos"] = t["pmos"] = { { "SOT-23", "1=G,2=S,3=D" }, { "TO-220-3", "1=G,2=D,3=S" } };
        t["njfet"] = t["pjfet"] = { { "SOT-23", "1=D,2=S,3=G" }, { "TO-92", "1=D,2=S,3=G" } };
        t["switch_spst"] = { { "Switch-THT-P5.0", "1=1,2=2" } };
        t["switch_spdt"] = { { "Slide-SPDT-P2.54", "1=A,2=C,3=B" } };
        t["relay_spst"] = { { "Relay-4pin", "1=COIL+,2=COIL-,3=1,4=2" } };
        t["fuse"] = { two1206, { "Axial-P10.16", "1=1,2=2" } };
        t["connector_2"] = { { "Header-1x2-P2.54", "1=1,2=2" } };
        t["connector_3"] = { { "Header-1x3-P2.54", "1=1,2=2,3=3" } };
        t["test_point"] = { { "TestPoint-THT", "1=1" } };
        t["logic_not"] = { { "SOT-23-5", "2=A,4=Y" } };
        for (const auto* g : { "logic_and", "logic_or", "logic_nand", "logic_nor", "logic_xor" })
            t[g] = { { "SOT-23-5", "1=A,2=B,4=Y" } };
        return t;
    }();
    return table;
}

std::map<juce::String, juce::String> pinMap(const juce::String& symbolId, const juce::String& footprint)
{
    std::map<juce::String, juce::String> m;
    const auto found = fits().find(symbolId);
    if (found == fits().end()) return m;
    for (const auto& fit : found->second)
        if (fit.footprint == footprint)
        {
            for (const auto& pair : juce::StringArray::fromTokens(fit.pins, ",", {}))
                m[pair.upToFirstOccurrenceOf("=", false, false)] = pair.fromFirstOccurrenceOf("=", false, false);
            break;
        }
    return m;
}

// ---- Geometry (mm) ----------------------------------------------------------

bool inPolygon(const std::vector<Point>& poly, Point p)
{
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > p.y) != (poly[j].y > p.y)
            && p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
            inside = !inside;
    return inside;
}

double pointSeg(Point p, Point a, Point b)
{
    const auto d = b - a;
    const double len2 = d.x * d.x + d.y * d.y;
    const double t = len2 > 0.0 ? juce::jlimit(0.0, 1.0, ((p.x - a.x) * d.x + (p.y - a.y) * d.y) / len2) : 0.0;
    return p.getDistanceFrom({ a.x + t * d.x, a.y + t * d.y });
}

bool segmentsCross(Point a, Point b, Point c, Point d)
{
    auto cross = [](Point o, Point p, Point q) { return (p.x - o.x) * (q.y - o.y) - (p.y - o.y) * (q.x - o.x); };
    const double d1 = cross(c, d, a), d2 = cross(c, d, b), d3 = cross(a, b, c), d4 = cross(a, b, d);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

double segSeg(Point a, Point b, Point c, Point d)
{
    if (segmentsCross(a, b, c, d)) return 0.0;
    return std::min({ pointSeg(a, c, d), pointSeg(b, c, d), pointSeg(c, a, b), pointSeg(d, a, b) });
}

std::vector<Point> corners(const juce::Rectangle<double>& r)
{
    return { { r.getX(), r.getY() }, { r.getRight(), r.getY() }, { r.getRight(), r.getBottom() }, { r.getX(), r.getBottom() } };
}

// Smallest distance from the rectangle's boundary to a polygon's edges.
double rectPolygonGap(const juce::Rectangle<double>& r, const std::vector<Point>& poly)
{
    const auto c = corners(r);
    double best = std::numeric_limits<double>::max();
    for (size_t i = 0; i < 4; ++i)
        for (size_t k = 0; k < poly.size(); ++k)
            best = std::min(best, segSeg(c[i], c[(i + 1) % 4], poly[k], poly[(k + 1) % poly.size()]));
    return best;
}

double rectPointGap(const juce::Rectangle<double>& r, Point p)
{
    const double dx = std::max({ r.getX() - p.x, 0.0, p.x - r.getRight() });
    const double dy = std::max({ r.getY() - p.y, 0.0, p.y - r.getBottom() });
    return std::sqrt(dx * dx + dy * dy);
}

Point rotated(double x, double y, int rotation)
{
    switch (((rotation % 360) + 360) % 360)
    {
        case 90: return { -y, x };
        case 180: return { -x, -y };
        case 270: return { y, -x };
        default: return { x, y };
    }
}

// Where the courtyard may go: inside the outline, out of cutouts and holes,
// `margin` from every board edge.
bool fitsBoard(const juce::Rectangle<double>& r, const BoardDesign& board, double margin)
{
    if (board.outline.size() < 3) return false;
    for (const auto& c : corners(r))
        if (!inPolygon(board.outline, c)) return false;
    if (rectPolygonGap(r, board.outline) < margin) return false;
    for (const auto& cut : board.cutouts)
    {
        if (cut.size() < 3) continue;
        if (inPolygon(cut, r.getCentre()) || r.contains(cut[0]) || rectPolygonGap(r, cut) < margin) return false;
    }
    for (const auto& h : board.holes)
        if (rectPointGap(r, h.centre) < h.diameter / 2.0 + margin) return false;
    return true;
}

double spacingOf(const Layout& layout)
{
    // Room for a couple of tracks between neighbouring parts.
    return std::max(1.0, 3.0 * (layout.rules.trackWidth + layout.rules.clearance));
}

juce::Rectangle<double> courtyardAt(const Footprint& fp, Point at, int rotation)
{
    const bool quarter = rotation == 90 || rotation == 270;
    const double w = quarter ? fp.courtyardH : fp.courtyardW, h = quarter ? fp.courtyardW : fp.courtyardH;
    return { at.x - w / 2.0, at.y - h / 2.0, w, h };
}

int connectionCount(const PlacedPart& a, const PlacedPart& b)
{
    int n = 0;
    for (const auto& [pin, net] : a.pinNets)
    {
        if (net.isEmpty()) continue;
        for (const auto& [pin2, net2] : b.pinNets)
            if (net2 == net) { ++n; break; }
    }
    return n;
}

// Best legal spot for part `index` given the parts already placed (`placed`).
bool bestSpot(const Layout& layout, int index, const std::vector<bool>& placed, const BoardDesign& board, Point& at, int& rotation)
{
    const auto& part = layout.parts[(size_t)index];
    const auto* fp = findFootprint(part.footprint);
    if (fp == nullptr || board.outline.size() < 3) return false;
    const double gap = spacingOf(layout);
    const double margin = board.edgeClearance + 0.5;

    std::vector<juce::Rectangle<double>> taken;
    std::map<juce::String, std::vector<Point>> placedPads;
    for (size_t i = 0; i < layout.parts.size(); ++i)
    {
        if (!placed[i] || (int)i == index) continue;
        taken.push_back(courtyardOf(layout.parts[i]).expanded(gap));
        for (const auto& pad : padsOf(layout.parts[i]))
            if (pad.net.isNotEmpty()) placedPads[pad.net].push_back(pad.centre);
    }

    const auto bounds = board.bounds();
    const auto centroid = bounds.getCentre();
    const double step = 1.27;
    double bestCost = std::numeric_limits<double>::max();
    bool found = false;
    for (int rot : { 0, 90, 180, 270 })
    {
        // Pad offsets at this rotation, with their nets.
        std::vector<std::pair<Point, juce::String>> offsets;
        PlacedPart probe = part;
        probe.at = {};
        probe.rotation = rot;
        for (const auto& pad : padsOf(probe))
            offsets.push_back({ pad.centre, pad.net });
        for (double y = std::floor(bounds.getY() / step) * step; y <= bounds.getBottom(); y += step)
            for (double x = std::floor(bounds.getX() / step) * step; x <= bounds.getRight(); x += step)
            {
                const Point c { x, y };
                const auto r = courtyardAt(*fp, c, rot);
                if (!bounds.contains(r)) continue;
                bool clash = false;
                for (const auto& t : taken)
                    if (t.intersects(r)) { clash = true; break; }
                if (clash || !fitsBoard(r, board, margin)) continue;
                double cost = 0.05 * c.getDistanceFrom(centroid);
                for (const auto& [offset, net] : offsets)
                {
                    if (net.isEmpty()) continue;
                    const auto it = placedPads.find(net);
                    if (it == placedPads.end()) continue;
                    double nearest = std::numeric_limits<double>::max();
                    for (const auto& q : it->second) nearest = std::min(nearest, (c + offset).getDistanceFrom(q));
                    cost += nearest;
                }
                // Prefer the plain orientation when it is as good.
                cost += rot == 0 ? 0.0 : 0.01;
                if (cost < bestCost)
                {
                    bestCost = cost;
                    at = c;
                    rotation = rot;
                    found = true;
                }
            }
    }
    return found;
}

// Off the board to the right, stacked, for parts that did not fit.
void parkOffBoard(Layout& layout, int index, const BoardDesign& board, double& parkY)
{
    auto& part = layout.parts[(size_t)index];
    const auto* fp = findFootprint(part.footprint);
    const auto b = board.bounds();
    const double w = fp != nullptr ? fp->courtyardW : 4.0, h = fp != nullptr ? fp->courtyardH : 4.0;
    part.rotation = 0;
    part.at = { b.getRight() + 6.0 + w / 2.0, parkY - h / 2.0 };
    parkY -= h + 2.0;
}

dr::Shape shapeOf(const BoardPad& pad)
{
    dr::Shape s;
    s.kind = pad.round ? dr::Shape::Kind::Circle : dr::Shape::Kind::Rectangle;
    s.centre = { dr::mm(pad.centre.x), dr::mm(pad.centre.y) };
    s.sizeX = dr::mm(pad.w);
    s.sizeY = dr::mm(pad.round ? pad.w : pad.h);
    return s;
}

// The route board for this layout, and the net names by index.
dr::Board routeBoardOf(const Layout& layout, const BoardDesign& board, juce::StringArray& nets)
{
    auto b = board.toRouteBoard();
    b.netClasses[0].traceWidth = dr::mm(layout.rules.trackWidth);
    b.netClasses[0].clearance = dr::mm(layout.rules.clearance);
    b.netClasses[0].viaDiameter = dr::mm(layout.rules.viaDiameter);
    b.netClasses[0].viaDrill = dr::mm(layout.rules.viaDrill);
    nets = netNames(layout);
    for (const auto& n : nets) b.addNet(n.toStdString());
    for (const auto& pad : allPads(layout))
    {
        dr::Pad p;
        p.reference = (pad.refdes + "." + pad.number).toStdString();
        p.shape = shapeOf(pad);
        p.net = pad.net.isEmpty() ? -1 : nets.indexOf(pad.net);
        p.firstLayer = 0;
        p.lastLayer = pad.drill > 0.0 ? b.layerCount() - 1 : 0;
        p.drill = dr::mm(pad.drill);
        b.pads.push_back(p);
    }
    return b;
}

juce::String padLabel(const std::string& reference, const Layout& layout)
{
    // "R1.2" -> "R1.2 (pin 2)" with the schematic pin name where it differs.
    const auto ref = juce::String(reference);
    const auto refdes = ref.upToLastOccurrenceOf(".", false, false), number = ref.fromLastOccurrenceOf(".", false, false);
    if (const auto* part = layout.find(refdes))
        for (const auto& pad : padsOf(*part))
            if (pad.number == number && pad.pin.isNotEmpty() && pad.pin != number)
                return ref + " (" + pad.pin + ")";
    return ref;
}

std::vector<Marker> toMarkers(const std::vector<dr::Violation>& list, const juce::StringArray& nets)
{
    std::vector<Marker> out;
    for (const auto& v : list)
    {
        Marker m;
        m.kind = dr::kindName(v.kind);
        auto netName = [&](int n) { return n >= 0 && n < nets.size() ? nets[n] : juce::String("no net"); };
        m.message = m.kind + ": " + juce::String(v.message);
        if (v.netA >= 0 || v.netB >= 0)
            m.message << " [" << netName(v.netA) << (v.netB >= 0 ? " / " + netName(v.netB) : juce::String()) << "]";
        m.at = { dr::toMm(v.at.x), dr::toMm(v.at.y) };
        m.located = true;
        out.push_back(m);
    }
    return out;
}

juce::var pointVar(Point p)
{
    juce::Array<juce::var> a { p.x, p.y };
    return a;
}

Point varPoint(const juce::var& v)
{
    if (const auto* a = v.getArray(); a != nullptr && a->size() >= 2) return { (double)(*a)[0], (double)(*a)[1] };
    return {};
}
}

// ---- Footprints -------------------------------------------------------------

const std::vector<Footprint>& footprints()
{
    static const auto list = makeFootprints();
    return list;
}

const Footprint* findFootprint(const juce::String& id)
{
    for (const auto& f : footprints())
        if (f.id.equalsIgnoreCase(id)) return &f;
    return nullptr;
}

juce::StringArray footprintsFor(const juce::String& symbolId)
{
    juce::StringArray ids;
    if (const auto found = fits().find(symbolId); found != fits().end())
        for (const auto& fit : found->second) ids.add(fit.footprint);
    return ids;
}

juce::String defaultFootprint(const juce::String& symbolId)
{
    const auto ids = footprintsFor(symbolId);
    return ids.isEmpty() ? juce::String() : ids[0];
}

juce::String notOnBoardReason(const juce::String& symbolId)
{
    if (symbolId == "oscilloscope_2ch" || symbolId == "bode_analyzer" || symbolId == "digital_multimeter")
        return "test equipment, not a board part";
    if (symbolId == "vcvs" || symbolId == "vccs" || symbolId == "ccvs" || symbolId == "cccs")
        return "an ideal controlled source has no physical part";
    if (fits().find(symbolId) == fits().end())
        return "no footprint for " + symbolId;
    return {};
}

// ---- Layout -----------------------------------------------------------------

void Layout::clearRoute()
{
    routed = false;
    tracks.clear();
    vias.clear();
    connections = routedConnections = iterations = 0;
    seconds = 0.0;
    unrouted.clear();
    violations.clear();
    routeError.clear();
}

const PlacedPart* Layout::find(const juce::String& refdes) const
{
    for (const auto& p : parts)
        if (p.refdes.equalsIgnoreCase(refdes)) return &p;
    return nullptr;
}

PlacedPart* Layout::find(const juce::String& refdes)
{
    for (auto& p : parts)
        if (p.refdes.equalsIgnoreCase(refdes)) return &p;
    return nullptr;
}

std::vector<BoardPad> padsOf(const PlacedPart& part)
{
    std::vector<BoardPad> pads;
    const auto* fp = findFootprint(part.footprint);
    if (fp == nullptr) return pads;
    const auto map = pinMap(part.symbolId, part.footprint);
    const bool quarter = part.rotation == 90 || part.rotation == 270;
    for (const auto& d : fp->pads)
    {
        BoardPad p;
        p.refdes = part.refdes;
        p.number = d.number;
        if (const auto m = map.find(d.number); m != map.end())
        {
            p.pin = m->second;
            if (const auto n = part.pinNets.find(p.pin); n != part.pinNets.end()) p.net = n->second;
        }
        p.centre = part.at + rotated(d.x, d.y, part.rotation);
        p.w = quarter ? d.h : d.w;
        p.h = quarter ? d.w : d.h;
        p.round = d.round;
        p.drill = d.drill;
        pads.push_back(p);
    }
    return pads;
}

std::vector<BoardPad> allPads(const Layout& layout)
{
    std::vector<BoardPad> pads;
    for (const auto& part : layout.parts)
        for (auto& p : padsOf(part)) pads.push_back(std::move(p));
    return pads;
}

juce::Rectangle<double> courtyardOf(const PlacedPart& part)
{
    if (const auto* fp = findFootprint(part.footprint)) return courtyardAt(*fp, part.at, part.rotation);
    return { part.at.x - 1.0, part.at.y - 1.0, 2.0, 2.0 };
}

juce::StringArray netNames(const Layout& layout)
{
    // Nets with at least one pad, in a stable order.
    juce::StringArray nets;
    for (const auto& pad : allPads(layout))
        if (pad.net.isNotEmpty()) nets.addIfNotAlreadyThere(pad.net);
    nets.sortNatural();
    return nets;
}

juce::StringArray placementProblems(const Layout& layout, const BoardDesign& board)
{
    juce::StringArray out;
    for (size_t i = 0; i < layout.parts.size(); ++i)
    {
        const auto& part = layout.parts[i];
        const auto r = courtyardOf(part);
        if (!fitsBoard(r, board, board.edgeClearance))
            out.add(part.refdes + " is off the board or over its edge, a hole or a cutout.");
        for (size_t k = i + 1; k < layout.parts.size(); ++k)
            if (r.intersects(courtyardOf(layout.parts[k])))
                out.add(part.refdes + " overlaps " + layout.parts[k].refdes + ".");
    }
    return out;
}

SyncReport syncFromSchematic(Layout& layout, const std::vector<SchematicPart>& parts, const BoardDesign& board)
{
    SyncReport report;
    std::set<juce::String> wanted;
    std::vector<int> fresh;
    for (const auto& sp : parts)
    {
        const auto reason = notOnBoardReason(sp.symbolId);
        if (reason.isNotEmpty())
        {
            report.skipped.add(sp.refdes + ": " + reason);
            continue;
        }
        wanted.insert(sp.refdes.toLowerCase());
        std::map<juce::String, juce::String> nets;
        for (const auto& [pin, net] : sp.pins) nets[pin] = net;
        if (auto* existing = layout.find(sp.refdes))
        {
            const bool symbolChanged = existing->symbolId != sp.symbolId;
            if (symbolChanged || existing->pinNets != nets || existing->value != sp.value)
            {
                existing->symbolId = sp.symbolId;
                existing->value = sp.value;
                existing->pinNets = nets;
                if (symbolChanged || !footprintsFor(sp.symbolId).contains(existing->footprint))
                    existing->footprint = defaultFootprint(sp.symbolId);
                report.updated.add(sp.refdes);
            }
            continue;
        }
        PlacedPart p;
        p.refdes = sp.refdes;
        p.symbolId = sp.symbolId;
        p.value = sp.value;
        p.footprint = defaultFootprint(sp.symbolId);
        p.pinNets = nets;
        layout.parts.push_back(p);
        fresh.push_back((int)layout.parts.size() - 1);
        report.added.add(sp.refdes);
    }
    for (int i = (int)layout.parts.size() - 1; i >= 0; --i)
        if (wanted.count(layout.parts[(size_t)i].refdes.toLowerCase()) == 0)
        {
            report.removed.add(layout.parts[(size_t)i].refdes);
            layout.parts.erase(layout.parts.begin() + i);
            for (auto& f : fresh) if (f > i) --f;
        }
    if (!report.added.isEmpty() || !report.removed.isEmpty() || !report.updated.isEmpty())
        layout.clearRoute();

    if (fresh.size() == layout.parts.size() && !fresh.empty())
        autoPlace(layout, board); // a first sync: lay the whole board out
    else
        for (const auto i : fresh) placeOne(layout, i, board);
    return report;
}

bool placeOne(Layout& layout, int partIndex, const BoardDesign& board)
{
    std::vector<bool> placed(layout.parts.size(), true);
    // Parts parked off the board do not count as placed.
    for (size_t i = 0; i < layout.parts.size(); ++i)
        placed[i] = (int)i != partIndex && fitsBoard(courtyardOf(layout.parts[i]), board, 0.0);
    Point at;
    int rotation = 0;
    if (bestSpot(layout, partIndex, placed, board, at, rotation))
    {
        layout.parts[(size_t)partIndex].at = at;
        layout.parts[(size_t)partIndex].rotation = rotation;
        return true;
    }
    double parkY = board.bounds().getBottom();
    for (size_t i = 0; i < layout.parts.size(); ++i)
        if ((int)i != partIndex && !placed[i]) parkY = std::min(parkY, courtyardOf(layout.parts[i]).getY() - 2.0);
    parkOffBoard(layout, partIndex, board, parkY);
    return false;
}

juce::StringArray autoPlace(Layout& layout, const BoardDesign& board)
{
    juce::StringArray notPlaced;
    const int n = (int)layout.parts.size();
    std::vector<std::vector<int>> links((size_t)n, std::vector<int>((size_t)n, 0));
    std::vector<int> degree((size_t)n, 0);
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k)
            if (i != k)
            {
                links[(size_t)i][(size_t)k] = connectionCount(layout.parts[(size_t)i], layout.parts[(size_t)k]);
                degree[(size_t)i] += links[(size_t)i][(size_t)k];
            }
    auto area = [&](int i) { const auto r = courtyardOf(layout.parts[(size_t)i]); return r.getWidth() * r.getHeight(); };

    std::vector<bool> placed((size_t)n, false), done((size_t)n, false);
    double parkY = board.bounds().getBottom();
    layout.clearRoute();
    for (int step = 0; step < n; ++step)
    {
        // Next: most links to what is placed, then most links overall, then largest.
        int pick = -1;
        double best = -1.0;
        for (int i = 0; i < n; ++i)
        {
            if (done[(size_t)i]) continue;
            int toPlaced = 0;
            for (int k = 0; k < n; ++k)
                if (placed[(size_t)k]) toPlaced += links[(size_t)i][(size_t)k];
            const double score = toPlaced * 1e6 + degree[(size_t)i] * 1e3 + area(i);
            if (score > best) { best = score; pick = i; }
        }
        done[(size_t)pick] = true;
        Point at;
        int rotation = 0;
        if (bestSpot(layout, pick, placed, board, at, rotation))
        {
            layout.parts[(size_t)pick].at = at;
            layout.parts[(size_t)pick].rotation = rotation;
            placed[(size_t)pick] = true;
        }
        else
        {
            parkOffBoard(layout, pick, board, parkY);
            notPlaced.add(layout.parts[(size_t)pick].refdes);
        }
    }
    return notPlaced;
}

void routeLayout(Layout& layout, const BoardDesign& board)
{
    layout.clearRoute();
    if (!board.problems().isEmpty())
    {
        layout.routeError = "Fix the board first: " + board.problems().joinIntoString(" ");
        return;
    }
    if (layout.parts.empty())
    {
        layout.routeError = "No parts on the board; update them from the schematic first.";
        return;
    }
    const auto placement = placementProblems(layout, board);
    if (!placement.isEmpty())
    {
        layout.routeError = "Fix the placement first: " + placement.joinIntoString(" ");
        return;
    }
    juce::StringArray nets;
    const auto rb = routeBoardOf(layout, board, nets);
    const auto result = dr::routeBoard(rb);
    if (!result.error.empty())
    {
        layout.routeError = result.error;
        return;
    }
    for (const auto& t : result.tracks)
    {
        TrackMm track;
        track.net = t.net >= 0 && t.net < nets.size() ? nets[t.net] : juce::String();
        track.layer = t.layer;
        track.width = dr::toMm(t.width);
        for (const auto& p : t.points) track.points.push_back({ dr::toMm(p.x), dr::toMm(p.y) });
        layout.tracks.push_back(track);
    }
    for (const auto& v : result.vias)
        layout.vias.push_back({ v.net >= 0 && v.net < nets.size() ? nets[v.net] : juce::String(), { dr::toMm(v.at.x), dr::toMm(v.at.y) },
                                dr::toMm(v.diameter), dr::toMm(v.drill) });
    for (const auto& u : result.unrouted)
        layout.unrouted.add(padLabel(u.pad, layout) + " (net " + (u.net >= 0 && u.net < nets.size() ? nets[u.net] : juce::String("?")) + "): " + juce::String(u.reason));
    layout.connections = result.connections;
    layout.routedConnections = result.routedConnections;
    layout.iterations = result.iterations;
    layout.seconds = result.seconds;
    layout.violations = toMarkers(dr::checkDesignRules(rb, result), nets);
    layout.routed = true;
}

std::vector<Marker> checkLayout(const Layout& layout, const BoardDesign& board)
{
    juce::StringArray nets;
    const auto rb = routeBoardOf(layout, board, nets);
    dr::RouteResult r;
    for (const auto& t : layout.tracks)
    {
        dr::Track track;
        track.net = nets.indexOf(t.net);
        track.layer = t.layer;
        track.width = dr::mm(t.width);
        for (const auto& p : t.points) track.points.push_back({ dr::mm(p.x), dr::mm(p.y) });
        r.tracks.push_back(track);
    }
    for (const auto& v : layout.vias)
    {
        dr::Via via;
        via.net = nets.indexOf(v.net);
        via.at = { dr::mm(v.at.x), dr::mm(v.at.y) };
        via.diameter = dr::mm(v.diameter);
        via.drill = dr::mm(v.drill);
        via.firstLayer = 0;
        via.lastLayer = rb.layerCount() - 1;
        r.vias.push_back(via);
    }
    auto markers = toMarkers(dr::checkDesignRules(rb, r), nets);
    for (const auto& p : placementProblems(layout, board))
        markers.push_back({ "Placement", p, {}, false });
    return markers;
}

std::vector<std::pair<Point, Point>> ratsnest(const Layout& layout)
{
    std::set<juce::String> open;
    if (layout.routed)
        for (const auto& u : layout.unrouted)
            open.insert(u.fromFirstOccurrenceOf("(net ", false, false).upToFirstOccurrenceOf("):", false, false));
    std::map<juce::String, std::vector<Point>> byNet;
    for (const auto& pad : allPads(layout))
        if (pad.net.isNotEmpty() && (!layout.routed || open.count(pad.net) != 0))
            byNet[pad.net].push_back(pad.centre);
    std::vector<std::pair<Point, Point>> lines;
    for (const auto& [net, pts] : byNet)
    {
        // Prim's spanning tree over the pad centres.
        std::vector<bool> in(pts.size(), false);
        std::vector<double> dist(pts.size(), std::numeric_limits<double>::max());
        std::vector<int> from(pts.size(), -1);
        if (pts.size() < 2) continue;
        dist[0] = 0.0;
        for (size_t step = 0; step < pts.size(); ++step)
        {
            int u = -1;
            for (size_t i = 0; i < pts.size(); ++i)
                if (!in[i] && (u < 0 || dist[i] < dist[(size_t)u])) u = (int)i;
            in[(size_t)u] = true;
            if (from[(size_t)u] >= 0) lines.push_back({ pts[(size_t)from[(size_t)u]], pts[(size_t)u] });
            for (size_t i = 0; i < pts.size(); ++i)
            {
                const double d = pts[(size_t)u].getDistanceFrom(pts[i]);
                if (!in[i] && d < dist[i]) { dist[i] = d; from[i] = u; }
            }
        }
    }
    return lines;
}

// ---- JSON -------------------------------------------------------------------

juce::var Layout::toVar() const
{
    auto* root = new juce::DynamicObject();
    juce::Array<juce::var> partList;
    for (const auto& p : parts)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("refdes", p.refdes);
        o->setProperty("symbol", p.symbolId);
        o->setProperty("value", p.value);
        o->setProperty("footprint", p.footprint);
        o->setProperty("at", pointVar(p.at));
        o->setProperty("rotation", p.rotation);
        auto* nets = new juce::DynamicObject();
        for (const auto& [pin, net] : p.pinNets) nets->setProperty(pin, net);
        o->setProperty("pins", juce::var(nets));
        partList.add(juce::var(o));
    }
    root->setProperty("parts", partList);
    auto* r = new juce::DynamicObject();
    r->setProperty("track_width", rules.trackWidth);
    r->setProperty("clearance", rules.clearance);
    r->setProperty("via_diameter", rules.viaDiameter);
    r->setProperty("via_drill", rules.viaDrill);
    root->setProperty("rules", juce::var(r));
    if (routed)
    {
        auto* route = new juce::DynamicObject();
        juce::Array<juce::var> trackList, viaList;
        for (const auto& t : tracks)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("net", t.net);
            o->setProperty("layer", t.layer);
            o->setProperty("width", t.width);
            juce::Array<juce::var> pts;
            for (const auto& p : t.points) pts.add(pointVar(p));
            o->setProperty("points", pts);
            trackList.add(juce::var(o));
        }
        for (const auto& v : vias)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("net", v.net);
            o->setProperty("at", pointVar(v.at));
            o->setProperty("diameter", v.diameter);
            o->setProperty("drill", v.drill);
            viaList.add(juce::var(o));
        }
        route->setProperty("tracks", trackList);
        route->setProperty("vias", viaList);
        route->setProperty("connections", connections);
        route->setProperty("routed_connections", routedConnections);
        route->setProperty("iterations", iterations);
        route->setProperty("seconds", seconds);
        route->setProperty("unrouted", juce::var(unrouted));
        juce::Array<juce::var> drc;
        for (const auto& m : violations)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("kind", m.kind);
            o->setProperty("message", m.message);
            if (m.located) o->setProperty("at", pointVar(m.at));
            drc.add(juce::var(o));
        }
        route->setProperty("violations", drc);
        root->setProperty("route", juce::var(route));
    }
    return juce::var(root);
}

Layout Layout::fromVar(const juce::var& value)
{
    Layout l;
    if (const auto* list = value.getProperty("parts", {}).getArray())
        for (const auto& v : *list)
        {
            PlacedPart p;
            p.refdes = v.getProperty("refdes", "").toString();
            p.symbolId = v.getProperty("symbol", "").toString();
            p.value = v.getProperty("value", "").toString();
            p.footprint = v.getProperty("footprint", "").toString();
            if (findFootprint(p.footprint) == nullptr) p.footprint = defaultFootprint(p.symbolId);
            p.at = varPoint(v.getProperty("at", {}));
            p.rotation = ((int)v.getProperty("rotation", 0) % 360 + 360) % 360 / 90 * 90;
            if (const auto* nets = v.getProperty("pins", {}).getDynamicObject())
                for (const auto& prop : nets->getProperties())
                    p.pinNets[prop.name.toString()] = prop.value.toString();
            if (p.refdes.isNotEmpty()) l.parts.push_back(p);
        }
    const auto r = value.getProperty("rules", {});
    l.rules.trackWidth = (double)r.getProperty("track_width", l.rules.trackWidth);
    l.rules.clearance = (double)r.getProperty("clearance", l.rules.clearance);
    l.rules.viaDiameter = (double)r.getProperty("via_diameter", l.rules.viaDiameter);
    l.rules.viaDrill = (double)r.getProperty("via_drill", l.rules.viaDrill);
    const auto route = value.getProperty("route", {});
    if (route.isObject())
    {
        l.routed = true;
        if (const auto* list = route.getProperty("tracks", {}).getArray())
            for (const auto& v : *list)
            {
                TrackMm t;
                t.net = v.getProperty("net", "").toString();
                t.layer = (int)v.getProperty("layer", 0);
                t.width = (double)v.getProperty("width", 0.25);
                if (const auto* pts = v.getProperty("points", {}).getArray())
                    for (const auto& p : *pts) t.points.push_back(varPoint(p));
                l.tracks.push_back(t);
            }
        if (const auto* list = route.getProperty("vias", {}).getArray())
            for (const auto& v : *list)
                l.vias.push_back({ v.getProperty("net", "").toString(), varPoint(v.getProperty("at", {})),
                                   (double)v.getProperty("diameter", 0.6), (double)v.getProperty("drill", 0.3) });
        l.connections = (int)route.getProperty("connections", 0);
        l.routedConnections = (int)route.getProperty("routed_connections", 0);
        l.iterations = (int)route.getProperty("iterations", 0);
        l.seconds = (double)route.getProperty("seconds", 0.0);
        if (const auto* list = route.getProperty("unrouted", {}).getArray())
            for (const auto& v : *list) l.unrouted.add(v.toString());
        if (const auto* list = route.getProperty("violations", {}).getArray())
            for (const auto& v : *list)
            {
                Marker m;
                m.kind = v.getProperty("kind", "").toString();
                m.message = v.getProperty("message", "").toString();
                if (v.hasProperty("at")) { m.at = varPoint(v.getProperty("at", {})); m.located = true; }
                l.violations.push_back(m);
            }
    }
    return l;
}
}
