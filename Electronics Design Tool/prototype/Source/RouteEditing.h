#pragma once

#include <JuceHeader.h>

#include <vector>

// Pure geometry for manual wire-route editing: turns an editor gesture on a
// computed route into routing constraints (points), and builds the preview
// shown while dragging. It never touches connectivity; the router still owns
// the final geometry and validates it at commit.
namespace schematic::route_edit
{
using P = juce::Point<float>;
using Polyline = std::vector<P>;

struct EditPoint
{
    P position;
    bool pinned = true;
};

// Distance along the route to the nearest point on it.
float travel(const Polyline& route, P p);

// Index of the segment within tolerance of p (nearest), or -1.
int segmentAt(const Polyline& route, P p, float tolerance);

// Offset that moves segment `segment` towards `drag`, perpendicular to it,
// snapped to whole grid steps. Zero for a degenerate segment.
P segmentOffset(const Polyline& route, int segment, P drag, float grid);

// The route with one segment shifted by offset, kept orthogonal (a jog is
// added where an end segment leaves a fixed pin). For the drag preview.
Polyline shiftedSegment(const Polyline& route, int segment, P offset);

// Constraints after dragging `segment` by offset: the segment's moved corners
// become pinned points (for an end segment, its moved midpoint stands in for
// the corner at the pin), existing points on the segment move with it, all
// other points stay; ordered along the route, duplicates removed.
std::vector<EditPoint> pointsForSegmentDrag(const Polyline& route, const std::vector<EditPoint>& existing,
                                            int segment, P offset, float grid);

// Existing points plus a new pinned point at p, ordered along the route.
std::vector<EditPoint> withInsertedPoint(const Polyline& route, const std::vector<EditPoint>& existing, P p);

// Orthogonal L-chain through the given points, for previews.
Polyline orthogonalChain(const std::vector<P>& points);
}
