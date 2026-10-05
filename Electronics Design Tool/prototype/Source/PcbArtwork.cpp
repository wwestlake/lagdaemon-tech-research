#include "PcbArtwork.h"

#include "PcbFont.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace dr = djehuti::route;

namespace pcb
{
namespace
{
bool inPoly(const std::vector<Point>& poly, Point p)
{
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > p.y) != (poly[j].y > p.y)
            && p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
            inside = !inside;
    return inside;
}

double pointSegment(Point p, Point a, Point b)
{
    const auto d = b - a;
    const double len2 = d.x * d.x + d.y * d.y;
    const double t = len2 > 0.0 ? juce::jlimit(0.0, 1.0, ((p.x - a.x) * d.x + (p.y - a.y) * d.y) / len2) : 0.0;
    return p.getDistanceFrom({ a.x + t * d.x, a.y + t * d.y });
}

double edgeDistance(const BoardDesign& board, Point p)
{
    double best = 1e9;
    auto poly = [&](const std::vector<Point>& pts) {
        for (size_t i = 0; i < pts.size(); ++i) best = std::min(best, pointSegment(p, pts[i], pts[(i + 1) % pts.size()]));
    };
    poly(board.outline);
    for (const auto& c : board.cutouts) poly(c);
    for (const auto& h : board.holes) best = std::min(best, std::abs(p.getDistanceFrom(h.centre) - h.diameter / 2.0));
    return best;
}

// Rectangle around segment ab grown by `grow` on every side, as a polygon in nm.
dr::Polygon capsuleBox(Point a, Point b, double grow)
{
    auto d = b - a;
    double len = std::sqrt(d.x * d.x + d.y * d.y);
    Point u = len > 1e-9 ? Point { d.x / len, d.y / len } : Point { 1.0, 0.0 };
    const Point n { -u.y, u.x };
    const Point a2 = a - u * grow, b2 = b + u * grow;
    const Point pts[] { a2 + n * grow, b2 + n * grow, b2 - n * grow, a2 - n * grow };
    dr::Polygon poly;
    for (const auto& p : pts) poly.push_back({ dr::mm(p.x), dr::mm(p.y) });
    return poly;
}

void forEachSegment(const Artwork& art, const std::function<void(Point, Point, double)>& fn)
{
    for (const auto& s : art.strokes)
        for (size_t i = 1; i < s.points.size(); ++i) fn(s.points[i - 1], s.points[i], s.width);
    for (const auto& f : art.fills)
        for (size_t i = 0; i < f.size(); ++i) fn(f[i], f[(i + 1) % f.size()], 0.0);
}

bool symbolNeedsPin1(const juce::String& symbolId)
{
    static const juce::StringArray plain { "resistor", "capacitor", "inductor", "fuse", "switch_spst", "test_point", "variable_capacitor" };
    return !plain.contains(symbolId);
}
}

const juce::StringArray& artLayers()
{
    static const juce::StringArray layers { "F.SilkS", "B.SilkS", "F.Cu", "B.Cu" };
    return layers;
}

bool isBottomLayer(const juce::String& layer) { return layer.startsWith("B."); }

int copperLayerIndex(const juce::String& layer, int layerCount)
{
    if (layer == "F.Cu") return 0;
    if (layer == "B.Cu") return std::max(0, layerCount - 1);
    return -1;
}

std::vector<Point> arcPoints(Point centre, double radius, double startDeg, double endDeg)
{
    double sweep = endDeg - startDeg;
    if (std::abs(sweep) >= 360.0) sweep = sweep > 0 ? 360.0 : -360.0;
    // Chord sagitta r(1 - cos(step/2)) <= 0.01 mm.
    const double maxStep = radius > 0.01 ? 2.0 * std::acos(std::max(-1.0, 1.0 - 0.01 / radius)) : juce::MathConstants<double>::pi;
    const int n = std::max(8, (int)std::ceil(std::abs(juce::degreesToRadians(sweep)) / std::max(1e-3, maxStep)));
    std::vector<Point> pts;
    for (int i = 0; i <= n; ++i)
    {
        const double a = juce::degreesToRadians(startDeg + sweep * i / n);
        pts.push_back({ centre.x + radius * std::cos(a), centre.y + radius * std::sin(a) });
    }
    return pts;
}

Artwork textArtwork(const BoardText& t)
{
    Artwork art;
    for (auto& s : font::layout(t.text, t.at, t.height, t.rotation, t.align, isBottomLayer(t.layer)))
        art.strokes.push_back({ std::move(s), t.lineWidth });
    return art;
}

Artwork graphicArtwork(const BoardGraphic& g)
{
    Artwork art;
    std::vector<Point> outline;
    bool closed = true;
    if (g.kind == "line" && g.points.size() >= 2)
    {
        outline = { g.points[0], g.points[1] };
        closed = false;
    }
    else if (g.kind == "rect" && g.points.size() >= 2)
    {
        const auto a = g.points[0], b = g.points[1];
        outline = { a, { b.x, a.y }, b, { a.x, b.y } };
    }
    else if (g.kind == "circle" && !g.points.empty() && g.radius > 0.0)
    {
        outline = arcPoints(g.points[0], g.radius, 0.0, 360.0);
        outline.pop_back();
    }
    else if (g.kind == "arc" && !g.points.empty() && g.radius > 0.0)
    {
        outline = arcPoints(g.points[0], g.radius, g.startAngle, g.endAngle);
        closed = false;
    }
    else if (g.kind == "polygon" && g.points.size() >= 3)
        outline = g.points;
    if (outline.size() < 2) return art;
    if (g.filled && closed && outline.size() >= 3)
    {
        art.fills.push_back(outline);
        // The fill's edge drawn with the line width too, so its size matches the outline.
        auto loop = outline;
        loop.push_back(outline.front());
        art.strokes.push_back({ loop, g.lineWidth });
    }
    else
    {
        if (closed) outline.push_back(outline.front());
        art.strokes.push_back({ outline, g.lineWidth });
    }
    return art;
}

Artwork userArtwork(const Layout& layout, const juce::String& layer)
{
    Artwork art;
    auto append = [&](Artwork a) {
        for (auto& s : a.strokes) art.strokes.push_back(std::move(s));
        for (auto& f : a.fills) art.fills.push_back(std::move(f));
    };
    for (const auto& t : layout.texts) if (t.layer == layer) append(textArtwork(t));
    for (const auto& g : layout.graphics) if (g.layer == layer) append(graphicArtwork(g));
    return art;
}

Artwork silkscreen(const Layout& layout, bool top)
{
    Artwork art = userArtwork(layout, top ? "F.SilkS" : "B.SilkS");
    if (!top) return art;
    const double w = layout.fab.silkLineWidth;
    for (const auto& part : layout.parts)
    {
        // Body outline just inside the courtyard; the pads cut it (clear areas).
        const auto r = courtyardOf(part).reduced(0.2);
        art.strokes.push_back({ { { r.getX(), r.getY() }, { r.getRight(), r.getY() }, { r.getRight(), r.getBottom() },
                                  { r.getX(), r.getBottom() }, { r.getX(), r.getY() } }, w });
        if (symbolNeedsPin1(part.symbolId))
            for (const auto& pad : padsOf(part))
                if (pad.number == "1")
                {
                    // A dot just outside the courtyard corner nearest pad 1.
                    const auto c = courtyardOf(part);
                    Point corner { pad.centre.x < c.getCentreX() ? c.getX() : c.getRight(), pad.centre.y < c.getCentreY() ? c.getY() : c.getBottom() };
                    const Point out { corner.x < c.getCentreX() ? -0.35 : 0.35, corner.y < c.getCentreY() ? -0.35 : 0.35 };
                    const auto dot = corner + out;
                    art.strokes.push_back({ { dot, dot + Point { 0.01, 0.0 } }, 0.3 });
                }
        if (layout.fab.partLabels)
        {
            const auto c = courtyardOf(part);
            BoardText label;
            label.text = part.refdes;
            label.height = layout.fab.labelHeight;
            label.lineWidth = w;
            label.at = { c.getCentreX(), c.getBottom() + 0.3 + label.height / 2.0 };
            for (auto& s : textArtwork(label).strokes) art.strokes.push_back(std::move(s));
        }
    }
    return art;
}

std::vector<Opening> maskOpenings(const Layout& layout, bool top)
{
    std::vector<Opening> out;
    const double e = layout.fab.maskExpansion;
    for (const auto& pad : allPads(layout))
    {
        const bool onSide = pad.drill > 0.0 || top; // SMD pads are on top
        if (!onSide) continue;
        out.push_back({ pad.centre, pad.w + 2 * e, (pad.round ? pad.w : pad.h) + 2 * e, pad.round });
    }
    if (!layout.fab.tentVias)
        for (const auto& v : layout.vias)
            out.push_back({ v.at, v.diameter + 2 * e, v.diameter + 2 * e, true });
    return out;
}

std::vector<Opening> pasteOpenings(const Layout& layout)
{
    std::vector<Opening> out;
    const double r = layout.fab.pasteReduction;
    for (const auto& pad : allPads(layout))
        if (pad.drill <= 0.0)
            out.push_back({ pad.centre, std::max(0.05, pad.w - 2 * r), std::max(0.05, (pad.round ? pad.w : pad.h) - 2 * r), pad.round });
    return out;
}

std::vector<Opening> silkClearAreas(const Layout& layout, bool top)
{
    // Mask openings, grown so a printed line edge stays off them.
    auto areas = maskOpenings(layout, top);
    const double grow = layout.fab.silkLineWidth / 2.0 + 0.05;
    for (auto& a : areas) { a.w += 2 * grow; a.h += 2 * grow; }
    return areas;
}

void addCopperArtKeepouts(const Layout& layout, dr::Board& board)
{
    const double clearance = layout.rules.clearance;
    for (const auto& layer : { juce::String("F.Cu"), juce::String("B.Cu") })
    {
        const int index = copperLayerIndex(layer, board.layerCount());
        if (layer == "B.Cu" && board.layerCount() < 2) continue;
        const auto art = userArtwork(layout, layer);
        forEachSegment(art, [&](Point a, Point b, double width) {
            dr::Keepout k;
            k.area = capsuleBox(a, b, width / 2.0 + clearance);
            k.firstLayer = k.lastLayer = index;
            board.keepouts.push_back(k);
        });
        for (const auto& f : art.fills)
        {
            dr::Keepout k;
            for (const auto& p : f) k.area.push_back({ dr::mm(p.x), dr::mm(p.y) });
            k.firstLayer = k.lastLayer = index;
            board.keepouts.push_back(k);
        }
    }
}

juce::StringArray copperArtProblems(const Layout& layout, const BoardDesign& board)
{
    juce::StringArray out;
    const auto pads = allPads(layout);
    for (const auto& layer : { juce::String("F.Cu"), juce::String("B.Cu") })
    {
        const auto art = userArtwork(layout, layer);
        if (art.strokes.empty() && art.fills.empty()) continue;
        bool off = false, nearEdge = false;
        std::set<juce::String> overPads;
        forEachSegment(art, [&](Point a, Point b, double width) {
            for (const auto& p : { a, b })
            {
                if (!inPoly(board.outline, p)) off = true;
                else if (edgeDistance(board, p) < board.edgeClearance + width / 2.0) nearEdge = true;
            }
            for (const auto& pad : pads)
            {
                if (pad.drill <= 0.0 && layer != "F.Cu") continue;
                const double half = std::max(pad.w, pad.round ? pad.w : pad.h) / 2.0;
                if (pointSegment(pad.centre, a, b) < half + width / 2.0 + layout.rules.clearance)
                    overPads.insert(pad.refdes + "." + pad.number);
            }
        });
        if (off) out.add("Copper art on " + layer + " goes off the board.");
        if (nearEdge) out.add("Copper art on " + layer + " is closer to the board edge than the edge clearance.");
        if (!overPads.empty())
        {
            juce::StringArray list;
            for (const auto& p : overPads) list.add(p);
            out.add("Copper art on " + layer + " touches or crowds pads " + list.joinIntoString(", ") + ".");
        }
    }
    return out;
}

juce::StringArray artworkWarnings(const Layout& layout)
{
    juce::StringArray out;
    for (size_t i = 0; i < layout.texts.size(); ++i)
    {
        const auto& t = layout.texts[i];
        if (t.height < 0.8) out.add("Text " + juce::String((int)i + 1) + " (\"" + t.text + "\") is " + juce::String(t.height, 2) + " mm high; most fabs need 0.8 mm or more.");
        if (t.lineWidth < 0.15) out.add("Text " + juce::String((int)i + 1) + " line width " + juce::String(t.lineWidth, 3) + " mm is under the usual 0.15 mm minimum.");
    }
    for (size_t i = 0; i < layout.graphics.size(); ++i)
        if (layout.graphics[i].lineWidth < 0.15)
            out.add("Graphic " + juce::String((int)i + 1) + " line width " + juce::String(layout.graphics[i].lineWidth, 3) + " mm is under the usual 0.15 mm minimum.");
    if (layout.fab.silkLineWidth < 0.15) out.add("Silkscreen line width is under the usual 0.15 mm minimum.");
    if (layout.fab.partLabels && layout.fab.labelHeight < 0.8) out.add("Part label height is under the usual 0.8 mm minimum.");
    return out;
}

juce::Rectangle<double> boundsOf(const Artwork& art)
{
    bool any = false;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    auto add = [&](Point p, double r) {
        if (!any) { x0 = p.x - r; x1 = p.x + r; y0 = p.y - r; y1 = p.y + r; any = true; return; }
        x0 = std::min(x0, p.x - r); x1 = std::max(x1, p.x + r); y0 = std::min(y0, p.y - r); y1 = std::max(y1, p.y + r);
    };
    for (const auto& s : art.strokes) for (const auto& p : s.points) add(p, s.width / 2.0);
    for (const auto& f : art.fills) for (const auto& p : f) add(p, 0.0);
    return any ? juce::Rectangle<double>(x0, y0, x1 - x0, y1 - y0) : juce::Rectangle<double>();
}
}
