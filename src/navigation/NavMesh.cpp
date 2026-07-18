#include "navigation/NavMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>

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

int NavMesh::findContainingPolygon(const glm::vec3& point) const {
    int best = -1;
    float bestVerticalDistance = std::numeric_limits<float>::max();

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

    for (int p = 0; p < polygonCount(); ++p) {
        const NavPolygon& polygon = polygons[static_cast<std::size_t>(p)];
        for (int k = 0; k < polygon.count; ++k) {
            const glm::vec3 candidate = closestPointOnSegmentXZ(point, corner(p, k), corner(p, k + 1));
            const float distance = distanceSquaredXZ(point, candidate);
            // Strict `<` keeps the lowest polygon index on a tie, so the snap is
            // deterministic when a point is equidistant from two polygons — a real
            // case, since a point off the end of a shared edge is exactly that.
            if (distance < bestDistance) {
                bestDistance = distance;
                bestPoint = candidate;
                best = p;
            }
        }
    }

    if (best >= 0) {
        projected = bestPoint;
    }
    return best;
}
