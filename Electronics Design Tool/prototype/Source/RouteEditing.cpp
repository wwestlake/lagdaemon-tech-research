#include "RouteEditing.h"

#include <algorithm>
#include <cmath>

namespace schematic::route_edit
{
namespace
{
P nearestOnSegment(P a, P b, P p)
{
    const auto ab = b - a;
    const auto length2 = ab.x * ab.x + ab.y * ab.y;
    if (length2 <= 0.0f)
        return a;
    const auto t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / length2, 0.0f, 1.0f);
    return a + ab * t;
}

P snap(P p, float grid)
{
    return { std::round(p.x / grid) * grid, std::round(p.y / grid) * grid };
}

void addUnique(std::vector<std::pair<float, EditPoint>>& list, float at, EditPoint point)
{
    for (const auto& [t, existing] : list)
        if (existing.position.getDistanceFrom(point.position) < 0.5f)
            return;
    list.push_back({ at, point });
}

std::vector<EditPoint> ordered(std::vector<std::pair<float, EditPoint>> list)
{
    std::stable_sort(list.begin(), list.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<EditPoint> out;
    for (const auto& [t, point] : list)
        out.push_back(point);
    return out;
}
}

float travel(const Polyline& route, P p)
{
    float best = std::numeric_limits<float>::max(), bestTravel = 0.0f, before = 0.0f;
    for (size_t s = 0; s + 1 < route.size(); ++s)
    {
        const auto q = nearestOnSegment(route[s], route[s + 1], p);
        const auto d = q.getDistanceFrom(p);
        if (d < best - 0.01f)
        {
            best = d;
            bestTravel = before + route[s].getDistanceFrom(q);
        }
        before += route[s].getDistanceFrom(route[s + 1]);
    }
    return bestTravel;
}

int segmentAt(const Polyline& route, P p, float tolerance)
{
    int found = -1;
    float best = tolerance;
    for (size_t s = 0; s + 1 < route.size(); ++s)
    {
        const auto d = nearestOnSegment(route[s], route[s + 1], p).getDistanceFrom(p);
        if (d <= best)
        {
            best = d;
            found = (int)s;
        }
    }
    return found;
}

P segmentOffset(const Polyline& route, int segment, P drag, float grid)
{
    if (segment < 0 || segment + 1 >= (int)route.size())
        return {};
    const auto a = route[(size_t)segment], b = route[(size_t)segment + 1];
    const bool horizontal = std::abs(a.y - b.y) < 0.5f;
    const bool vertical = std::abs(a.x - b.x) < 0.5f;
    if (horizontal == vertical) // zero-length or diagonal
        return {};
    return horizontal ? P(0.0f, std::round(drag.y / grid) * grid) : P(std::round(drag.x / grid) * grid, 0.0f);
}

Polyline shiftedSegment(const Polyline& route, int segment, P offset)
{
    if (segment < 0 || segment + 1 >= (int)route.size() || offset == P())
        return route;
    Polyline out;
    for (int i = 0; i < (int)route.size(); ++i)
    {
        const bool moved = i == segment || i == segment + 1;
        if (moved && i == 0)
            out.push_back(route.front()); // the pin stays; jog out from it
        out.push_back(moved ? route[(size_t)i] + offset : route[(size_t)i]);
        if (moved && i == (int)route.size() - 1)
            out.push_back(route.back());
    }
    return out;
}

std::vector<EditPoint> pointsForSegmentDrag(const Polyline& route, const std::vector<EditPoint>& existing,
                                            int segment, P offset, float grid)
{
    if (segment < 0 || segment + 1 >= (int)route.size() || offset == P())
        return existing;
    const auto a = route[(size_t)segment], b = route[(size_t)segment + 1];
    const bool startsAtEnd = segment == 0;
    const bool endsAtEnd = segment + 2 == (int)route.size();

    std::vector<std::pair<float, EditPoint>> list;
    for (const auto& point : existing)
    {
        const bool onSegment = nearestOnSegment(a, b, point.position).getDistanceFrom(point.position) < 0.5f;
        addUnique(list, travel(route, point.position), { onSegment ? point.position + offset : point.position, point.pinned });
    }
    // The pin ends cannot move, so an end segment is held by its midpoint.
    const auto mid = snap((a + b) * 0.5f, grid);
    if (startsAtEnd || endsAtEnd)
        addUnique(list, travel(route, mid), { mid + offset, true });
    if (!startsAtEnd)
        addUnique(list, travel(route, a), { a + offset, true });
    if (!endsAtEnd)
        addUnique(list, travel(route, b), { b + offset, true });
    return ordered(std::move(list));
}

std::vector<EditPoint> withInsertedPoint(const Polyline& route, const std::vector<EditPoint>& existing, P p)
{
    std::vector<std::pair<float, EditPoint>> list;
    for (const auto& point : existing)
        list.push_back({ travel(route, point.position), point });
    addUnique(list, travel(route, p), { p, true });
    return ordered(std::move(list));
}

Polyline orthogonalChain(const std::vector<P>& points)
{
    Polyline out;
    for (const auto& p : points)
    {
        if (!out.empty() && std::abs(out.back().x - p.x) > 0.5f && std::abs(out.back().y - p.y) > 0.5f)
            out.push_back({ p.x, out.back().y });
        out.push_back(p);
    }
    return out;
}
}
