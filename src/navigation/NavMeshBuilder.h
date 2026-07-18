#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "navigation/NavMesh.h"

// Phase 18B — baking, the *pure* half. A navmesh is built from a **triangle soup in
// world space** and nothing else: no ECS, no ResourceManager, no Vulkan, no Mesh
// type. That is the whole point of the split.
//
//     NavMesh = f(triangles, params)
//
// The engine-side adapter (NavMeshBaker) harvests those triangles out of the scene's
// mesh assets and calls this. It is the same shape as GltfLoader (Engine, tinygltf)
// feeding Core's AnimationClip: the layer that knows about file formats and GPU
// buffers converts to plain data at the boundary, and the algorithm downstream never
// learns either exists.
//
// The reason this is worth insisting on: a bake that took `Mesh` (which includes
// vulkan.h) would be untestable headlessly, and Phase 18A's own coverage audit found
// exactly what that costs — the scene *load* path went untested for the engine's
// entire history because it could not run without a device. See
// docs/DESIGN_NAVIGATION.md.
struct NavTriangle {
    glm::vec3 a{0.0f};
    glm::vec3 b{0.0f};
    glm::vec3 c{0.0f};
};

struct NavBakeParams {
    // Surfaces steeper than this are not floor. Measured against +Y, so the bake
    // trusts triangle **winding** to tell a floor from a ceiling — a downward-facing
    // surface is rejected rather than silently becoming walkable ground you can
    // stand on from below. Stated because it is a real constraint on source
    // geometry, not a detail: a double-sided or inside-out mesh bakes wrong.
    float maxSlopeDegrees = 45.0f;

    // Corners closer together than this become one vertex. Load-bearing, not
    // cosmetic: NavMesh::buildAdjacency matches edges by **vertex index**, so two
    // triangles that merely *touch* geometrically share no edge until they are
    // welded — and an unwelded bake yields a mesh where every triangle is its own
    // island and every path is Unreachable.
    float weldEpsilon = 0.01f;

    // Triangles with less area than this are dropped. A zero-area triangle has no
    // usable plane, so it would poison NavMesh::heightAt and make containment
    // meaningless.
    float minTriangleArea = 1e-6f;

    // Phase 18D — agent-radius erosion. Polygons within this distance of a mesh
    // boundary are dropped, so a *point* agent planning on the eroded mesh keeps a
    // body's worth of clearance from the edge.
    //
    // **Erosion belongs before planning, and that placement is the design.** It
    // changes the traversable space itself, so it is a property of the navmesh —
    // baked once, paid once. Local avoidance (also 18D) belongs *after* planning
    // because it responds to transient conditions. Confusing the two produces a
    // planner that disagrees with the space it is planning in.
    //
    // Zero (the default) disables it: erosion that switched itself on would silently
    // delete parts of an existing navmesh on the next rebake, and a navmesh that
    // quietly shrank is far harder to diagnose than one that never eroded.
    float agentRadius = 0.0f;
};

// What the bake did. Returned rather than logged because the editor will want to
// show it (18C), tests assert on it, and "your navmesh is empty" is a far worse
// error message than "all 412 triangles were rejected by the slope filter"
// (RULES.md Rule 1 — better error messages are a feature).
struct NavBakeStats {
    int inputTriangles = 0;
    int rejectedBySlope = 0;
    int rejectedDegenerate = 0;
    int polygons = 0;
    int vertices = 0;
    int erodedByRadius = 0; // dropped for being too close to a boundary (18D)

    // Polygons with no walkable neighbour at all. Not an error — a legitimately
    // isolated platform is one — but the leading symptom of a weldEpsilon too small
    // for the source geometry, which otherwise presents as "pathfinding is broken".
    int isolatedPolygons = 0;

    std::string describe() const;
};

// **Triangles are kept as triangles — deliberately.** The obvious next step is
// merging coplanar neighbours into larger convex polygons (Recast does), and it is
// *not* done here because it buys no correctness: the funnel string-pulls a
// triangulated floor into the same straight line a merged one gives, so merging only
// shrinks the A* node count. That is an optimization, and nothing has measured the
// search yet (Rule 18). The self-test pins the claim by crossing a triangulated
// plane and asserting a single waypoint. Merge when a profile asks, not before —
// the same call 17B made about CUBICSPLINE and 17D about 2D blending.
//
// Welds, slope-filters, and builds adjacency. Deterministic: triangles are consumed
// in the order given and welded vertices are numbered by first appearance, so the
// same soup always produces byte-identical polygon and vertex ordering — which is
// what lets A* over the result stay deterministic (Rule 10).
NavMesh buildNavMesh(const std::vector<NavTriangle>& triangles,
                     const NavBakeParams& params = {},
                     NavBakeStats* outStats = nullptr);
