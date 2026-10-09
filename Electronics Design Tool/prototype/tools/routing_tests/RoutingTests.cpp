// Constrained schematic routing checks (libavoid via SchematicRouter).
// Expected results are worked out from the geometry before running: grid 24,
// R1/R2 with a blocking body between them, waypoints below the block.

#include <JuceHeader.h>
#include "../../Source/SchematicRouter.h"

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
