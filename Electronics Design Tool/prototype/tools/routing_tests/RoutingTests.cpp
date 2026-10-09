// Constrained schematic routing checks (libavoid via SchematicRouter).
// Expected results are worked out from the geometry before running: grid 24,
// R1/R2 with a blocking body between them, waypoints below the block.

#include <JuceHeader.h>
#include "../../Source/SchematicRouter.h"
#include "../../Source/RouteEditing.h"

#include <cstdio>

namespace routing = schematic::routing;
using P = juce::Point<float>;

namespace
{
int failures = 0;

void checkTrue(const char* name, bool condition, const juce::String& detail = {})
{
    std::printf("%s  %s %s\n", condition ? "PASS" : "FAIL", name, detail.toRawUTF8());
    if (!condition) ++failures;
}

juce::String describe(const routing::Polyline& route)
{
    juce::String s;
    for (const auto& p : route) s << "(" << p.x << "," << p.y << ")";
    return s;
}

routing::Obstacle resistor(float x, float y) // body x..x+96, y..y+48, pins mid-height on each end
{
    routing::Obstacle o;
    o.bounds = { x, y, 96.0f, 48.0f };
    o.pins.push_back({ { x, y + 24.0f }, { -1.0f, 0.0f } });
    o.pins.push_back({ { x + 96.0f, y + 24.0f }, { 1.0f, 0.0f } });
    return o;
}

routing::Obstacle block() { routing::Obstacle o; o.bounds = { 288.0f, 48.0f, 96.0f, 144.0f }; return o; }

routing::Connection connect(int fromObstacle, int fromPin, int toObstacle, int toPin, int net, std::vector<P> waypoints = {})
{
    return { routing::Endpoint::forPin(fromObstacle, fromPin), routing::Endpoint::forPin(toObstacle, toPin), net, std::move(waypoints) };
}
}

int main()
{
    constexpr float grid = 24.0f;
    const std::vector<P> below { { 240.0f, 312.0f }, { 432.0f, 312.0f } };

    std::printf("-- checker sanity --\n");
    {
        routing::Problem p;
        p.obstacles = { resistor(96, 96), resistor(480, 96), block() };
        checkTrue("straight line through the block is a body crossing", routing::crossesBody({ { 192, 120 }, { 480, 120 } }, p.obstacles));
        checkTrue("collinear runs overlap", routing::routesOverlap({ { 0, 0 }, { 100, 0 } }, { { 50, 0 }, { 150, 0 } }));
        checkTrue("parallel runs a grid apart do not overlap", !routing::routesOverlap({ { 0, 0 }, { 100, 0 } }, { { 0, 24 }, { 100, 24 } }));
    }

    std::printf("-- unconstrained route avoids the body --\n");
    {
        routing::Problem p;
        p.obstacles = { resistor(96, 96), resistor(480, 96), block() };
        p.connections = { connect(0, 1, 1, 0, 0) };
        std::vector<juce::String> why;
        const auto routes = routing::routeConnections(p, grid, nullptr, {}, &why);
        checkTrue("routes", routes[0].size() >= 2, why[0]);
        checkTrue("starts and ends on the pins", !routes[0].empty() && routes[0].front() == P(192, 120) && routes[0].back() == P(480, 120), describe(routes[0]));
        checkTrue("no body crossing", !routing::crossesBody(routes[0], p.obstacles), describe(routes[0]));
    }

    std::printf("-- pinned waypoints, in order --\n");
    {
        routing::Problem p;
        p.obstacles = { resistor(96, 96), resistor(480, 96), block() };
        p.connections = { connect(0, 1, 1, 0, 0, below) };
        std::vector<juce::String> why;
        const auto routes = routing::routeConnections(p, grid, nullptr, {}, &why);
        checkTrue("routes through two pinned points", why[0].isEmpty() && routes[0].size() >= 2, why[0]);
        checkTrue("visits (240,312) then (432,312)", routing::visitsInOrder(routes[0], below), describe(routes[0]));
        checkTrue("reverse order is not what it visits", !routing::visitsInOrder(routes[0], { below[1], below[0] }), describe(routes[0]));
        checkTrue("no body crossing", !routing::crossesBody(routes[0], p.obstacles), describe(routes[0]));

        // Move R2 down 96: the pinned points must not move, only the stretches to them reroute.
        p.obstacles[1] = resistor(480, 192);
        const auto moved = routing::routeConnections(p, grid, nullptr, {}, &why);
        checkTrue("after moving R2 still routes", why[0].isEmpty() && moved[0].size() >= 2, why[0]);
        checkTrue("after moving R2 pinned points are still visited exactly", routing::visitsInOrder(moved[0], below), describe(moved[0]));
        checkTrue("after moving R2 ends on its new pin", !moved[0].empty() && moved[0].back() == P(480, 216), describe(moved[0]));
        checkTrue("after moving R2 no body crossing", !routing::crossesBody(moved[0], p.obstacles), describe(moved[0]));
    }

    std::printf("-- unreachable pinned point fails explicitly --\n");
    {
        routing::Problem p;
        p.obstacles = { resistor(96, 96), resistor(480, 96), block() };
        p.connections = { connect(0, 1, 1, 0, 0, { { 336.0f, 120.0f } }), connect(0, 0, 1, 1, 1) };
        std::vector<juce::String> why;
        const auto routes = routing::routeConnections(p, grid, nullptr, {}, &why);
        checkTrue("point inside a body is refused with a reason", routes[0].empty() && why[0].contains("inside a component"), why[0]);
        checkTrue("the other wire still routes", why[1].isEmpty() && routes[1].size() >= 2, why[1]);
    }

    std::printf("-- independent wires do not overlap --\n");
    {
        // A tall block between two independent pairs: both nets must detour
        // around it and compete for the same channel.
        routing::Obstacle tall;
        tall.bounds = { 288.0f, -48.0f, 96.0f, 240.0f };
        for (const bool pinned : { false, true })
        {
            routing::Problem p;
            p.obstacles = { resistor(96, 96), resistor(480, 96), resistor(96, 0), resistor(480, 0), tall };
            p.connections = { connect(0, 1, 1, 0, 0, pinned ? below : std::vector<P> {}), connect(2, 1, 3, 0, 1) };
            std::vector<juce::String> why;
            const auto routes = routing::routeConnections(p, grid, nullptr, {}, &why);
            const juce::String tag = pinned ? " (net 0 pinned)" : " (both free)";
            checkTrue("both route", why[0].isEmpty() && why[1].isEmpty() && routes[0].size() >= 2 && routes[1].size() >= 2, why[0] + why[1] + tag);
            checkTrue("independent nets do not share a segment", !routing::routesOverlap(routes[0], routes[1]),
                      describe(routes[0]) + " | " + describe(routes[1]) + tag);
            for (const auto& r : routes)
                checkTrue("no body crossing", !routing::crossesBody(r, p.obstacles), describe(r) + tag);
        }
    }

    std::printf("-- out-and-back through a pinned point --\n");
    {
        // R1's right pin to R2's left pin via a point past R2: the route must go
        // out to (720,72) and come back, and the point must survive joining.
        routing::Problem p;
        p.obstacles = { resistor(96, 96), resistor(480, 96) };
        p.connections = { connect(0, 1, 1, 0, 0, { { 720.0f, 72.0f } }) };
        std::vector<juce::String> why;
        const auto routes = routing::routeConnections(p, grid, nullptr, {}, &why);
        checkTrue("routes via a point beyond the target", why[0].isEmpty() && routes[0].size() >= 2, why[0] + " " + describe(routes[0]));
        checkTrue("visits (720,72)", routing::visitsInOrder(routes[0], { { 720.0f, 72.0f } }), describe(routes[0]));
    }

    std::printf("-- manual editing constraints --\n");
    {
        namespace edit = schematic::route_edit;
        routing::Problem p;
        p.obstacles = { resistor(96, 96), resistor(480, 96), block() };
        p.connections = { connect(0, 1, 1, 0, 0) };
        auto routeWith = [&](const std::vector<edit::EditPoint>& points, juce::String& why) {
            p.connections[0].waypoints.clear();
            for (const auto& e : points) if (e.pinned) p.connections[0].waypoints.push_back(e.position);
            std::vector<juce::String> w;
            const auto r = routing::routeConnections(p, grid, nullptr, {}, &w);
            why = w[0];
            return r[0];
        };
        juce::String why;
        const auto original = routeWith({}, why); // (192,120)(240,120)(240,216)(432,216)(432,120)(480,120)

        // Drag the bottom run (segment 2) down by 100 px: snaps to 96.
        const auto offset = edit::segmentOffset(original, 2, { 7.0f, 100.0f }, grid);
        checkTrue("segment drag offset is perpendicular and grid-snapped", offset == P(0, 96), juce::String(offset.x) + "," + juce::String(offset.y));
        const auto dragged = edit::pointsForSegmentDrag(original, {}, 2, offset, grid);
        checkTrue("segment drag pins exactly the two moved corners", dragged.size() == 2 && dragged[0].position == P(240, 312)
                  && dragged[1].position == P(432, 312) && dragged[0].pinned && dragged[1].pinned);
        const auto afterDrag = routeWith(dragged, why);
        checkTrue("router honours the dragged segment", why.isEmpty() && routing::visitsInOrder(afterDrag, { { 240, 312 }, { 432, 312 } }), why + " " + describe(afterDrag));
        checkTrue("dragged route has no body crossing", !routing::crossesBody(afterDrag, p.obstacles, 0, 1), describe(afterDrag));
        const auto preview = edit::shiftedSegment(original, 2, offset);
        checkTrue("preview matches the committed route", preview == afterDrag, describe(preview));

        // Drag the first segment (it leaves R1's pin) down 48: pin stays, midpoint holds it.
        const auto endDrag = edit::pointsForSegmentDrag(original, {}, 0, { 0, 48 }, grid);
        checkTrue("end-segment drag pins its midpoint and far corner", endDrag.size() == 2 && endDrag[0].position == P(216, 168)
                  && endDrag[1].position == P(240, 168), describe({ endDrag.empty() ? P() : endDrag[0].position, endDrag.size() > 1 ? endDrag[1].position : P() }));
        const auto afterEnd = routeWith(endDrag, why);
        checkTrue("end-segment drag routes, starts on the pin", why.isEmpty() && !afterEnd.empty() && afterEnd.front() == P(192, 120), why + " " + describe(afterEnd));
        const auto jog = edit::shiftedSegment(original, 0, { 0, 48 });
        checkTrue("end-segment preview keeps the pin and jogs", jog.size() >= 3 && jog[0] == P(192, 120) && jog[1] == P(192, 168), describe(jog));

        // Existing points on a dragged segment move with it; others stay.
        const auto withExisting = edit::pointsForSegmentDrag(afterDrag, dragged, 3, { 0, 0 }, grid);
        checkTrue("zero drag leaves constraints as they were", withExisting.size() == 2 && withExisting[0].position == P(240, 312));

        // Insert keeps order along the wire.
        auto inserted = edit::withInsertedPoint(original, {}, { 336, 216 });
        inserted = edit::withInsertedPoint(original, inserted, { 264, 216 });
        checkTrue("inserted points are ordered along the wire", inserted.size() == 2 && inserted[0].position == P(264, 216) && inserted[1].position == P(336, 216));
        const auto withPoints = routeWith(inserted, why);
        // The points keep their exact coordinates; the free stretches next to
        // them may pick a different, equally short bend.
        checkTrue("inserted points are visited in order, no body crossing",
                  why.isEmpty() && routing::visitsInOrder(withPoints, { { 264, 216 }, { 336, 216 } }) && !routing::crossesBody(withPoints, p.obstacles, 0, 1),
                  why + " " + describe(withPoints));

        // Impossible: a point inside the block is refused (canvas restores the original).
        const auto bad = edit::withInsertedPoint(original, {}, { 336, 120 });
        const auto refused = routeWith(bad, why);
        checkTrue("point inside a body is refused", refused.empty() && why.contains("inside a component"), why);

        checkTrue("orthogonal chain turns at a corner", edit::orthogonalChain({ { 0, 0 }, { 48, 24 } }) == routing::Polyline({ { 0, 0 }, { 48, 0 }, { 48, 24 } }));
        checkTrue("segmentAt finds the bottom run", edit::segmentAt(original, { 330, 220 }, 8.0f) == 2);
    }

    std::printf("-- app-shaped obstacles: padded 0.6 grid, pins inside the obstacle --\n");
    {
        auto padded = [](routing::Obstacle o) { o.bounds = o.bounds.expanded(24.0f * 0.60f); return o; };
        routing::Problem p;
        p.obstacles = { padded(resistor(96, 96)), padded(resistor(480, 96)), padded(block()) };
        p.connections = { connect(0, 1, 1, 0, 0), connect(0, 1, 1, 0, 1, below) };
        p.connections[1].a = routing::Endpoint::forPin(0, 0); // second wire leaves R1's other end
        p.connections[1].b = routing::Endpoint::forPin(1, 1);
        std::vector<juce::String> why;
        const auto routes = routing::routeConnections(p, grid, nullptr, {}, &why);
        checkTrue("plain wire routes with pins inside padded obstacles", why[0].isEmpty() && routes[0].size() >= 2, why[0] + " " + describe(routes[0]));
        checkTrue("pinned wire routes with pins inside padded obstacles", why[1].isEmpty() && routes[1].size() >= 2, why[1] + " " + describe(routes[1]));
        checkTrue("pinned wire visits its points in order", routing::visitsInOrder(routes[1], below), describe(routes[1]));
        checkTrue("plain wire leaves/enters only its own parts", !routing::crossesBody(routes[0], p.obstacles, 0, 1), describe(routes[0]));
        checkTrue("pinned wire leaves/enters only its own parts", !routing::crossesBody(routes[1], p.obstacles, 0, 1), describe(routes[1]));
    }

    std::printf("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
