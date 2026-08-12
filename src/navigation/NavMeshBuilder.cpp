#include "navigation/NavMeshBuilder.h"

#include <chrono>
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
          epsilonSquared_((epsilon > 0.0f ? epsilon : 1e-6f) * (epsilon > 0.0f ? epsilon : 1e-6f)),
          // Cells are TWICE the weld radius, which is what makes the 8-cell probe below
          // exact. With cells of exactly one epsilon, a point can be within epsilon of a
          // point two cells away on the far side, and only the full 27-cell probe is
          // correct — 27 hash lookups per corner, and welding was 71% of a 13 ms bake.
          cellSize_(2.0f * (epsilon > 0.0f ? epsilon : 1e-6f)) {}

    int add(const glm::vec3& point, std::vector<glm::vec3>& vertices) {
        const int64_t cx = cell(point.x);
        const int64_t cy = cell(point.y);
        const int64_t cz = cell(point.z);

        // Per axis, exactly one neighbour can hold a point within epsilon: the one the
        // point sits near. Offset < epsilon reaches back, offset > cellSize - epsilon
        // reaches forward, and both cannot hold at once because cellSize is 2*epsilon.
        // So the probe is 2x2x2 = 8 cells, not 27, with identical results.
        const int64_t nx = neighbourStep(point.x, cx);
        const int64_t ny = neighbourStep(point.y, cy);
        const int64_t nz = neighbourStep(point.z, cz);

        for (int ix = 0; ix < 2; ++ix) {
            for (int iy = 0; iy < 2; ++iy) {
                for (int iz = 0; iz < 2; ++iz) {
                    const Cell probe{cx + (ix ? nx : 0), cy + (iy ? ny : 0), cz + (iz ? nz : 0)};
                    if ((ix && nx == 0) || (iy && ny == 0) || (iz && nz == 0)) {
                        continue; // no neighbour on that axis: the own-cell probe covers it
                    }
                    const auto it = buckets_.find(probe);
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
        buckets_[Cell{cx, cy, cz}].push_back(index);
        return index;
    }

private:
    int64_t cell(float value) const {
        return static_cast<int64_t>(std::floor(value / cellSize_));
    }

    // -1, 0 or +1: which neighbouring cell on this axis can still be within epsilon.
    int64_t neighbourStep(float value, int64_t cellIndex) const {
        const float offset = value - static_cast<float>(cellIndex) * cellSize_;
        if (offset < epsilon_) return -1;
        if (offset > cellSize_ - epsilon_) return 1;
        return 0;
    }

    // The bucket key is the exact cell triple. It was a formatted std::string in a
    // std::map, which meant 27 string allocations and 27 tree walks per welded corner:
    // a streamed voxel world re-baking a 112x112 surface measured 104 ms in this
    // function alone, of a 130 ms bake. Determinism does not come from the container —
    // the probe order is the fixed dx/dy/dz loop below and each bucket is
    // insertion-ordered — so an unordered_map keyed on the triple is the same bake,
    // measurably faster.
    struct Cell {
        int64_t x, y, z;
        bool operator==(const Cell& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };
    struct CellHash {
        std::size_t operator()(const Cell& cell) const {
            // 64-bit mix (splitmix64 finalizer) per axis: cell coordinates are small
            // and highly correlated, and a plain xor piles them into one bucket.
            auto mix = [](uint64_t value) {
                value += 0x9e3779b97f4a7c15ull;
                value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
                value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
                return value ^ (value >> 31);
            };
            return static_cast<std::size_t>(mix(static_cast<uint64_t>(cell.x)) ^
                                            (mix(static_cast<uint64_t>(cell.y)) << 1) ^
                                            (mix(static_cast<uint64_t>(cell.z)) << 2));
        }
    };

    float epsilon_;
    float epsilonSquared_;
    float cellSize_;
    std::unordered_map<Cell, std::vector<int>, CellHash> buckets_;
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
    if (weldMs > 0.0 || adjacencyMs > 0.0) {
        result += "; weld " + std::to_string(weldMs) + " ms, adjacency " +
                  std::to_string(adjacencyMs) + " ms, erode " + std::to_string(erodeMs) + " ms";
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
    const auto weldStart = std::chrono::steady_clock::now();

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

    const auto afterWeld = std::chrono::steady_clock::now();
    mesh.buildAdjacency();
    const auto afterAdjacency = std::chrono::steady_clock::now();

    // Erosion runs *after* adjacency, because "near a boundary" is defined by the
    // adjacency (an edge with no neighbour), and before anything plans on the mesh.
    if (params.agentRadius > 0.0f) {
        mesh = erodeByRadius(mesh, params.agentRadius, stats.erodedByRadius);
    }

    const auto afterErode = std::chrono::steady_clock::now();
    const auto elapsed = [](auto from, auto to) {
        return std::chrono::duration<double, std::milli>(to - from).count();
    };
    stats.weldMs = elapsed(weldStart, afterWeld);
    stats.adjacencyMs = elapsed(afterWeld, afterAdjacency);
    stats.erodeMs = elapsed(afterAdjacency, afterErode);

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
