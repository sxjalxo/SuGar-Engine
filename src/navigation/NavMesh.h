#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

// Phase 18A — the navmesh asset. **Immutable data, not state**: polygons never
// change at runtime, so a navmesh is never snapshotted. Agents reference one by
// name (NavAgentComponent::navMesh → NavMeshRegistry) and the routes planned over
// it are a pure function of it:
//
//     Route = f(navmesh, start, goal)
//
// This header is pure data + pure math, which is exactly why it lives in Core: no
// Vulkan, no ResourceManager, and therefore testable headlessly (RULES.md Rule 9,
// Rule 15). See docs/DESIGN_NAVIGATION.md.

// One convex polygon, indexing into NavMesh::indices the same way a submesh
// indexes a vertex buffer — one allocation for the whole mesh rather than a
// vector per polygon.
//
// **Convexity is load-bearing**, not a tidiness preference: it is what makes "a
// straight line inside one polygon is always walkable" true, which is what makes
// the funnel string-pull in NavPath.h correct rather than merely plausible.
struct NavPolygon {
    int firstIndex = 0; // offset into NavMesh::indices
    int count = 0;      // corner count (>= 3)
};

// An off-mesh connection: a traversable edge between two points that share no polygon
// edge — a jump across a trench, a drop off a ledge, a ladder (M4 L3, see the addendum in
// docs/DESIGN_NAVIGATION.md).
//
// A link is **mesh data**, not a second graph: it lives on the NavMesh, the search treats
// it as one more kind of edge, and the funnel splits its corridor around it. The endpoints
// are authored by whoever built the mesh (the *game* knows a one-block gap is jumpable;
// the engine would have to guess); the polygons they land in are **derived** and resolved
// by buildAdjacency, for exactly the reason `neighbors` is — an asset that carried its own
// resolved indices could carry stale ones.
struct NavLink {
    glm::vec3 start{0.0f};
    glm::vec3 end{0.0f};

    // What crossing costs the search. Zero charges the 3D distance between the endpoints;
    // a larger value makes the planner prefer walking around, which is how a game says
    // "climbing this is expensive" without a second cost model.
    float cost = 0.0f;

    // False makes it one-way (`start` → `end`). A drop off a cliff is one-way: you can
    // fall down it and not climb back up.
    bool bidirectional = true;

    // Derived by buildAdjacency; -1 when an endpoint is not on the mesh (a link authored
    // against geometry that later changed simply stops being traversable, rather than
    // pointing at whatever polygon now occupies that index).
    int startPolygon = -1;
    int endPolygon = -1;
};

struct NavMesh {
    // Shared corner positions. `y` is carried through every query so agents follow
    // slopes; the queries themselves are 2D (XZ), see below.
    std::vector<glm::vec3> vertices;

    // Polygon corners, concatenated. Corner k of polygon p is
    // vertices[indices[p.firstIndex + k]].
    std::vector<int> indices;

    std::vector<NavPolygon> polygons;

    // Parallel to `indices`: neighbors[p.firstIndex + k] is the polygon across the
    // edge from corner k to corner k+1, or -1 at a boundary.
    //
    // Derived from the geometry by buildAdjacency() rather than stored in the asset
    // file. An asset that carried its own adjacency could carry a *stale* copy — a
    // second source of truth about the same geometry, which is Rule 21's problem
    // wearing an asset costume.
    std::vector<int> neighbors;

    // Off-mesh connections. Authored (by the bake's caller); their polygon indices are
    // resolved by buildAdjacency.
    std::vector<NavLink> links;

    // Matches every pair of polygons sharing an undirected vertex pair, and resolves each
    // link's endpoints to the polygons that contain them. Must be called after any change
    // to polygons/indices/links; loading paths call it for you.
    void buildAdjacency();

    bool empty() const { return polygons.empty(); }

    // Structurally sound: every polygon has at least 3 corners, every index is in
    // range, and the adjacency array is the right size. Cheap enough to assert on
    // load, which is where a malformed bake should die rather than three seconds
    // later inside A*.
    bool valid() const;

    int polygonCount() const { return static_cast<int>(polygons.size()); }

    // Corner k of polygon `polygon` (k is taken modulo the corner count, so callers
    // can ask for `k + 1` without wrapping by hand).
    const glm::vec3& corner(int polygon, int k) const;

    // Endpoints of edge k — the edge from corner k to corner k+1, i.e. the one whose
    // neighbor is neighbors[firstIndex + k].
    void edge(int polygon, int k, glm::vec3& a, glm::vec3& b) const;

    // Average of the corners. Used as the A* cost reference point and to orient
    // portals; not a centroid in the exact area-weighted sense, and does not need
    // to be — both uses only need a point strictly inside a convex polygon.
    glm::vec3 center(int polygon) const;

    // Ground-plane containment (XZ). Winding-agnostic: it asks whether the point is
    // on the same side of every edge, which holds for clockwise and
    // counter-clockwise polygons alike, so a bake's winding convention cannot
    // silently invert the test.
    bool containsXZ(int polygon, const glm::vec3& point) const;

    // The polygon's surface height at (point.x, point.z), from the plane through its
    // first three corners. Falls back to the mean corner height for a polygon that
    // is degenerate in XZ (which valid() does not reject — a zero-area polygon is
    // useless but not corrupt).
    float heightAt(int polygon, const glm::vec3& point) const;

    // The polygon containing `point` in XZ, preferring the one whose surface is
    // vertically closest — which is how stacked floors (a bridge over a path)
    // disambiguate without the representation needing to know about them.
    // -1 when the point is outside every polygon.
    int findContainingPolygon(const glm::vec3& point) const;

    // findContainingPolygon, but never fails on a non-empty mesh: when the point is
    // off-mesh it returns the polygon with the nearest boundary point and writes
    // that boundary point to `projected`.
    //
    // This exists because a *destination* is gameplay's, and gameplay is entitled to
    // name a point slightly off the mesh (a click, a spawn marker, a shot-at
    // position). Snapping is the navigation system's job, not the caller's — the
    // alternative is every behavior in the game reimplementing it slightly wrong.
    int findNearestPolygon(const glm::vec3& point, glm::vec3& projected) const;
};
