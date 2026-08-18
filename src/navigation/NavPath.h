#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "navigation/NavMesh.h"

// Phase 18A — planning. Pure functions over an immutable navmesh, with no ECS and
// no state of their own:
//
//     Route = f(navmesh, start, goal)
//
// Planning is deterministic by construction, not by hope (RULES.md Rule 10): the A*
// frontier is ordered by `(f, polygon index)`, a **total** order, so the priority
// queue's lack of stability cannot leak into which of two equal-cost routes is
// returned. Two runs with the same query return byte-identical waypoints.
//
// The two passes are deliberately separate rather than one `findPath`:
//
//   1. `findCorridor` — A* over polygons, producing the sequence of polygons to
//      cross. A future local-avoidance pass (18D) needs to steer *within* this.
//   2. `stringPull`   — the funnel, turning that corridor into the shortest actual
//      line through its portals. Without it agents walk polygon-center to
//      polygon-center: the classic zig-zag that makes a correct search look broken.
//
// One is not a refinement of the other, so neither is hidden inside the other.
// See DevDocs/DESIGN_NAVIGATION.md.
namespace NavPath {

// Why a plan failed. Distinguished rather than collapsed to a bool because the
// agent's authoritative `status` records that an attempt *happened*, and "there is
// no navmesh under that name" (an asset problem, likely a missing bake) is a
// different thing for a developer to see than "no route exists" (a level-design
// problem). Rule 1: better error messages are a feature.
enum class Result {
    Success,
    EmptyNavMesh,   // the mesh has no polygons at all
    StartOffMesh,   // start could not be snapped to any polygon
    GoalOffMesh,    // goal could not be snapped to any polygon
    Unreachable     // both ends are on the mesh, but no corridor connects them
};

// A* over polygons. `start` and `goal` are snapped to the mesh first (see
// NavMesh::findNearestPolygon) and the snapped points are written back, so the
// caller plans and steers against the same points the search costed.
//
// `outCorridor` is the polygon sequence from start to goal inclusive; it holds a
// single polygon when both ends land in the same one.
// `outLinkSteps`, when given, is parallel to the corridor *transitions*: entry i is the
// index of the NavLink used to get from `outCorridor[i]` to `outCorridor[i + 1]`, or -1
// when that step crossed an ordinary shared edge. Optional so every existing caller is
// unchanged; string-pulling needs it because the funnel is only valid across shared
// portals (see the off-mesh-link addendum in DevDocs/DESIGN_NAVIGATION.md).
// How many A* corridor searches have run since the last reset.
//
// A diagnostic, in the same spirit as PhysicsWorld::lastBroadphaseCandidateCount() and
// Renderer::submittedDrawCalls(): the failure this exists to catch — N agents chasing an
// unreachable goal paying a full search each, every step — is invisible to a correctness
// test, because every one of those searches returns the right answer. Only the *count*
// shows it (DevDocs/DESIGN_REPLAN_BACKOFF.md).
//
// Not simulation state: it is never serialized, never snapshotted, and resetting it
// changes nothing about what any agent does.
std::size_t searchesPerformed();
void resetSearchCount();

Result findCorridor(const NavMesh& mesh,
                    glm::vec3& start,
                    glm::vec3& goal,
                    std::vector<int>& outCorridor,
                    std::vector<int>* outLinkSteps = nullptr);

// The simple stupid funnel algorithm: string-pulls `corridor` into the shortest
// path through its portals. `start` and `goal` must already be on the mesh (this is
// what findCorridor's write-back gives you).
//
// `outWaypoints` **excludes the start** and ends with the goal — it is the list of
// places to walk to, not a polyline of where you have been. Excluding the start is
// what keeps the agent's `pathIndex` free of an off-by-one that only shows up when
// an agent is spawned exactly on its first waypoint.
void stringPull(const NavMesh& mesh,
                const std::vector<int>& corridor,
                const glm::vec3& start,
                const glm::vec3& goal,
                std::vector<glm::vec3>& outWaypoints);

// String-pull a corridor that may cross off-mesh links. The funnel assumes every
// transition is a shared portal, so a corridor that uses links is pulled in **runs**: each
// contiguous stretch of edge-connected polygons is funnelled on its own, and the link's
// two endpoints are emitted as waypoints between runs. `linkSteps` comes from
// findCorridor's optional out-parameter; passing all -1 is identical to the overload above.
void stringPullWithLinks(const NavMesh& mesh,
                         const std::vector<int>& corridor,
                         const std::vector<int>& linkSteps,
                         const glm::vec3& start,
                         const glm::vec3& goal,
                         std::vector<glm::vec3>& outWaypoints);

// findCorridor + stringPull. The ordinary entry point; the two halves stay public
// for tests and for 18D.
Result findPath(const NavMesh& mesh,
                const glm::vec3& start,
                const glm::vec3& goal,
                std::vector<glm::vec3>& outWaypoints);

} // namespace NavPath
