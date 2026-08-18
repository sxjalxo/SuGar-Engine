#include "navigation/NavPath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
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

std::size_t g_searchCount = 0;

std::size_t searchesPerformed() {
    return g_searchCount;
}

void resetSearchCount() {
    g_searchCount = 0;
}

Result findCorridor(const NavMesh& mesh,
                    glm::vec3& start,
                    glm::vec3& goal,
                    std::vector<int>& outCorridor,
                    std::vector<int>* outLinkSteps) {
    g_searchCount++;
    outCorridor.clear();
    if (outLinkSteps != nullptr) {
        outLinkSteps->clear();
    }

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
    // Which link was crossed to reach this polygon, or -1 for an ordinary shared edge.
    // Parallel to cameFrom, so unwinding the corridor unwinds the link usage with it.
    std::vector<int> cameByLink(polygonCount, -1);
    std::vector<glm::vec3> entryPoint(polygonCount, glm::vec3(0.0f));
    std::vector<bool> closed(polygonCount, false);

    // All scratch, all stack-local: the search owns no state that outlives the call
    // (DevDocs/DESIGN_NAVIGATION.md — open/closed sets are derived, by definition).
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
            cameByLink[static_cast<std::size_t>(neighbor)] = -1;
            entryPoint[static_cast<std::size_t>(neighbor)] = crossing;
            open.push({ tentative + glm::distance(crossing, goal), neighbor });
        }

        // Off-mesh links leaving this polygon — one more kind of edge, expanded after the
        // shared ones and in index order, so the frontier's total order is untouched and
        // two runs still return the same corridor.
        for (std::size_t linkIndex = 0; linkIndex < mesh.links.size(); ++linkIndex) {
            const NavLink& link = mesh.links[linkIndex];
            if (link.startPolygon < 0 || link.endPolygon < 0) {
                continue; // an endpoint resolved to nothing: not traversable
            }

            int fromPolygon = -1;
            glm::vec3 enter(0.0f);
            glm::vec3 exit(0.0f);
            if (link.startPolygon == node.polygon) {
                fromPolygon = link.endPolygon;
                enter = link.start;
                exit = link.end;
            } else if (link.bidirectional && link.endPolygon == node.polygon) {
                fromPolygon = link.startPolygon;
                enter = link.end;
                exit = link.start;
            } else {
                continue;
            }
            if (fromPolygon == node.polygon || closed[static_cast<std::size_t>(fromPolygon)]) {
                continue;
            }

            // Walk to the link's near endpoint, then pay the crossing: zero means charge
            // the distance, a set cost means the game decided what climbing is worth.
            const float crossingCost = link.cost > 0.0f ? link.cost : glm::distance(enter, exit);
            const float tentative = cost[current] + glm::distance(entryPoint[current], enter) + crossingCost;
            if (tentative >= cost[static_cast<std::size_t>(fromPolygon)]) {
                continue;
            }

            cost[static_cast<std::size_t>(fromPolygon)] = tentative;
            cameFrom[static_cast<std::size_t>(fromPolygon)] = node.polygon;
            cameByLink[static_cast<std::size_t>(fromPolygon)] = static_cast<int>(linkIndex);
            entryPoint[static_cast<std::size_t>(fromPolygon)] = exit;
            open.push({ tentative + glm::distance(exit, goal), fromPolygon });
        }
    }

    if (!reached) {
        return Result::Unreachable;
    }

    std::vector<int> reversedLinks;
    for (int polygon = goalPolygon; polygon != -1; polygon = cameFrom[static_cast<std::size_t>(polygon)]) {
        outCorridor.push_back(polygon);
        if (polygon == startPolygon) {
            break;
        }
        reversedLinks.push_back(cameByLink[static_cast<std::size_t>(polygon)]);
    }
    std::reverse(outCorridor.begin(), outCorridor.end());
    if (outLinkSteps != nullptr) {
        outLinkSteps->assign(reversedLinks.rbegin(), reversedLinks.rend());
    }
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
            // the adjacency and the search disagree — a navmesh bake/adjacency bug.
            // Bail to the goal rather than emitting a path through geometry we cannot
            // justify, but say so: a silent straight line that cuts through a wall is
            // exactly the kind of "legal-looking output that is quietly wrong" this
            // file is written to avoid, and a developer needs a reason to rebake.
            std::cerr << "[nav] corridor polygons " << corridor[i] << " and "
                      << corridor[i + 1] << " share no edge; adjacency disagrees with "
                      << "the search — steering straight to goal (rebake the navmesh)\n";
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

void stringPullWithLinks(const NavMesh& mesh,
                         const std::vector<int>& corridor,
                         const std::vector<int>& linkSteps,
                         const glm::vec3& start,
                         const glm::vec3& goal,
                         std::vector<glm::vec3>& outWaypoints) {
    outWaypoints.clear();

    // No link in this corridor: the ordinary funnel is exactly right, and taking the
    // split path would only risk differing on a case that has one obvious answer.
    const bool usesLink = std::any_of(linkSteps.begin(), linkSteps.end(),
                                      [](int step) { return step >= 0; });
    if (!usesLink) {
        stringPull(mesh, corridor, start, goal, outWaypoints);
        return;
    }

    // Otherwise pull each edge-connected run on its own. The funnel is only valid across
    // shared portals; a link is not one, so it terminates the run — and its two endpoints
    // become waypoints, which is precisely "walk to the ledge, then cross".
    glm::vec3 runStart = start;
    std::size_t runBegin = 0;
    std::vector<glm::vec3> runWaypoints;

    for (std::size_t step = 0; step <= linkSteps.size(); ++step) {
        const bool lastStep = step == linkSteps.size();
        const int linkIndex = lastStep ? -1 : linkSteps[step];
        if (!lastStep && linkIndex < 0) {
            continue; // ordinary edge: the run continues
        }

        const std::vector<int> run(corridor.begin() + static_cast<std::ptrdiff_t>(runBegin),
                                   corridor.begin() + static_cast<std::ptrdiff_t>(step) + 1);
        if (lastStep) {
            stringPull(mesh, run, runStart, goal, runWaypoints);
            outWaypoints.insert(outWaypoints.end(), runWaypoints.begin(), runWaypoints.end());
            return;
        }

        const NavLink& link = mesh.links[static_cast<std::size_t>(linkIndex)];
        // Which end of the link this crossing enters by: the corridor tells us which
        // polygon we are leaving, and a bidirectional link is traversed either way.
        const bool forward = link.startPolygon == corridor[step];
        const glm::vec3 enter = forward ? link.start : link.end;
        const glm::vec3 exit = forward ? link.end : link.start;

        stringPull(mesh, run, runStart, enter, runWaypoints);
        outWaypoints.insert(outWaypoints.end(), runWaypoints.begin(), runWaypoints.end());
        outWaypoints.push_back(exit);

        runStart = exit;
        runBegin = step + 1;
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
    std::vector<int> linkSteps;

    const Result result = findCorridor(mesh, snappedStart, snappedGoal, corridor, &linkSteps);
    if (result != Result::Success) {
        return result;
    }

    stringPullWithLinks(mesh, corridor, linkSteps, snappedStart, snappedGoal, outWaypoints);
    return Result::Success;
}

} // namespace NavPath
