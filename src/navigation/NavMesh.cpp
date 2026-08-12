#include "navigation/NavMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace {

// Squared ground-plane distance. Every navmesh query is 2D — height is carried
// along and interpolated, never searched over. A mesh with overlapping floors is a
// *baking* concern (emit separate polygons); the representation already supports it.
float distanceSquaredXZ(const glm::vec3& a, const glm::vec3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

// Closest point to `p` on segment [a, b], in XZ, with y interpolated along the
// segment so the result stays on the mesh surface.
glm::vec3 closestPointOnSegmentXZ(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    const float lengthSquared = dx * dx + dz * dz;
    if (lengthSquared <= 0.0f) {
        return a;
    }
    float t = ((p.x - a.x) * dx + (p.z - a.z) * dz) / lengthSquared;
    t = std::max(0.0f, std::min(1.0f, t));
    return a + (b - a) * t;
}

// Undirected vertex pair → one 64-bit key, so two polygons naming the same edge in
// opposite corner order still hash together.
uint64_t edgeKey(int a, int b) {
    const uint32_t low = static_cast<uint32_t>(a < b ? a : b);
    const uint32_t high = static_cast<uint32_t>(a < b ? b : a);
    return (static_cast<uint64_t>(high) << 32) | low;
}

} // namespace

void NavMesh::buildAdjacency() {
    neighbors.assign(indices.size(), -1);

    // Edges are matched by **vertex index**, not by position, so a bake must weld
    // shared corners. Position-matching would need an epsilon, and an epsilon that
    // is wrong in either direction either fuses distinct floors or silently opens a
    // seam an agent can never cross — a class of bug that is hard to see and easy to
    // avoid by making welding the bake's job.
    std::unordered_map<uint64_t, std::pair<int, int>> seen; // edge → (polygon, corner)
    seen.reserve(indices.size());

    for (int p = 0; p < polygonCount(); ++p) {
        const NavPolygon& polygon = polygons[static_cast<std::size_t>(p)];
        for (int k = 0; k < polygon.count; ++k) {
            const int i0 = indices[static_cast<std::size_t>(polygon.firstIndex + k)];
            const int i1 = indices[static_cast<std::size_t>(polygon.firstIndex + (k + 1) % polygon.count)];

            const uint64_t key = edgeKey(i0, i1);
            const auto it = seen.find(key);
            if (it == seen.end()) {
                seen.emplace(key, std::make_pair(p, k));
                continue;
            }

            const auto [otherPolygon, otherCorner] = it->second;
            neighbors[static_cast<std::size_t>(polygon.firstIndex + k)] = otherPolygon;
            neighbors[static_cast<std::size_t>(polygons[static_cast<std::size_t>(otherPolygon)].firstIndex + otherCorner)] = p;

            // A third polygon on the same edge is a malformed mesh; dropping the
            // entry means it links to nothing rather than overwriting a good link.
            seen.erase(it);
        }
    }

    // The lookup grid indexes the same polygons, so it is rebuilt here for the same
    // reason the neighbour table is: derived from the geometry at the one place the
    // geometry settles. Built BEFORE the link pass below, which queries it.
    buildLookupGrid();

    // Off-mesh links: resolve each endpoint to the polygon under it. Snapped rather than
    // required to be exactly inside, because a link endpoint is authored against the
    // *world* (the lip of a ledge, the far side of a gap) and landing a hair off the mesh
    // is normal — the same reason a destination is snapped before planning. An endpoint
    // with no polygon anywhere near it leaves -1 and the link is simply not traversable.
    for (NavLink& link : links) {
        glm::vec3 projected(0.0f);
        link.startPolygon = findNearestPolygon(link.start, projected);
        link.endPolygon = findNearestPolygon(link.end, projected);
    }
}

bool NavMesh::valid() const {
    if (neighbors.size() != indices.size()) {
        return false;
    }
    for (const NavPolygon& polygon : polygons) {
        if (polygon.count < 3 || polygon.firstIndex < 0) {
            return false;
        }
        if (static_cast<std::size_t>(polygon.firstIndex + polygon.count) > indices.size()) {
            return false;
        }
        for (int k = 0; k < polygon.count; ++k) {
            const int index = indices[static_cast<std::size_t>(polygon.firstIndex + k)];
            if (index < 0 || static_cast<std::size_t>(index) >= vertices.size()) {
                return false;
            }
        }
    }
    return true;
}

const glm::vec3& NavMesh::corner(int polygon, int k) const {
    const NavPolygon& p = polygons[static_cast<std::size_t>(polygon)];
    const int wrapped = ((k % p.count) + p.count) % p.count;
    return vertices[static_cast<std::size_t>(indices[static_cast<std::size_t>(p.firstIndex + wrapped)])];
}

void NavMesh::edge(int polygon, int k, glm::vec3& a, glm::vec3& b) const {
    a = corner(polygon, k);
    b = corner(polygon, k + 1);
}

glm::vec3 NavMesh::center(int polygon) const {
    const NavPolygon& p = polygons[static_cast<std::size_t>(polygon)];
    glm::vec3 sum(0.0f);
    for (int k = 0; k < p.count; ++k) {
        sum += corner(polygon, k);
    }
    return sum / static_cast<float>(p.count);
}

bool NavMesh::containsXZ(int polygon, const glm::vec3& point) const {
    const NavPolygon& p = polygons[static_cast<std::size_t>(polygon)];

    // Winding-agnostic: the point is inside a convex polygon when it lies on the
    // same side of every edge. Testing for one specific sign would make the result
    // depend on the bake's winding convention, and a mesh exported the other way
    // round would report everything as outside — a total failure that looks like an
    // empty navmesh rather than like a winding bug.
    bool anyPositive = false;
    bool anyNegative = false;

    for (int k = 0; k < p.count; ++k) {
        const glm::vec3& a = corner(polygon, k);
        const glm::vec3& b = corner(polygon, k + 1);
        const float cross = (b.x - a.x) * (point.z - a.z) - (b.z - a.z) * (point.x - a.x);
        // A generous epsilon: a point exactly on a shared edge must be reported
        // inside *both* polygons rather than neither, or an agent walking a portal
        // falls off the mesh for one step.
        if (cross > 1e-5f) {
            anyPositive = true;
        } else if (cross < -1e-5f) {
            anyNegative = true;
        }
        if (anyPositive && anyNegative) {
            return false;
        }
    }
    return true;
}

float NavMesh::heightAt(int polygon, const glm::vec3& point) const {
    const NavPolygon& p = polygons[static_cast<std::size_t>(polygon)];

    const glm::vec3& a = corner(polygon, 0);
    const glm::vec3& b = corner(polygon, 1);
    const glm::vec3& c = corner(polygon, 2);
    const glm::vec3 normal = glm::cross(b - a, c - a);

    if (std::fabs(normal.y) < 1e-6f) {
        // Degenerate in XZ (a zero-area or vertical polygon): no surface height to
        // solve for, so fall back to the mean corner height rather than dividing by
        // ~0 and returning an infinity that poisons every later distance.
        float sum = 0.0f;
        for (int k = 0; k < p.count; ++k) {
            sum += corner(polygon, k).y;
        }
        return sum / static_cast<float>(p.count);
    }

    return a.y - (normal.x * (point.x - a.x) + normal.z * (point.z - a.z)) / normal.y;
}


void NavMesh::buildLookupGrid() {
    lookupGrid = LookupGrid{};
    if (polygons.empty() || vertices.empty()) return;

    // Cell size from the mean polygon extent: a voxel bake's polygons are all about one
    // cell across, and a grid sized to the geometry keeps both the cells-per-polygon and
    // the polygons-per-cell counts near one.
    float minX = std::numeric_limits<float>::max(), minZ = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest(), maxZ = std::numeric_limits<float>::lowest();
    double extentSum = 0.0;
    for (int p = 0; p < polygonCount(); ++p) {
        const NavPolygon& polygon = polygons[static_cast<std::size_t>(p)];
        float pMinX = std::numeric_limits<float>::max(), pMinZ = std::numeric_limits<float>::max();
        float pMaxX = std::numeric_limits<float>::lowest(), pMaxZ = std::numeric_limits<float>::lowest();
        for (int k = 0; k < polygon.count; ++k) {
            const glm::vec3& c = corner(p, k);
            pMinX = std::min(pMinX, c.x); pMaxX = std::max(pMaxX, c.x);
            pMinZ = std::min(pMinZ, c.z); pMaxZ = std::max(pMaxZ, c.z);
        }
        minX = std::min(minX, pMinX); maxX = std::max(maxX, pMaxX);
        minZ = std::min(minZ, pMinZ); maxZ = std::max(maxZ, pMaxZ);
        extentSum += std::max(pMaxX - pMinX, pMaxZ - pMinZ);
    }

    const float meanExtent = static_cast<float>(extentSum / static_cast<double>(polygonCount()));
    float cell = meanExtent > 1e-4f ? meanExtent : 1.0f;
    // Bound the grid so a huge or pathological mesh cannot ask for a billion cells.
    constexpr int kMaxCells = 1 << 20;
    const float width = std::max(maxX - minX, 1e-3f), depth = std::max(maxZ - minZ, 1e-3f);
    while (static_cast<double>(width / cell + 1.0) * static_cast<double>(depth / cell + 1.0) >
           static_cast<double>(kMaxCells)) {
        cell *= 2.0f;
    }

    LookupGrid grid;
    grid.cellSize = cell;
    grid.minX = minX;
    grid.minZ = minZ;
    grid.cols = static_cast<int>((maxX - minX) / cell) + 1;
    grid.rows = static_cast<int>((maxZ - minZ) / cell) + 1;
    if (grid.cols <= 0 || grid.rows <= 0) return;

    // Counting sort: count per cell, prefix-sum, then fill. Polygons are visited in
    // ascending index in both passes, so each cell's list comes out sorted — which is
    // what keeps the "lowest index wins" tie-break identical to the old linear scan.
    const auto cellRange = [&](int p, int& x0, int& z0, int& x1, int& z1) {
        const NavPolygon& polygon = polygons[static_cast<std::size_t>(p)];
        float pMinX = std::numeric_limits<float>::max(), pMinZ = std::numeric_limits<float>::max();
        float pMaxX = std::numeric_limits<float>::lowest(), pMaxZ = std::numeric_limits<float>::lowest();
        for (int k = 0; k < polygon.count; ++k) {
            const glm::vec3& c = corner(p, k);
            pMinX = std::min(pMinX, c.x); pMaxX = std::max(pMaxX, c.x);
            pMinZ = std::min(pMinZ, c.z); pMaxZ = std::max(pMaxZ, c.z);
        }
        x0 = std::clamp(static_cast<int>((pMinX - grid.minX) / grid.cellSize), 0, grid.cols - 1);
        x1 = std::clamp(static_cast<int>((pMaxX - grid.minX) / grid.cellSize), 0, grid.cols - 1);
        z0 = std::clamp(static_cast<int>((pMinZ - grid.minZ) / grid.cellSize), 0, grid.rows - 1);
        z1 = std::clamp(static_cast<int>((pMaxZ - grid.minZ) / grid.cellSize), 0, grid.rows - 1);
    };

    grid.cellStart.assign(static_cast<std::size_t>(grid.cols) * grid.rows + 1, 0);
    for (int p = 0; p < polygonCount(); ++p) {
        int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        cellRange(p, x0, z0, x1, z1);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                ++grid.cellStart[static_cast<std::size_t>(z) * grid.cols + x + 1];
    }
    for (std::size_t i = 1; i < grid.cellStart.size(); ++i) {
        grid.cellStart[i] += grid.cellStart[i - 1];
    }
    std::vector<int> cursor(grid.cellStart.begin(), grid.cellStart.end() - 1);
    grid.cellPolygons.assign(static_cast<std::size_t>(grid.cellStart.back()), -1);
    for (int p = 0; p < polygonCount(); ++p) {
        int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        cellRange(p, x0, z0, x1, z1);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                grid.cellPolygons[static_cast<std::size_t>(
                    cursor[static_cast<std::size_t>(z) * grid.cols + x]++)] = p;
    }

    lookupGrid = std::move(grid);
}

int NavMesh::findContainingPolygon(const glm::vec3& point) const {
    int best = -1;
    float bestVerticalDistance = std::numeric_limits<float>::max();

    // A point can only be inside a polygon whose bounds overlap its own cell, so the
    // grid answers this exactly — no ring expansion needed.
    if (!lookupGrid.empty()) {
        const int cx = static_cast<int>((point.x - lookupGrid.minX) / lookupGrid.cellSize);
        const int cz = static_cast<int>((point.z - lookupGrid.minZ) / lookupGrid.cellSize);
        if (cx < 0 || cz < 0 || cx >= lookupGrid.cols || cz >= lookupGrid.rows) return -1;
        const std::size_t cell = static_cast<std::size_t>(cz) * lookupGrid.cols + cx;
        for (int i = lookupGrid.cellStart[cell]; i < lookupGrid.cellStart[cell + 1]; ++i) {
            const int p = lookupGrid.cellPolygons[static_cast<std::size_t>(i)];
            if (!containsXZ(p, point)) continue;
            const float vertical = std::fabs(heightAt(p, point) - point.y);
            if (vertical < bestVerticalDistance) {
                bestVerticalDistance = vertical;
                best = p;
            }
        }
        return best;
    }

    for (int p = 0; p < polygonCount(); ++p) {
        if (!containsXZ(p, point)) {
            continue;
        }
        // Vertically nearest wins, which is how a bridge over a path disambiguates
        // without the representation knowing that bridges exist.
        const float vertical = std::fabs(heightAt(p, point) - point.y);
        if (vertical < bestVerticalDistance) {
            bestVerticalDistance = vertical;
            best = p;
        }
    }
    return best;
}

int NavMesh::findNearestPolygon(const glm::vec3& point, glm::vec3& projected) const {
    const int contained = findContainingPolygon(point);
    if (contained >= 0) {
        projected = glm::vec3(point.x, heightAt(contained, point), point.z);
        return contained;
    }

    int best = -1;
    glm::vec3 bestPoint(0.0f);
    float bestDistance = std::numeric_limits<float>::max();

    // Strict `<` everywhere below keeps the lowest polygon index on a tie, so the snap is
    // deterministic when a point is equidistant from two polygons — a real case, since a
    // point off the end of a shared edge is exactly that. The grid preserves it by
    // visiting candidates in ascending index within each ring and by only *improving* on
    // a tie, never replacing one.
    const auto consider = [&](int p) {
        const NavPolygon& polygon = polygons[static_cast<std::size_t>(p)];
        for (int k = 0; k < polygon.count; ++k) {
            const glm::vec3 candidate = closestPointOnSegmentXZ(point, corner(p, k), corner(p, k + 1));
            const float distance = distanceSquaredXZ(point, candidate);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestPoint = candidate;
                best = p;
            }
        }
    };

    if (!lookupGrid.empty()) {
        // Expanding ring search. A ring is only worth visiting while the *closest
        // possible* point in it can still beat the best found so far, which is what makes
        // stopping safe rather than merely plausible: a polygon two rings out can be
        // nearer than one diagonally adjacent, and the radius test is what catches it.
        const int cx = std::clamp(static_cast<int>((point.x - lookupGrid.minX) / lookupGrid.cellSize),
                                  0, lookupGrid.cols - 1);
        const int cz = std::clamp(static_cast<int>((point.z - lookupGrid.minZ) / lookupGrid.cellSize),
                                  0, lookupGrid.rows - 1);
        const int maxRing = std::max(lookupGrid.cols, lookupGrid.rows);
        std::vector<int> ringCandidates;
        for (int ring = 0; ring <= maxRing; ++ring) {
            if (best >= 0) {
                // Nearest point of this ring, measured from the query point's own cell.
                const float ringDistance = static_cast<float>(ring - 1) * lookupGrid.cellSize;
                if (ringDistance > 0.0f && ringDistance * ringDistance > bestDistance) break;
            }
            ringCandidates.clear();
            const int x0 = cx - ring, x1 = cx + ring, z0 = cz - ring, z1 = cz + ring;
            for (int z = z0; z <= z1; ++z) {
                if (z < 0 || z >= lookupGrid.rows) continue;
                for (int x = x0; x <= x1; ++x) {
                    if (x < 0 || x >= lookupGrid.cols) continue;
                    // Ring, not filled square: the interior was visited by earlier rings.
                    if (ring > 0 && x != x0 && x != x1 && z != z0 && z != z1) continue;
                    const std::size_t cell = static_cast<std::size_t>(z) * lookupGrid.cols + x;
                    for (int i = lookupGrid.cellStart[cell]; i < lookupGrid.cellStart[cell + 1]; ++i) {
                        ringCandidates.push_back(lookupGrid.cellPolygons[static_cast<std::size_t>(i)]);
                    }
                }
            }
            // A polygon spanning several cells appears more than once; sorting also puts
            // the ring's candidates in ascending index order for the tie-break.
            std::sort(ringCandidates.begin(), ringCandidates.end());
            ringCandidates.erase(std::unique(ringCandidates.begin(), ringCandidates.end()),
                                 ringCandidates.end());
            for (const int p : ringCandidates) {
                consider(p);
            }
        }
    } else {
        for (int p = 0; p < polygonCount(); ++p) {
            consider(p);
        }
    }

    if (best >= 0) {
        projected = bestPoint;
    }
    return best;
}
