#include "SchematicRouter.h"

#include <libavoid/libavoid.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>

namespace schematic::routing
{
namespace
{
using P = juce::Point<float>;

constexpr float shapeBuffer = 12.0f;

void configureRouter(Avoid::Router& router, float gridSize)
{
    // segmentPenalty keeps routes to few bends and is required for nudging.
    router.setRoutingParameter(Avoid::segmentPenalty, 50.0);
    router.setRoutingParameter(Avoid::shapeBufferDistance, shapeBuffer);
    // Nudge parallel segments of different wires one grid step apart so the
    // grid snap afterwards barely moves them.
    router.setRoutingParameter(Avoid::idealNudgingDistance, gridSize);
    // Pin stubs stay fixed: wires leave pins straight along the lead.
    router.setRoutingOption(Avoid::nudgeOrthogonalSegmentsConnectedToShapes, false);
    // Wires sharing an endpoint belong to the same net; let their shared
    // runs overlap instead of drawing parallel copies of one net.
    router.setRoutingOption(Avoid::nudgeSharedPathsWithCommonEndPoint, false);
    router.setRoutingOption(Avoid::performUnifyingNudgingPreprocessingStep, true);
    router.setTransactionUse(true);
}

Avoid::ConnDirFlags dirFlags(P d)
{
    if (std::abs(d.x) > std::abs(d.y))
        return d.x < 0.0f ? Avoid::ConnDirLeft : Avoid::ConnDirRight;
    if (std::abs(d.y) > 0.0f)
        return d.y < 0.0f ? Avoid::ConnDirUp : Avoid::ConnDirDown;
    return Avoid::ConnDirAll;
}

Avoid::Polygon rectPolygon(juce::Rectangle<float> r)
{
    Avoid::Polygon polygon(4);
    polygon.setPoint(0, Avoid::Point(r.getX(), r.getY()));
    polygon.setPoint(1, Avoid::Point(r.getRight(), r.getY()));
    polygon.setPoint(2, Avoid::Point(r.getRight(), r.getBottom()));
    polygon.setPoint(3, Avoid::Point(r.getX(), r.getBottom()));
    return polygon;
}

// Shapes with one connection pin per symbol pin. Pin class ids are pinIndex + 1.
struct ShapeTable
{
    std::vector<Avoid::ShapeRef*> shapes;
    std::map<const Avoid::ShapeRef*, int> indexOf;
};

ShapeTable addShapes(Avoid::Router& router, const std::vector<Obstacle>& obstacles)
{
    ShapeTable table;
    for (int i = 0; i < (int)obstacles.size(); ++i)
    {
        const auto& obstacle = obstacles[(size_t)i];
        auto bounds = obstacle.bounds;
        if (bounds.getWidth() < 1.0f) bounds = bounds.withSizeKeepingCentre(1.0f, bounds.getHeight());
        if (bounds.getHeight() < 1.0f) bounds = bounds.withSizeKeepingCentre(bounds.getWidth(), 1.0f);
        auto polygon = rectPolygon(bounds);
        auto* shape = new Avoid::ShapeRef(&router, polygon);
        for (int p = 0; p < (int)obstacle.pins.size(); ++p)
        {
            const auto& pin = obstacle.pins[(size_t)p];
            new Avoid::ShapeConnectionPin(shape, (unsigned int)(p + 1),
                                          pin.position.x - bounds.getX(),
                                          pin.position.y - bounds.getY(),
                                          false, 0.0, dirFlags(pin.direction));
        }
        table.shapes.push_back(shape);
        table.indexOf[shape] = i;
    }
    return table;
}

Avoid::ConnEnd connEndFor(const Endpoint& e, const ShapeTable& shapes,
                          const std::vector<Avoid::JunctionRef*>& junctions)
{
    if (e.isPin())
        return Avoid::ConnEnd(shapes.shapes[(size_t)e.obstacle], (unsigned int)(e.pin + 1));
    if (e.isJunction())
        return Avoid::ConnEnd(junctions[(size_t)e.junction]);
    return Avoid::ConnEnd(Avoid::Point(e.point.x, e.point.y));
}

P endpointPosition(const Endpoint& e, const std::vector<Obstacle>& obstacles, const std::vector<P>& junctions)
{
    if (e.isPin()) return obstacles[(size_t)e.obstacle].pins[(size_t)e.pin].position;
    if (e.isJunction()) return junctions[(size_t)e.junction];
    return e.point;
}

P endpointDirection(const Endpoint& e, const std::vector<Obstacle>& obstacles)
{
    return e.isPin() ? obstacles[(size_t)e.obstacle].pins[(size_t)e.pin].direction : P();
}

float snapValue(float v, float grid)
{
    return std::round(v / grid) * grid;
}

bool nearlyEqual(float a, float b)
{
    return std::abs(a - b) < 0.5f;
}

void simplify(Polyline& points)
{
    Polyline out;
    for (const auto& p : points)
        if (out.empty() || p.getDistanceFrom(out.back()) > 0.5f)
            out.push_back(p);
    // Drop interior points on a straight run.
    bool changed = true;
    while (changed && out.size() >= 3)
    {
        changed = false;
        for (size_t i = 1; i + 1 < out.size(); ++i)
        {
            const auto& a = out[i - 1];
            const auto& b = out[i];
            const auto& c = out[i + 1];
            if ((nearlyEqual(a.x, b.x) && nearlyEqual(b.x, c.x)) || (nearlyEqual(a.y, b.y) && nearlyEqual(b.y, c.y)))
            {
                out.erase(out.begin() + (long)i);
                changed = true;
                break;
            }
        }
    }
    points = std::move(out);
}

// Converts a libavoid route to strictly orthogonal points (libavoid can emit
// a tiny diagonal where a pin is a fraction off its ideal position).
Polyline orthogonalize(const Polyline& raw)
{
    Polyline out;
    for (const auto& p : raw)
    {
        if (!out.empty())
        {
            const auto prev = out.back();
            if (!nearlyEqual(prev.x, p.x) && !nearlyEqual(prev.y, p.y))
                out.push_back({ p.x, prev.y });
        }
        out.push_back(p);
    }
    simplify(out);
    return out;
}

bool isHorizontal(P a, P b) { return nearlyEqual(a.y, b.y); }

// Moves every interior segment onto the grid. The first and last points are
// pins (already on the grid); their stubs keep leaving along the lead and
// are kept at least one grid step long.
Polyline snapToGrid(Polyline points, P startDir, P endDir, float grid)
{
    simplify(points);
    if (points.size() < 3)
        return points;

    const auto n = points.size();
    for (size_t i = 1; i + 2 < n; ++i)
    {
        // Segment i: points[i] -> points[i+1], both interior.
        if (isHorizontal(points[i], points[i + 1]))
        {
            const auto y = snapValue(points[i].y, grid);
            points[i].y = points[i + 1].y = y;
        }
        else
        {
            const auto x = snapValue(points[i].x, grid);
            points[i].x = points[i + 1].x = x;
        }
    }

    // Single-bend routes and stub ends: the vertices next to the pins.
    auto fixStub = [grid](P pin, P& next, P* after, P dir) {
        if (dir == P())
            return;
        if (std::abs(dir.x) > 0.5f)
        {
            next.y = pin.y;
            if ((next.x - pin.x) * dir.x < grid)
            {
                next.x = pin.x + dir.x * grid;
                if (after != nullptr) after->x = next.x;
            }
        }
        else
        {
            next.x = pin.x;
            if ((next.y - pin.y) * dir.y < grid)
            {
                next.y = pin.y + dir.y * grid;
                if (after != nullptr) after->y = next.y;
            }
        }
    };

    if (n >= 3)
    {
        fixStub(points[0], points[1], n >= 4 ? &points[2] : nullptr, startDir);
        fixStub(points[n - 1], points[n - 2], n >= 4 ? &points[n - 3] : nullptr, endDir);
    }

    // Re-orthogonalize after the stub adjustments.
    Polyline out;
    for (const auto& p : points)
    {
        if (!out.empty())
        {
            const auto prev = out.back();
            if (!nearlyEqual(prev.x, p.x) && !nearlyEqual(prev.y, p.y))
                out.push_back({ p.x, prev.y });
        }
        out.push_back(p);
    }
    simplify(out);
    return out;
}

struct Segment
{
    P a;
    P b;
    int connection = -1;
    int net = -1;
    size_t index = 0; // segment index within its polyline
};

bool collinearOverlap(const Segment& s, const Segment& t)
{
    const auto sh = isHorizontal(s.a, s.b);
    const auto th = isHorizontal(t.a, t.b);
    if (sh != th)
        return false;
    if (sh)
    {
        if (!nearlyEqual(s.a.y, t.a.y)) return false;
        const auto s0 = std::min(s.a.x, s.b.x), s1 = std::max(s.a.x, s.b.x);
        const auto t0 = std::min(t.a.x, t.b.x), t1 = std::max(t.a.x, t.b.x);
        return std::min(s1, t1) - std::max(s0, t0) > 0.5f;
    }
    if (!nearlyEqual(s.a.x, t.a.x)) return false;
    const auto s0 = std::min(s.a.y, s.b.y), s1 = std::max(s.a.y, s.b.y);
    const auto t0 = std::min(t.a.y, t.b.y), t1 = std::max(t.a.y, t.b.y);
    return std::min(s1, t1) - std::max(s0, t0) > 0.5f;
}

bool segmentHitsRect(P a, P b, juce::Rectangle<float> r)
{
    const auto box = juce::Rectangle<float>(a, b).expanded(isHorizontal(a, b) ? 0.0f : 0.25f,
                                                          isHorizontal(a, b) ? 0.25f : 0.0f);
    return box.intersects(r);
}

bool segmentTouchesPoint(P a, P b, P p)
{
    if (isHorizontal(a, b))
        return nearlyEqual(p.y, a.y) && p.x >= std::min(a.x, b.x) - 0.5f && p.x <= std::max(a.x, b.x) + 0.5f;
    return nearlyEqual(p.x, a.x) && p.y >= std::min(a.y, b.y) - 0.5f && p.y <= std::max(a.y, b.y) + 0.5f;
}

// After snapping, two different nets can land on the same grid line, or an
// interior segment can cut a symbol or a foreign pin. Shift offending
// interior segments to the nearest free grid line (SCH-W2, SCH-W3).
void resolveConflicts(std::vector<Polyline>& routes, const Problem& problem, float grid)
{
    auto netOf = [&](int connection) {
        const auto net = problem.connections[(size_t)connection].net;
        return net >= 0 ? net : -1000 - connection;
    };

    // Pins and their nets, for the foreign-pin test.
    std::vector<std::pair<P, int>> pinNets;
    for (int c = 0; c < (int)problem.connections.size(); ++c)
        for (const auto* e : { &problem.connections[(size_t)c].a, &problem.connections[(size_t)c].b })
            if (e->isPin())
                pinNets.push_back({ problem.obstacles[(size_t)e->obstacle].pins[(size_t)e->pin].position, netOf(c) });

    std::vector<juce::Rectangle<float>> bodies;
    for (const auto& obstacle : problem.obstacles)
        bodies.push_back(obstacle.bounds.reduced(1.0f));

    auto segmentOk = [&](P a, P b, int connection, size_t index) {
        const auto net = netOf(connection);
        for (const auto& body : bodies)
            if (segmentHitsRect(a, b, body))
                return false;
        for (const auto& [pin, pinNet] : pinNets)
            if (pinNet != net && segmentTouchesPoint(a, b, pin))
                return false;
        const Segment s { a, b, connection, net, index };
        for (int c = 0; c < (int)routes.size(); ++c)
        {
            if (netOf(c) == net)
                continue;
            const auto& route = routes[(size_t)c];
            for (size_t i = 1; i < route.size(); ++i)
                if (collinearOverlap(s, { route[i - 1], route[i], c, netOf(c), i - 1 }))
                    return false;
        }
        return true;
    };

    for (int pass = 0; pass < 4; ++pass)
    {
        bool changed = false;
        for (int c = 0; c < (int)routes.size(); ++c)
        {
            auto& route = routes[(size_t)c];
            if (route.size() < 4)
                continue; // only interior segments can move
            for (size_t i = 1; i + 2 < route.size(); ++i)
            {
                if (segmentOk(route[i], route[i + 1], c, i))
                    continue;

                const auto horizontal = isHorizontal(route[i], route[i + 1]);
                const auto base = horizontal ? route[i].y : route[i].x;
                for (int step = 1; step <= 6; ++step)
                {
                    bool placed = false;
                    for (int sign : { 1, -1 })
                    {
                        const auto v = base + (float)(sign * step) * grid;
                        auto a = route[i];
                        auto b = route[i + 1];
                        if (horizontal) { a.y = v; b.y = v; } else { a.x = v; b.x = v; }
                        // The neighbours stretch; they must stay valid too.
                        auto pa = route[i - 1];
                        auto pb = route[i + 2];
                        if (!segmentOk(pa, a, c, i - 1) || !segmentOk(b, pb, c, i + 1) || !segmentOk(a, b, c, i))
                            continue;
                        route[i] = a;
                        route[i + 1] = b;
                        placed = changed = true;
                        break;
                    }
                    if (placed)
                        break;
                }
            }
        }
        if (!changed)
            break;
    }
}

// libavoid places a junction where the tree branches in its own geometry,
// but after nudging and grid snapping several of a junction's wires can
// leave it together along the same run. Slide the junction along that
// shared run to where the wires actually part, so the dot sits on the T.
void slideJunctions(std::vector<Polyline>& routes, const Problem& problem, std::vector<P>& junctions, float grid)
{
    for (int j = 0; j < (int)junctions.size(); ++j)
    {
        // Incident routes, oriented to start at the junction.
        std::vector<std::pair<size_t, bool>> incident; // route index, reversed
        for (size_t c = 0; c < problem.connections.size(); ++c)
        {
            if (routes[c].size() < 2) continue;
            if (problem.connections[c].a.isJunction() && problem.connections[c].a.junction == j) incident.push_back({ c, false });
            else if (problem.connections[c].b.isJunction() && problem.connections[c].b.junction == j) incident.push_back({ c, true });
        }
        if (incident.size() < 3)
            continue;

        for (auto& [c, reversed] : incident)
            if (reversed) std::reverse(routes[c].begin(), routes[c].end());

        for (int iteration = 0; iteration < 16; ++iteration)
        {
            std::map<std::pair<int, int>, std::vector<size_t>> byDirection;
            for (auto& [c, reversed] : incident)
            {
                const auto& r = routes[c];
                if (r.size() < 2) continue;
                const auto d = r[1] - r[0];
                const auto key = std::make_pair(d.x > 0.5f ? 1 : d.x < -0.5f ? -1 : 0, d.y > 0.5f ? 1 : d.y < -0.5f ? -1 : 0);
                byDirection[key].push_back(c);
            }
            std::pair<int, int> bestKey { 0, 0 };
            size_t bestCount = 1;
            for (const auto& [key, members] : byDirection)
                if (members.size() > bestCount) { bestCount = members.size(); bestKey = key; }
            if (bestCount < 2)
                break;

            float slide = std::numeric_limits<float>::max();
            for (auto c : byDirection[bestKey])
            {
                auto length = routes[c][0].getDistanceFrom(routes[c][1]);
                if (routes[c].size() == 2) length -= grid; // never slide onto the far end
                slide = std::min(slide, length);
            }
            slide = std::floor(slide / grid) * grid;
            if (slide < grid)
                break;

            const P dir { (float)bestKey.first, (float)bestKey.second };
            const auto moved = junctions[(size_t)j] + dir * slide;
            for (auto& [c, reversed] : incident)
            {
                auto& r = routes[c];
                const auto shares = std::find(byDirection[bestKey].begin(), byDirection[bestKey].end(), c) != byDirection[bestKey].end();
                if (shares)
                {
                    r[0] = moved;
                    if (r[0].getDistanceFrom(r[1]) < 0.5f) r.erase(r.begin());
                }
                else
                {
                    r.insert(r.begin(), moved);
                }
                simplify(r);
            }
            junctions[(size_t)j] = moved;
        }

        for (auto& [c, reversed] : incident)
            if (reversed) std::reverse(routes[c].begin(), routes[c].end());
    }
}
}

std::vector<Polyline> routeConnections(const Problem& problem, float gridSize, std::vector<juce::Point<float>>* adjustedJunctions)
{
    std::vector<Polyline> result(problem.connections.size());
    if (problem.connections.empty())
        return result;

    Avoid::Router router(Avoid::OrthogonalRouting);
    configureRouter(router, gridSize);
    const auto shapes = addShapes(router, problem.obstacles);

    std::vector<Avoid::JunctionRef*> junctions;
    for (const auto& j : problem.junctions)
    {
        auto* junction = new Avoid::JunctionRef(&router, Avoid::Point(j.x, j.y));
        junction->setPositionFixed(true);
        junctions.push_back(junction);
    }

    std::vector<Avoid::ConnRef*> connectors;
    for (const auto& connection : problem.connections)
    {
        auto* connector = new Avoid::ConnRef(&router,
                                             connEndFor(connection.a, shapes, junctions),
                                             connEndFor(connection.b, shapes, junctions));
        connector->setRoutingType(Avoid::ConnType_Orthogonal);
        connectors.push_back(connector);
    }

    router.processTransaction();

    for (size_t i = 0; i < connectors.size(); ++i)
    {
        const auto& route = connectors[i]->displayRoute();
        if (route.ps.size() < 2)
            continue;

        Polyline raw;
        for (const auto& p : route.ps)
            raw.push_back({ (float)p.x, (float)p.y });

        // libavoid reports the exact connection point; make sure the polyline
        // starts and ends on the model's pin/junction coordinates.
        const auto& connection = problem.connections[i];
        raw.front() = endpointPosition(connection.a, problem.obstacles, problem.junctions);
        raw.back() = endpointPosition(connection.b, problem.obstacles, problem.junctions);

        result[i] = snapToGrid(orthogonalize(raw),
                               endpointDirection(connection.a, problem.obstacles),
                               endpointDirection(connection.b, problem.obstacles),
                               gridSize);
    }

    resolveConflicts(result, problem, gridSize);
    auto slidJunctions = problem.junctions;
    slideJunctions(result, problem, slidJunctions, gridSize);
    for (auto& route : result)
        simplify(route);
    if (adjustedJunctions != nullptr)
        *adjustedJunctions = slidJunctions;
    return result;
}

std::vector<TreeSolution> routeNetTrees(const std::vector<Obstacle>& obstacles,
                                        const std::vector<NetTerminals>& nets,
                                        float gridSize)
{
    std::vector<TreeSolution> solutions(nets.size());

    Avoid::Router router(Avoid::OrthogonalRouting);
    configureRouter(router, gridSize);
    router.setRoutingOption(Avoid::improveHyperedgeRoutesMovingJunctions, true);
    const auto shapes = addShapes(router, obstacles);
    const std::vector<Avoid::JunctionRef*> noJunctions;

    auto* rerouter = router.hyperedgeRerouter();
    std::map<size_t, size_t> hyperedgeForNet;
    for (size_t n = 0; n < nets.size(); ++n)
    {
        const auto& terminals = nets[n].terminals;
        if (terminals.size() == 2)
        {
            solutions[n].edges.push_back({ terminals[0], terminals[1] });
        }
        else if (terminals.size() > 2)
        {
            Avoid::ConnEndList ends;
            for (const auto& terminal : terminals)
                ends.push_back(connEndFor(terminal, shapes, noJunctions));
            hyperedgeForNet[n] = rerouter->registerHyperedgeForRerouting(ends);
        }
    }

    if (hyperedgeForNet.empty())
        return solutions;

    router.processTransaction();

    for (const auto& [n, index] : hyperedgeForNet)
    {
        auto lists = rerouter->newAndDeletedObjectLists(index);
        auto& solution = solutions[n];

        std::vector<P> rawJunctions;
        for (auto* junction : lists.newJunctionList)
        {
            const auto p = junction->position();
            rawJunctions.push_back({ (float)p.x, (float)p.y });
            solution.junctions.push_back({ snapValue((float)p.x, gridSize), snapValue((float)p.y, gridSize) });
        }

        // Connectors created by the rerouter expose neither ConnEnds nor
        // (publicly) their anchors; each route end sits exactly on either a
        // new junction or one of this net's terminal pins, so match by position.
        auto toEndpoint = [&](const Avoid::Point& at, Endpoint& out) {
            const P q { (float)at.x, (float)at.y };
            float best = 2.0f;
            bool found = false;
            for (int j = 0; j < (int)rawJunctions.size(); ++j)
                if (const auto d = rawJunctions[(size_t)j].getDistanceFrom(q); d < best)
                {
                    best = d;
                    out = Endpoint::forJunction(j);
                    found = true;
                }
            for (const auto& terminal : nets[n].terminals)
                if (const auto d = obstacles[(size_t)terminal.obstacle].pins[(size_t)terminal.pin].position.getDistanceFrom(q); d < best)
                {
                    best = d;
                    out = terminal;
                    found = true;
                }
            return found;
        };

        for (auto* connector : lists.newConnectorList)
        {
            const auto& route = connector->displayRoute();
            if (route.ps.size() < 2)
                continue;
            Endpoint a, b;
            if (toEndpoint(route.ps.front(), a) && toEndpoint(route.ps.back(), b))
                solution.edges.push_back({ a, b });
        }

        // A junction that snapped onto a terminal pin becomes that pin.
        for (int j = 0; j < (int)solution.junctions.size(); ++j)
        {
            for (const auto& terminal : nets[n].terminals)
            {
                if (obstacles[(size_t)terminal.obstacle].pins[(size_t)terminal.pin].position.getDistanceFrom(solution.junctions[(size_t)j]) > 0.5f)
                    continue;
                for (auto& edge : solution.edges)
                {
                    if (edge.a.isJunction() && edge.a.junction == j) edge.a = terminal;
                    if (edge.b.isJunction() && edge.b.junction == j) edge.b = terminal;
                }
                break;
            }
        }

        // Merge junctions that snapped onto the same grid point.
        for (int j = 0; j < (int)solution.junctions.size(); ++j)
            for (int k = j + 1; k < (int)solution.junctions.size(); ++k)
                if (solution.junctions[(size_t)j].getDistanceFrom(solution.junctions[(size_t)k]) < 0.5f)
                    for (auto& edge : solution.edges)
                    {
                        if (edge.a.isJunction() && edge.a.junction == k) edge.a.junction = j;
                        if (edge.b.isJunction() && edge.b.junction == k) edge.b.junction = j;
                    }

        auto same = [](const Endpoint& x, const Endpoint& y) {
            return (x.isJunction() && y.isJunction() && x.junction == y.junction)
                || (x.isPin() && y.isPin() && x.obstacle == y.obstacle && x.pin == y.pin);
        };
        solution.edges.erase(std::remove_if(solution.edges.begin(), solution.edges.end(),
                                            [&](const TreeEdge& e) { return same(e.a, e.b); }),
                             solution.edges.end());

        // Dissolve junctions with fewer than three edges: a two-edge junction
        // is just a bend, so its edges join into one.
        bool dissolved = true;
        while (dissolved)
        {
            dissolved = false;
            for (int j = 0; j < (int)solution.junctions.size() && !dissolved; ++j)
            {
                std::vector<size_t> touching;
                for (size_t e = 0; e < solution.edges.size(); ++e)
                    if ((solution.edges[e].a.isJunction() && solution.edges[e].a.junction == j)
                        || (solution.edges[e].b.isJunction() && solution.edges[e].b.junction == j))
                        touching.push_back(e);
                if (touching.size() == 0 || touching.size() >= 3)
                    continue;

                auto other = [&](const TreeEdge& e) {
                    return (e.a.isJunction() && e.a.junction == j) ? e.b : e.a;
                };
                if (touching.size() == 2)
                {
                    const TreeEdge merged { other(solution.edges[touching[0]]), other(solution.edges[touching[1]]) };
                    solution.edges.erase(solution.edges.begin() + (long)touching[1]);
                    solution.edges.erase(solution.edges.begin() + (long)touching[0]);
                    if (!same(merged.a, merged.b))
                        solution.edges.push_back(merged);
                }
                else
                {
                    solution.edges.erase(solution.edges.begin() + (long)touching[0]);
                }
                dissolved = true;
            }
        }

        // Compact junction indices to the ones still in use.
        std::vector<int> remap(solution.junctions.size(), -1);
        std::vector<P> used;
        for (auto& edge : solution.edges)
            for (auto* end : { &edge.a, &edge.b })
                if (end->isJunction())
                {
                    if (remap[(size_t)end->junction] < 0)
                    {
                        remap[(size_t)end->junction] = (int)used.size();
                        used.push_back(solution.junctions[(size_t)end->junction]);
                    }
                    end->junction = remap[(size_t)end->junction];
                }
        solution.junctions = std::move(used);

        // libavoid occasionally returns no usable tree for a hyperedge. If
        // the terminals are not all connected, fall back to a minimum
        // spanning tree over the terminal pins (Manhattan distance).
        const auto& terminals = nets[n].terminals;
        std::vector<int> parent(terminals.size() + solution.junctions.size());
        for (size_t k = 0; k < parent.size(); ++k) parent[k] = (int)k;
        std::function<int(int)> find = [&](int x) { return parent[(size_t)x] == x ? x : parent[(size_t)x] = find(parent[(size_t)x]); };
        auto nodeOf = [&](const Endpoint& e) {
            if (e.isJunction()) return (int)terminals.size() + e.junction;
            for (size_t k = 0; k < terminals.size(); ++k)
                if (terminals[k].obstacle == e.obstacle && terminals[k].pin == e.pin) return (int)k;
            return -1;
        };
        for (const auto& edge : solution.edges)
        {
            const auto a = nodeOf(edge.a), b = nodeOf(edge.b);
            if (a >= 0 && b >= 0) parent[(size_t)find(a)] = find(b);
        }
        bool connected = true;
        for (size_t k = 1; k < terminals.size(); ++k)
            connected &= find((int)k) == find(0);
        if (!connected)
        {
            solution.junctions.clear();
            solution.edges.clear();
            std::vector<bool> inTree(terminals.size(), false);
            inTree[0] = true;
            auto position = [&](size_t k) { return obstacles[(size_t)terminals[k].obstacle].pins[(size_t)terminals[k].pin].position; };
            for (size_t added = 1; added < terminals.size(); ++added)
            {
                float best = std::numeric_limits<float>::max();
                size_t from = 0, to = 0;
                for (size_t a = 0; a < terminals.size(); ++a)
                    for (size_t b = 0; b < terminals.size(); ++b)
                        if (inTree[a] && !inTree[b])
                        {
                            const auto d = std::abs(position(a).x - position(b).x) + std::abs(position(a).y - position(b).y);
                            if (d < best) { best = d; from = a; to = b; }
                        }
                inTree[to] = true;
                solution.edges.push_back({ terminals[from], terminals[to] });
            }
        }
    }

    return solutions;
}
}
