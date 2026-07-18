#include "navigation/NavMeshBuilder.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

// Welds corners onto a shared vertex array. Quantizes to a grid of `epsilon` cells
// and probes the 27 surrounding cells, rather than trusting the cell alone: two
// points 0.001 apart can straddle a cell boundary and would otherwise stay separate,
// which is the silent version of this bug — adjacency then fails on exactly those
// edges and a few portals in the level quietly do not exist.
class VertexWelder {
public:
    explicit VertexWelder(float epsilon)
        : epsilon_(epsilon > 0.0f ? epsilon : 1e-6f),
          epsilonSquared_((epsilon > 0.0f ? epsilon : 1e-6f) * (epsilon > 0.0f ? epsilon : 1e-6f)) {}

    int add(const glm::vec3& point, std::vector<glm::vec3>& vertices) {
        const int64_t cx = cell(point.x);
        const int64_t cy = cell(point.y);
        const int64_t cz = cell(point.z);

        for (int64_t dx = -1; dx <= 1; ++dx) {
            for (int64_t dy = -1; dy <= 1; ++dy) {
                for (int64_t dz = -1; dz <= 1; ++dz) {
                    const auto it = buckets_.find(key(cx + dx, cy + dy, cz + dz));
                    if (it == buckets_.end()) {
                        continue;
                    }
                    for (int candidate : it->second) {
                        const glm::vec3 delta = vertices[static_cast<std::size_t>(candidate)] - point;
                        if (glm::dot(delta, delta) <= epsilonSquared_) {
                            return candidate;
                        }
                    }
                }
            }
        }

        const int index = static_cast<int>(vertices.size());
        vertices.push_back(point);
        buckets_[key(cx, cy, cz)].push_back(index);
        return index;
    }

private:
    int64_t cell(float value) const {
        return static_cast<int64_t>(std::floor(value / epsilon_));
    }

    // Three cell coordinates into one map key. A std::map keyed on the tuple keeps
    // the *probe* order fixed too, so a bake is reproducible run to run and not just
    // within a run.
    static std::string key(int64_t x, int64_t y, int64_t z) {
        return std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
    }

    float epsilon_;
    float epsilonSquared_;
    std::map<std::string, std::vector<int>> buckets_;
};

// Closest point to `p` on segment [a, b] in XZ, y interpolated.
glm::vec3 closestOnSegmentXZ(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    const float lengthSquared = dx * dx + dz * dz;
    if (lengthSquared <= 0.0f) {
        return a;
    }
    float t = ((p.x - a.x) * dx + (p.z - a.z) * dz) / lengthSquared;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return a + (b - a) * t;
}

// Drops every polygon with a corner closer than `radius` to a boundary edge, then
// rebuilds the mesh from the survivors.
//
// **Granularity is polygon-level, and that is stated rather than discovered.** True
// erosion offsets the boundary inward and re-triangulates the remainder; this drops
// whole polygons instead, so it over-erodes by up to one polygon's width. That is
// honest for a triangle-soup bake and costs nothing to reason about — and the right
// fix when it is not enough is voxelization (the 18B open question), not a more
// elaborate polygon offset. Coarse and predictable beats clever and surprising.
NavMesh erodeByRadius(const NavMesh& mesh, float radius, int& outDropped) {
    // Boundary edges of the *input* mesh: an edge with no neighbour is the outside.
    std::vector<std::pair<glm::vec3, glm::vec3>> boundary;
    for (int p = 0; p < mesh.polygonCount(); ++p) {
        const NavPolygon& polygon = mesh.polygons[static_cast<std::size_t>(p)];
        for (int k = 0; k < polygon.count; ++k) {
            if (mesh.neighbors[static_cast<std::size_t>(polygon.firstIndex + k)] >= 0) {
                continue;
            }
            glm::vec3 a(0.0f);
            glm::vec3 b(0.0f);
            mesh.edge(p, k, a, b);
            boundary.emplace_back(a, b);
        }
    }

    const float radiusSquared = radius * radius;
    NavMesh eroded;
    eroded.vertices = mesh.vertices; // reuse indices; unreferenced vertices are inert
    outDropped = 0;

    for (int p = 0; p < mesh.polygonCount(); ++p) {
        const NavPolygon& polygon = mesh.polygons[static_cast<std::size_t>(p)];

        bool tooClose = false;
        for (int k = 0; k < polygon.count && !tooClose; ++k) {
            const glm::vec3& corner = mesh.corner(p, k);
            for (const auto& [a, b] : boundary) {
                const glm::vec3 closest = closestOnSegmentXZ(corner, a, b);
                const float dx = corner.x - closest.x;
                const float dz = corner.z - closest.z;
                if ((dx * dx + dz * dz) < radiusSquared) {
                    tooClose = true;
                    break;
                }
            }
        }

        if (tooClose) {
            outDropped++;
            continue;
        }

        NavPolygon kept;
        kept.firstIndex = static_cast<int>(eroded.indices.size());
        kept.count = polygon.count;
        for (int k = 0; k < polygon.count; ++k) {
            eroded.indices.push_back(mesh.indices[static_cast<std::size_t>(polygon.firstIndex + k)]);
        }
        eroded.polygons.push_back(kept);
    }

    // Rebuilt, never carried over: dropping polygons creates new boundaries, so the
    // old neighbour table would claim links to polygons that no longer exist.
    eroded.buildAdjacency();
    return eroded;
}

} // namespace

std::string NavBakeStats::describe() const {
    std::string result = "navmesh bake: " + std::to_string(polygons) + " polygons, " +
                         std::to_string(vertices) + " vertices, from " +
                         std::to_string(inputTriangles) + " triangles";
    if (rejectedBySlope > 0) {
        result += "; " + std::to_string(rejectedBySlope) + " rejected as too steep";
    }
    if (rejectedDegenerate > 0) {
        result += "; " + std::to_string(rejectedDegenerate) + " degenerate";
    }
    if (erodedByRadius > 0) {
        result += "; " + std::to_string(erodedByRadius) + " eroded for agent radius";
    }
    if (isolatedPolygons > 0) {
        result += "; " + std::to_string(isolatedPolygons) +
                  " isolated (check weldEpsilon against the source geometry)";
    }
    return result;
}

NavMesh buildNavMesh(const std::vector<NavTriangle>& triangles,
                     const NavBakeParams& params,
                     NavBakeStats* outStats) {
    NavMesh mesh;
    NavBakeStats stats;
    stats.inputTriangles = static_cast<int>(triangles.size());

    // cos of the slope limit, compared against the triangle normal's Y. Clamped so a
    // nonsensical parameter degrades to "accept only perfectly flat" rather than to
    // a NaN threshold that accepts everything.
    const float slope = params.maxSlopeDegrees < 0.0f ? 0.0f
                      : (params.maxSlopeDegrees > 90.0f ? 90.0f : params.maxSlopeDegrees);
    const float minNormalY = std::cos(slope * 3.14159265358979323846f / 180.0f);

    VertexWelder welder(params.weldEpsilon);

    for (const NavTriangle& triangle : triangles) {
        const glm::vec3 edge1 = triangle.b - triangle.a;
        const glm::vec3 edge2 = triangle.c - triangle.a;
        const glm::vec3 cross = glm::cross(edge1, edge2);

        // |cross| is twice the area — compared before normalizing, because
        // normalizing a degenerate triangle is the division by ~0 this guards.
        const float doubleArea = glm::length(cross);
        if (doubleArea * 0.5f < params.minTriangleArea) {
            stats.rejectedDegenerate++;
            continue;
        }

        // Signed, not absolute: a ceiling is not a floor you may stand on from
        // below, so winding decides up from down (see NavBakeParams).
        if ((cross.y / doubleArea) < minNormalY) {
            stats.rejectedBySlope++;
            continue;
        }

        const int i0 = welder.add(triangle.a, mesh.vertices);
        const int i1 = welder.add(triangle.b, mesh.vertices);
        const int i2 = welder.add(triangle.c, mesh.vertices);

        // Welding can collapse a thin-but-not-degenerate triangle into a line. It
        // passed the area test on its *original* corners, so it has to be re-checked
        // after welding or it enters the mesh as a polygon with a repeated index —
        // which buildAdjacency would then match against itself.
        if (i0 == i1 || i1 == i2 || i0 == i2) {
            stats.rejectedDegenerate++;
            continue;
        }

        NavPolygon polygon;
        polygon.firstIndex = static_cast<int>(mesh.indices.size());
        polygon.count = 3;
        mesh.indices.push_back(i0);
        mesh.indices.push_back(i1);
        mesh.indices.push_back(i2);
        mesh.polygons.push_back(polygon);
    }

    mesh.buildAdjacency();

    // Erosion runs *after* adjacency, because "near a boundary" is defined by the
    // adjacency (an edge with no neighbour), and before anything plans on the mesh.
    if (params.agentRadius > 0.0f) {
        mesh = erodeByRadius(mesh, params.agentRadius, stats.erodedByRadius);
    }

    stats.polygons = mesh.polygonCount();
    stats.vertices = static_cast<int>(mesh.vertices.size());
    for (int p = 0; p < mesh.polygonCount(); ++p) {
        const NavPolygon& polygon = mesh.polygons[static_cast<std::size_t>(p)];
        bool connected = false;
        for (int k = 0; k < polygon.count; ++k) {
            connected |= mesh.neighbors[static_cast<std::size_t>(polygon.firstIndex + k)] >= 0;
        }
        if (!connected) {
            stats.isolatedPolygons++;
        }
    }

    if (outStats != nullptr) {
        *outStats = stats;
    }
    return mesh;
}
