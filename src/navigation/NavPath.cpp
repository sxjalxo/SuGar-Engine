#include "navigation/NavPath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <queue>
#include <vector>

namespace {

constexpr float kInfinity = std::numeric_limits<float>::max();

// Signed area of the triangle projected to the ground plane, in the orientation the
// funnel below is written against: with the apex at the origin, a point to the left
// of travel and a point to the right give a **positive** value. Every side test in
// stringPull is expressed through this one function so there is exactly one place a
// sign convention can be wrong — and exactly one place the self-test pins.
float triArea2(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const float ax = b.x - a.x;
    const float az = b.z - a.z;
    const float bx = c.x - a.x;
    const float bz = c.z - a.z;
    return bx * az - ax * bz;
}

bool equalXZ(const glm::vec3& a, const glm::vec3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return (dx * dx + dz * dz) < 1e-8f;
}

// A* frontier entry. `polygon` is carried so ties break on it — see the header.
struct OpenNode {
    float f = 0.0f;
    int polygon = -1;
};

// Total order: lower f first, then lower polygon index. std::priority_queue is not
// a stable container, so a comparator that only looked at `f` would let two runs
// expand equal-cost polygons in different orders and return different (equally
// optimal) routes — a determinism break that no single run can show you.
struct OpenNodeGreater {
    bool operator()(const OpenNode& a, const OpenNode& b) const {
        if (a.f != b.f) {
            return a.f > b.f;
        }
        return a.polygon > b.polygon;
    }
};

// The corner index of `polygon` whose edge leads to `neighbor`, or -1.
int findSharedEdge(const NavMesh& mesh, int polygon, int neighbor) {
    const NavPolygon& p = mesh.polygons[static_cast<std::size_t>(polygon)];
    for (int k = 0; k < p.count; ++k) {
        if (mesh.neighbors[static_cast<std::size_t>(p.firstIndex + k)] == neighbor) {
            return k;
        }
    }
    return -1;
}

// The two endpoints of the portal between `from` and `to`, ordered (left, right)
// relative to the direction of travel.
//
// Ordered geometrically rather than by polygon winding: a bake that emits clockwise
// polygons would otherwise mirror every funnel decision and produce paths that hug
// the *outside* of corners — legal-looking output that is quietly wrong. The
// crossing direction cannot be parallel to the portal, because the two polygon
// centers lie strictly on opposite sides of their shared edge, so the test never
// lands on zero.
bool portalPoints(const NavMesh& mesh, int from, int to, glm::vec3& left, glm::vec3& right) {
    const int edgeIndex = findSharedEdge(mesh, from, to);
    if (edgeIndex < 0) {
        return false;
    }

    glm::vec3 a(0.0f);
    glm::vec3 b(0.0f);
    mesh.edge(from, edgeIndex, a, b);

    const glm::vec3 direction = mesh.center(to) - mesh.center(from);
    const glm::vec3 midpoint = (a + b) * 0.5f;
    const glm::vec3 toA = a - midpoint;

    // Left iff `a` is counter-clockwise from the direction of travel in XZ.
    const float cross = direction.x * toA.z - direction.z * toA.x;
    if (cross > 0.0f) {
        left = a;
        right = b;
    } else {
        left = b;
        right = a;
    }
    return true;
}

} // namespace

namespace NavPath {

Result findCorridor(const NavMesh& mesh,
                    glm::vec3& start,
                    glm::vec3& goal,
                    std::vector<int>& outCorridor) {
    outCorridor.clear();

    if (mesh.empty()) {
        return Result::EmptyNavMesh;
    }

    // Snap both ends before costing anything, and write them back: gameplay is
    // entitled to name a destination slightly off the mesh, and the caller must
    // steer toward the same point the search planned to.
    glm::vec3 snappedStart(0.0f);
    const int startPolygon = mesh.findNearestPolygon(start, snappedStart);
    if (startPolygon < 0) {
        return Result::StartOffMesh;
    }

    glm::vec3 snappedGoal(0.0f);
    const int goalPolygon = mesh.findNearestPolygon(goal, snappedGoal);
    if (goalPolygon < 0) {
        return Result::GoalOffMesh;
    }

    start = snappedStart;
    goal = snappedGoal;

    if (startPolygon == goalPolygon) {
        outCorridor.push_back(startPolygon);
        return Result::Success;
    }

    const std::size_t polygonCount = mesh.polygons.size();
    std::vector<float> cost(polygonCount, kInfinity);
    std::vector<int> cameFrom(polygonCount, -1);
    std::vector<glm::vec3> entryPoint(polygonCount, glm::vec3(0.0f));
    std::vector<bool> closed(polygonCount, false);

    // All scratch, all stack-local: the search owns no state that outlives the call
    // (docs/DESIGN_NAVIGATION.md — open/closed sets are derived, by definition).
    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenNodeGreater> open;

    cost[static_cast<std::size_t>(startPolygon)] = 0.0f;
    entryPoint[static_cast<std::size_t>(startPolygon)] = start;
    open.push({ glm::distance(start, goal), startPolygon });

    bool reached = false;
    while (!open.empty()) {
        const OpenNode node = open.top();
        open.pop();

        const std::size_t current = static_cast<std::size_t>(node.polygon);
        if (closed[current]) {
            continue; // lazy deletion — a stale entry for an already-expanded polygon
        }
        closed[current] = true;

        if (node.polygon == goalPolygon) {
            reached = true;
            break;
        }

        const NavPolygon& polygon = mesh.polygons[current];
        for (int k = 0; k < polygon.count; ++k) {
            const int neighbor = mesh.neighbors[static_cast<std::size_t>(polygon.firstIndex + k)];
            if (neighbor < 0 || closed[static_cast<std::size_t>(neighbor)]) {
                continue;
            }

            glm::vec3 a(0.0f);
            glm::vec3 b(0.0f);
            mesh.edge(node.polygon, k, a, b);
            const glm::vec3 crossing = (a + b) * 0.5f;

            // Costed between **portal midpoints**, not polygon centers: a centroid
            // measure over-charges long thin polygons and picks visibly silly
            // corridors through them. Distance is 3D, so a slope costs more than the
            // flat route it shortcuts.
            const float tentative = cost[current] + glm::distance(entryPoint[current], crossing);
            if (tentative >= cost[static_cast<std::size_t>(neighbor)]) {
                continue;
            }

            cost[static_cast<std::size_t>(neighbor)] = tentative;
            cameFrom[static_cast<std::size_t>(neighbor)] = node.polygon;
            entryPoint[static_cast<std::size_t>(neighbor)] = crossing;
            open.push({ tentative + glm::distance(crossing, goal), neighbor });
        }
    }

    if (!reached) {
        return Result::Unreachable;
    }

    for (int polygon = goalPolygon; polygon != -1; polygon = cameFrom[static_cast<std::size_t>(polygon)]) {
        outCorridor.push_back(polygon);
        if (polygon == startPolygon) {
            break;
        }
    }
    std::reverse(outCorridor.begin(), outCorridor.end());
    return Result::Success;
}

void stringPull(const NavMesh& mesh,
                const std::vector<int>& corridor,
                const glm::vec3& start,
                const glm::vec3& goal,
                std::vector<glm::vec3>& outWaypoints) {
    outWaypoints.clear();

    if (corridor.size() < 2) {
        // Same polygon: it is convex, so the straight line is walkable by
        // definition. This is the one place convexity is used directly rather than
        // through the funnel.
        outWaypoints.push_back(goal);
        return;
    }

    // Portals first, so the funnel below is a pure loop over data rather than a loop
    // that also re-derives geometry.
    std::vector<glm::vec3> lefts;
    std::vector<glm::vec3> rights;
    lefts.reserve(corridor.size());
    rights.reserve(corridor.size());

    for (std::size_t i = 0; i + 1 < corridor.size(); ++i) {
        glm::vec3 left(0.0f);
        glm::vec3 right(0.0f);
        if (!portalPoints(mesh, corridor[i], corridor[i + 1], left, right)) {
            // The corridor named two polygons that do not share an edge, which means
            // the adjacency and the search disagree. Bail to the goal rather than
            // emitting a path through geometry we cannot justify.
            outWaypoints.push_back(goal);
            return;
        }
        lefts.push_back(left);
        rights.push_back(right);
    }

    // The goal as a degenerate final portal, which is what closes the funnel — the
    // last real portal does not, on its own, decide the last corner.
    lefts.push_back(goal);
    rights.push_back(goal);

    glm::vec3 apex = start;
    glm::vec3 portalLeft = start;
    glm::vec3 portalRight = start;
    std::size_t apexIndex = 0;
    std::size_t leftIndex = 0;
    std::size_t rightIndex = 0;

    for (std::size_t i = 0; i < lefts.size(); ++i) {
        const glm::vec3& left = lefts[i];
        const glm::vec3& right = rights[i];

        // Tighten the right side of the funnel.
        if (triArea2(apex, portalRight, right) <= 0.0f) {
            if (equalXZ(apex, portalRight) || triArea2(apex, portalLeft, right) > 0.0f) {
                portalRight = right;
                rightIndex = i;
            } else {
                // Right crossed over left: the left endpoint is a corner of the
                // shortest path. Emit it, restart the funnel from there.
                outWaypoints.push_back(portalLeft);
                apex = portalLeft;
                apexIndex = leftIndex;
                portalLeft = apex;
                portalRight = apex;
                leftIndex = apexIndex;
                rightIndex = apexIndex;
                i = apexIndex; // ++i resumes at the portal after the new apex
                continue;
            }
        }

        // Tighten the left side of the funnel.
        if (triArea2(apex, portalLeft, left) >= 0.0f) {
            if (equalXZ(apex, portalLeft) || triArea2(apex, portalRight, left) < 0.0f) {
                portalLeft = left;
                leftIndex = i;
            } else {
                outWaypoints.push_back(portalRight);
                apex = portalRight;
                apexIndex = rightIndex;
                portalLeft = apex;
                portalRight = apex;
                leftIndex = apexIndex;
                rightIndex = apexIndex;
                i = apexIndex;
                continue;
            }
        }
    }

    if (outWaypoints.empty() || !equalXZ(outWaypoints.back(), goal)) {
        outWaypoints.push_back(goal);
    }
}

Result findPath(const NavMesh& mesh,
                const glm::vec3& start,
                const glm::vec3& goal,
                std::vector<glm::vec3>& outWaypoints) {
    outWaypoints.clear();

    glm::vec3 snappedStart = start;
    glm::vec3 snappedGoal = goal;
    std::vector<int> corridor;

    const Result result = findCorridor(mesh, snappedStart, snappedGoal, corridor);
    if (result != Result::Success) {
        return result;
    }

    stringPull(mesh, corridor, snappedStart, snappedGoal, outWaypoints);
    return Result::Success;
}

} // namespace NavPath
