#include "PcbBoard.h"

#include <djehuti_route/outline.h>

#include <cmath>
#include <limits>

namespace pcb
{
namespace
{
namespace dr = djehuti::route;

std::vector<Point> toPoints(const dr::Polygon& polygon)
{
    std::vector<Point> points;
    for (const auto& p : polygon) points.push_back({ dr::toMm(p.x), dr::toMm(p.y) });
    return points;
}

dr::Polygon toPolygon(const std::vector<Point>& points)
{
    dr::Polygon polygon;
    for (const auto& p : points) polygon.push_back({ dr::mm(p.x), dr::mm(p.y) });
    return polygon;
}

juce::var pointsVar(const std::vector<Point>& points)
{
    juce::Array<juce::var> list;
    for (const auto& p : points) list.add(juce::Array<juce::var> { p.x, p.y });
    return list;
}

std::vector<Point> pointsFrom(const juce::var& value)
{
    std::vector<Point> points;
    if (const auto* list = value.getArray())
        for (const auto& p : *list)
            if (const auto* xy = p.getArray(); xy != nullptr && xy->size() >= 2)
                points.push_back({ (double)(*xy)[0], (double)(*xy)[1] });
    return points;
}

double param(const std::map<juce::String, double>& params, const ShapeSpec& spec, const char* key)
{
    const auto found = params.find(key);
    if (found != params.end()) return found->second;
    for (const auto& p : spec.params)
        if (p.key == key) return p.defaultValue;
    return 0.0;
}
}

const std::vector<ShapeSpec>& shapes()
{
    static const std::vector<ShapeSpec> list {
        { "rectangle", "Rectangle", { { "width", "Width", 100.0 }, { "height", "Height", 80.0 } } },
        { "rounded_rectangle", "Rounded rectangle", { { "width", "Width", 100.0 }, { "height", "Height", 80.0 }, { "radius", "Corner radius", 3.0 } } },
        { "chamfered_rectangle", "Chamfered rectangle", { { "width", "Width", 100.0 }, { "height", "Height", 80.0 }, { "chamfer", "Chamfer", 5.0 } } },
        { "l_shape", "L shape", { { "width", "Width", 100.0 }, { "height", "Height", 80.0 }, { "notch_width", "Notch width", 40.0 }, { "notch_height", "Notch height", 30.0 } } },
        { "u_shape", "U shape", { { "width", "Width", 100.0 }, { "height", "Height", 80.0 }, { "slot_width", "Slot width", 30.0 }, { "slot_depth", "Slot depth", 40.0 } } },
        { "t_shape", "T shape", { { "bar_width", "Bar width", 100.0 }, { "bar_height", "Bar height", 30.0 }, { "stem_width", "Stem width", 40.0 }, { "stem_height", "Stem height", 50.0 } } },
        { "circle", "Circle", { { "diameter", "Diameter", 60.0 } } },
        { "polygon", "Regular polygon (hexagon...)", { { "sides", "Sides", 6.0 }, { "across_flats", "Across flats", 60.0 } } },
    };
    return list;
}

const ShapeSpec* findShape(const juce::String& id)
{
    for (const auto& s : shapes())
        if (s.id == id) return &s;
    return nullptr;
}

std::vector<Point> makeShape(const juce::String& shapeId, const std::map<juce::String, double>& params)
{
    namespace o = dr::outline;
    const auto* spec = findShape(shapeId);
    if (spec == nullptr) return {};
    auto p = [&](const char* key) { return param(params, *spec, key); };
    if (shapeId == "rectangle") return toPoints(o::rectangle(p("width"), p("height")));
    if (shapeId == "rounded_rectangle") return toPoints(o::roundedRectangle(p("width"), p("height"), p("radius")));
    if (shapeId == "chamfered_rectangle") return toPoints(o::chamferedRectangle(p("width"), p("height"), p("chamfer")));
    if (shapeId == "l_shape") return toPoints(o::lShape(p("width"), p("height"), p("notch_width"), p("notch_height")));
    if (shapeId == "u_shape") return toPoints(o::uShape(p("width"), p("height"), p("slot_width"), p("slot_depth")));
    if (shapeId == "t_shape") return toPoints(o::tShape(p("bar_width"), p("bar_height"), p("stem_width"), p("stem_height")));
    if (shapeId == "circle") return toPoints(o::circle(p("diameter")));
    if (shapeId == "polygon") return toPoints(o::regularPolygonAcrossFlats(juce::jlimit(3, 64, (int)std::lround(p("sides"))), p("across_flats")));
    return {};
}

BoardDesign BoardDesign::standard(const juce::String& id)
{
    BoardDesign d;
    d.source = "standard";
    d.standardId = id;
    if (const auto* spec = dr::findStandardBoard(id.toStdString()))
    {
        d.outline = toPoints(spec->outline);
        for (const auto& h : spec->holes)
            d.holes.push_back({ { dr::toMm(h.centre.x), dr::toMm(h.centre.y) }, dr::toMm(h.diameter) });
    }
    return d;
}

BoardDesign BoardDesign::fromShape(const juce::String& shapeId, const std::map<juce::String, double>& params)
{
    BoardDesign d;
    d.source = "shape";
    d.shape = shapeId;
    d.shapeParams = params;
    if (const auto* spec = findShape(shapeId))
        for (const auto& p : spec->params)
            if (d.shapeParams.find(p.key) == d.shapeParams.end())
                d.shapeParams[p.key] = p.defaultValue;
    d.outline = makeShape(shapeId, d.shapeParams);
    return d;
}

juce::var BoardDesign::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty("schemaVersion", 1);
    o->setProperty("source", source);
    if (source == "standard") o->setProperty("standard", standardId);
    if (source == "shape")
    {
        o->setProperty("shape", shape);
        auto* params = new juce::DynamicObject();
        for (const auto& [k, v] : shapeParams) params->setProperty(juce::Identifier(k), v);
        o->setProperty("shapeParams", juce::var(params));
    }
    o->setProperty("outline", pointsVar(outline));
    juce::Array<juce::var> holeList;
    for (const auto& h : holes) holeList.add(juce::Array<juce::var> { h.centre.x, h.centre.y, h.diameter });
    o->setProperty("holes", holeList);
    juce::Array<juce::var> cutoutList;
    for (const auto& c : cutouts) cutoutList.add(pointsVar(c));
    o->setProperty("cutouts", cutoutList);
    o->setProperty("layers", layers);
    o->setProperty("thickness", thickness);
    o->setProperty("edgeClearance", edgeClearance);
    return juce::var(o);
}

BoardDesign BoardDesign::fromVar(const juce::var& value)
{
    BoardDesign d;
    if (value.getDynamicObject() == nullptr)
        return standard("fab-100");
    d.source = value.getProperty("source", "custom").toString();
    d.standardId = value.getProperty("standard", "fab-100").toString();
    d.shape = value.getProperty("shape", "rectangle").toString();
    if (const auto* params = value.getProperty("shapeParams", {}).getDynamicObject())
        for (const auto& p : params->getProperties())
            d.shapeParams[p.name.toString()] = (double)p.value;
    d.outline = pointsFrom(value.getProperty("outline", {}));
    if (const auto* list = value.getProperty("holes", {}).getArray())
        for (const auto& h : *list)
            if (const auto* v = h.getArray(); v != nullptr && v->size() >= 3)
                d.holes.push_back({ { (double)(*v)[0], (double)(*v)[1] }, (double)(*v)[2] });
    if (const auto* list = value.getProperty("cutouts", {}).getArray())
        for (const auto& c : *list)
            d.cutouts.push_back(pointsFrom(c));
    d.layers = juce::jlimit(1, 32, (int)value.getProperty("layers", 2));
    d.thickness = (double)value.getProperty("thickness", 1.6);
    d.edgeClearance = (double)value.getProperty("edgeClearance", 0.3);
    return d;
}

juce::StringArray BoardDesign::problems() const
{
    juce::StringArray list;
    const auto poly = toPolygon(outline);
    for (const auto& p : dr::outline::validate(poly)) list.add("Outline: " + juce::String(p));
    if (!list.isEmpty()) return list;
    std::vector<dr::Polygon> cuts;
    for (const auto& c : cutouts) cuts.push_back(toPolygon(c));
    for (const auto& p : dr::outline::validateCutouts(poly, cuts)) list.add(juce::String(p));
    for (size_t i = 0; i < holes.size(); ++i)
    {
        const auto& h = holes[i];
        const dr::Point centre { dr::mm(h.centre.x), dr::mm(h.centre.y) };
        double edge = std::numeric_limits<double>::infinity();
        for (size_t a = 0, b = poly.size() - 1; a < poly.size(); b = a++)
            edge = std::min(edge, dr::pointSegmentDistance(centre, poly[b], poly[a]));
        if (h.diameter <= 0.0)
            list.add("Hole " + juce::String((int)i + 1) + " has no diameter.");
        else if (!dr::polygonContains(poly, centre) || edge < dr::mm(h.diameter / 2.0))
            list.add("Hole " + juce::String((int)i + 1) + " is not fully inside the board.");
    }
    if (layers < 1) list.add("A board needs at least one copper layer.");
    return list;
}

juce::Rectangle<double> BoardDesign::bounds() const
{
    if (outline.empty()) return {};
    double x0 = outline[0].x, y0 = outline[0].y, x1 = x0, y1 = y0;
    for (const auto& p : outline)
    {
        x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
    }
    return { x0, y0, x1 - x0, y1 - y0 };
}

double BoardDesign::areaMm2() const
{
    double area = dr::outline::areaMm2(toPolygon(outline));
    for (const auto& c : cutouts) area -= dr::outline::areaMm2(toPolygon(c));
    for (const auto& h : holes) area -= juce::MathConstants<double>::pi * h.diameter * h.diameter / 4.0;
    return area;
}

double BoardDesign::perimeterMm() const
{
    return dr::outline::perimeterMm(toPolygon(outline));
}

dr::Board BoardDesign::toRouteBoard() const
{
    dr::Board b;
    b.layers.clear();
    for (int i = 0; i < layers; ++i)
        b.layers.push_back(i == 0 ? "F.Cu" : i == layers - 1 ? "B.Cu" : "In" + std::to_string(i) + ".Cu");
    b.outline = toPolygon(outline);
    for (const auto& c : cutouts) b.cutouts.push_back(toPolygon(c));
    for (const auto& h : holes) b.addHole(h.centre.x, h.centre.y, h.diameter);
    b.edgeClearance = dr::mm(edgeClearance);
    return b;
}
}
