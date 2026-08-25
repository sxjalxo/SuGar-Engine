#include "physics/PhysicsWorld.h"
#include "ecs/Registry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

namespace {

// World-space collider derived from a transform. Boxes stay axis-aligned (v1
// ignores rotation); the collider's local extents/radius are scaled by the
// transform scale.
struct WorldShape {
    Entity entity = INVALID_ENTITY;
    ColliderType type = ColliderType::Box;
    glm::vec3 center{0.0f};
    glm::vec3 halfExtents{0.0f}; // box
    float radius = 0.0f;         // sphere
    bool isTrigger = false;
    uint32_t layer = 0xFFFFFFFFu;
    uint32_t mask = 0xFFFFFFFFu;
};

// Two colliders interact only if each one's mask includes the other's layer.
inline bool layersInteract(const WorldShape& a, const WorldShape& b) {
    return (a.mask & b.layer) != 0u && (b.mask & a.layer) != 0u;
}

// Contact normal always points from shape A to shape B.
struct Contact {
    bool hit = false;
    glm::vec3 normal{0.0f};
    float penetration = 0.0f;
    glm::vec3 point{0.0f}; // world-space contact point (approx), for gameplay events
};

WorldShape makeWorldShape(Entity entity, const Transform& transform, const ColliderComponent& collider) {
    WorldShape shape;
    shape.entity = entity;
    shape.type = collider.type;
    shape.center = transform.position;
    shape.halfExtents = collider.halfExtents * glm::abs(transform.scale);
    const float maxScale = std::max({std::abs(transform.scale.x),
                                     std::abs(transform.scale.y),
                                     std::abs(transform.scale.z)});
    shape.radius = collider.radius * maxScale;
    shape.isTrigger = collider.isTrigger;
    shape.layer = collider.layer;
    shape.mask = collider.mask;
    return shape;
}

Contact testBoxBox(const WorldShape& a, const WorldShape& b) {
    Contact contact;
    const glm::vec3 aMin = a.center - a.halfExtents;
    const glm::vec3 aMax = a.center + a.halfExtents;
    const glm::vec3 bMin = b.center - b.halfExtents;
    const glm::vec3 bMax = b.center + b.halfExtents;

    const glm::vec3 overlap(
        std::min(aMax.x, bMax.x) - std::max(aMin.x, bMin.x),
        std::min(aMax.y, bMax.y) - std::max(aMin.y, bMin.y),
        std::min(aMax.z, bMax.z) - std::max(aMin.z, bMin.z));

    if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f) {
        return contact;
    }

    // Resolve along the axis of least penetration.
    float penetration = overlap.x;
    glm::vec3 normal((b.center.x < a.center.x) ? -1.0f : 1.0f, 0.0f, 0.0f);
    if (overlap.y < penetration) {
        penetration = overlap.y;
        normal = glm::vec3(0.0f, (b.center.y < a.center.y) ? -1.0f : 1.0f, 0.0f);
    }
    if (overlap.z < penetration) {
        penetration = overlap.z;
        normal = glm::vec3(0.0f, 0.0f, (b.center.z < a.center.z) ? -1.0f : 1.0f);
    }

    contact.hit = true;
    contact.normal = normal;
    contact.penetration = penetration;
    // Contact point: centre of the overlapping AABB region, not the midpoint of the
    // two centres (which drifts off the touching surfaces for dissimilar sizes).
    contact.point = 0.5f * (glm::max(aMin, bMin) + glm::min(aMax, bMax));
    return contact;
}

Contact testSphereSphere(const WorldShape& a, const WorldShape& b) {
    Contact contact;
    const glm::vec3 diff = b.center - a.center;
    const float dist = glm::length(diff);
    const float radiusSum = a.radius + b.radius;
    if (dist >= radiusSum) {
        return contact;
    }

    contact.hit = true;
    contact.penetration = radiusSum - dist;
    contact.normal = dist > 1e-6f ? diff / dist : glm::vec3(0.0f, 1.0f, 0.0f);
    // Point on a's surface toward b, backed off half the penetration into the overlap.
    contact.point = a.center + contact.normal * (a.radius - contact.penetration * 0.5f);
    return contact;
}

// box vs sphere, normal pointing from the box toward the sphere.
Contact testBoxSphere(const WorldShape& box, const WorldShape& sphere) {
    Contact contact;
    const glm::vec3 boxMin = box.center - box.halfExtents;
    const glm::vec3 boxMax = box.center + box.halfExtents;
    const glm::vec3 closest = glm::clamp(sphere.center, boxMin, boxMax);
    const glm::vec3 diff = sphere.center - closest;
    const float dist = glm::length(diff);

    if (dist > 1e-6f) {
        if (dist >= sphere.radius) {
            return contact;
        }
        contact.hit = true;
        contact.normal = diff / dist;
        contact.penetration = sphere.radius - dist;
        contact.point = closest; // the point on the box surface nearest the sphere
        return contact;
    }

    // Sphere center inside the box: push out along the nearest face.
    const glm::vec3 toMax = boxMax - sphere.center;
    const glm::vec3 toMin = sphere.center - boxMin;
    float penetration = toMax.x;
    glm::vec3 normal(1.0f, 0.0f, 0.0f);
    auto consider = [&](float depth, const glm::vec3& axis) {
        if (depth < penetration) {
            penetration = depth;
            normal = axis;
        }
    };
    consider(toMin.x, glm::vec3(-1.0f, 0.0f, 0.0f));
    consider(toMax.y, glm::vec3(0.0f, 1.0f, 0.0f));
    consider(toMin.y, glm::vec3(0.0f, -1.0f, 0.0f));
    consider(toMax.z, glm::vec3(0.0f, 0.0f, 1.0f));
    consider(toMin.z, glm::vec3(0.0f, 0.0f, -1.0f));

    contact.hit = true;
    contact.normal = normal;
    contact.penetration = penetration + sphere.radius;
    contact.point = sphere.center; // sphere centre is inside the box
    return contact;
}

Contact narrowphase(const WorldShape& a, const WorldShape& b) {
    if (a.type == ColliderType::Box && b.type == ColliderType::Box) {
        return testBoxBox(a, b);
    }
    if (a.type == ColliderType::Sphere && b.type == ColliderType::Sphere) {
        return testSphereSphere(a, b);
    }
    if (a.type == ColliderType::Box && b.type == ColliderType::Sphere) {
        return testBoxSphere(a, b); // normal box(a) -> sphere(b) == a -> b
    }

    // a is sphere, b is box: compute box->sphere then flip to a->b.
    Contact contact = testBoxSphere(b, a);
    contact.normal = -contact.normal;
    return contact;
}

// These three only inspect bodies, so they take a const Registry: the ECS records
// access through the const path as a read, which is what lets the Physics system
// prove it doesn't mutate anything it declared read-only (Phase 13B).
float inverseMass(const Registry& registry, Entity entity) {
    if (!registry.rigidBodies.has(entity)) {
        return 0.0f; // collider with no body == immovable
    }
    const RigidBodyComponent& body = registry.rigidBodies.get(entity);
    if (body.isStatic || body.mass <= 0.0001f) {
        return 0.0f;
    }
    return 1.0f / body.mass;
}

float restitutionOf(const Registry& registry, Entity entity) {
    return registry.rigidBodies.has(entity) ? registry.rigidBodies.get(entity).restitution : 0.0f;
}

float frictionOf(const Registry& registry, Entity entity) {
    return registry.rigidBodies.has(entity) ? registry.rigidBodies.get(entity).friction : 0.0f;
}

// Resolves one contact and returns the normal impulse magnitude (0 if the bodies
// are immovable or already separating), which callers surface as the collision
// event's "impact strength".
float resolveContact(Registry& registry, const WorldShape& a, const WorldShape& b, const Contact& contact) {
    const float invA = inverseMass(registry, a.entity);
    const float invB = inverseMass(registry, b.entity);
    const float invSum = invA + invB;
    if (invSum <= 0.0f) {
        return 0.0f; // both immovable
    }

    // Positional correction so resting bodies don't sink (Baumgarte with slop).
    constexpr float correctionPercent = 0.8f;
    constexpr float slop = 0.01f;
    const glm::vec3 correction =
        (std::max(contact.penetration - slop, 0.0f) / invSum) * correctionPercent * contact.normal;
    if (invA > 0.0f) {
        registry.transforms.get(a.entity).transform.position -= invA * correction;
    }
    if (invB > 0.0f) {
        registry.transforms.get(b.entity).transform.position += invB * correction;
    }

    // Normal impulse with restitution.
    const glm::vec3 vA = registry.rigidBodies.has(a.entity) ? registry.rigidBodies.get(a.entity).velocity : glm::vec3(0.0f);
    const glm::vec3 vB = registry.rigidBodies.has(b.entity) ? registry.rigidBodies.get(b.entity).velocity : glm::vec3(0.0f);
    const glm::vec3 relativeVelocity = vB - vA;
    const float velAlongNormal = glm::dot(relativeVelocity, contact.normal);
    if (velAlongNormal > 0.0f) {
        return 0.0f; // already separating
    }

    const float e = std::min(restitutionOf(registry, a.entity), restitutionOf(registry, b.entity));
    const float j = -(1.0f + e) * velAlongNormal / invSum;
    glm::vec3 totalImpulse = j * contact.normal;

    // Coulomb friction: a tangential impulse opposing sliding, clamped by the
    // normal impulse (|jt| <= mu * j). mu is the combined friction coefficient.
    glm::vec3 tangent = relativeVelocity - velAlongNormal * contact.normal;
    const float tangentLength = glm::length(tangent);
    if (tangentLength > 1e-6f) {
        tangent /= tangentLength;
        const float jt = -glm::dot(relativeVelocity, tangent) / invSum;
        const float mu = std::sqrt(frictionOf(registry, a.entity) * frictionOf(registry, b.entity));
        const float clampedJt = std::clamp(jt, -mu * j, mu * j);
        totalImpulse += clampedJt * tangent;
    }

    if (invA > 0.0f) {
        registry.rigidBodies.get(a.entity).velocity = vA - invA * totalImpulse;
    }
    if (invB > 0.0f) {
        registry.rigidBodies.get(b.entity).velocity = vB + invB * totalImpulse;
    }

    return j; // normal impulse magnitude (>= 0 here)
}

// World-space AABB of a shape (boxes are axis-aligned; spheres use their radius).
void shapeAabb(const WorldShape& shape, glm::vec3& outMin, glm::vec3& outMax) {
    const glm::vec3 radius = shape.type == ColliderType::Box ? shape.halfExtents : glm::vec3(shape.radius);
    outMin = shape.center - radius;
    outMax = shape.center + radius;
}

// Uniform-grid (spatial-hash) broadphase (Phase 15). Replaces the old O(n^2)
// all-pairs scan: each shape is bucketed into the grid cells its AABB overlaps,
// and only shapes sharing a cell become candidate pairs. Cell size is derived from
// the largest shape so every shape spans ~1-2 cells per axis, keeping bucket
// occupancy low. The grid is rebuilt every step and owns no persistent state, so
// it stays deterministic and needs no serialization. Returned pairs are (a<b) and
// sorted, so contact resolution order is stable run-to-run (it was previously the
// unordered_map's iteration order).
std::vector<std::pair<size_t, size_t>> broadphasePairs(const std::vector<WorldShape>& shapes,
                                                     size_t& outCandidates) {
    // `outCandidates` counts AABB tests actually performed. That, not the returned
    // pair count, is the number a scale test has to watch: a degenerate grid returns
    // the right pairs and merely takes O(n^2) comparisons to find them
    // (DevDocs/DESIGN_BROADPHASE_SCALE.md).
    outCandidates = 0;
    std::vector<std::pair<size_t, size_t>> pairs;
    const size_t count = shapes.size();
    if (count < 2) {
        return pairs;
    }

    // Cell size comes from the MEDIAN shape, not the largest
    // (DevDocs/DESIGN_BROADPHASE_SCALE.md). Sizing it by the largest meant one arena wall
    // — 40 m long, among a thousand projectiles — made every cell 40 m across, so every
    // small shape landed in the same bucket and the grid became the all-pairs scan it
    // exists to replace: 1 524 shapes produced 23 221 candidate pairs at 0.84 FPS.
    // Every game has at least one collider far larger than its typical one.
    std::vector<float> extents;
    extents.reserve(count);
    for (const WorldShape& shape : shapes) {
        extents.push_back(shape.type == ColliderType::Box
            ? std::max({shape.halfExtents.x, shape.halfExtents.y, shape.halfExtents.z})
            : shape.radius);
    }
    const size_t middle = extents.size() / 2;
    std::nth_element(extents.begin(), extents.begin() + middle, extents.end());
    const float cellSize = std::max(extents[middle] * 2.0f, 1e-3f);
    const float invCell = 1.0f / cellSize;

    // A correctly sized cell moves the disparity problem rather than removing it: a 36 m
    // floor would now occupy ~1 300 buckets and a 1 km ground plane a million. So a shape
    // whose AABB spans more than this many cells is kept OUT of the grid and tested
    // against everything directly — the second tier. `k` is a handful of level-geometry
    // pieces, so `O(n + k*n)` replaces `O(n^2)`.
    constexpr int64_t MaxCellsPerShape = 32;

    // Pack a signed cell coordinate (21 bits/axis) into one key. Coordinates far
    // beyond +/-1e6 cells wrap rather than crash — fine for gameplay-scale scenes.
    auto cellKey = [](int x, int y, int z) -> int64_t {
        constexpr int64_t mask = 0x1FFFFF;
        return (static_cast<int64_t>(x) & mask)
             | ((static_cast<int64_t>(y) & mask) << 21)
             | ((static_cast<int64_t>(z) & mask) << 42);
    };
    // Clamp before the int cast so extreme or NaN positions can't overflow it
    // (that would be UB). Correctness is unaffected — the exact AABB test is the
    // arbiter; this only bounds the bucket index (distant shapes may alias, which
    // just costs a rejected candidate).
    auto cellCoord = [invCell](float v) -> int {
        const float c = std::floor(v * invCell);
        if (c >= 2.0e9f) {
            return 2000000000;
        }
        if (c <= -2.0e9f || !(c == c)) { // second test catches NaN
            return -2000000000;
        }
        return static_cast<int>(c);
    };

    const auto spannedCells = [&](const glm::vec3& lo, const glm::vec3& hi) -> int64_t {
        const int64_t nx = static_cast<int64_t>(cellCoord(hi.x)) - cellCoord(lo.x) + 1;
        const int64_t ny = static_cast<int64_t>(cellCoord(hi.y)) - cellCoord(lo.y) + 1;
        const int64_t nz = static_cast<int64_t>(cellCoord(hi.z)) - cellCoord(lo.z) + 1;
        // Saturate rather than overflow: a shape at the coordinate clamp spans a
        // meaningless number of cells, and "too many" is the only answer needed.
        if (nx > MaxCellsPerShape || ny > MaxCellsPerShape || nz > MaxCellsPerShape) {
            return MaxCellsPerShape + 1;
        }
        return nx * ny * nz;
    };

    std::unordered_map<int64_t, std::vector<int>> grid;
    std::vector<size_t> oversized; // the second tier: level geometry, a handful of it
    std::vector<bool> isOversized(count, false);
    grid.reserve(count * 2);
    for (size_t i = 0; i < count; ++i) {
        glm::vec3 lo, hi;
        shapeAabb(shapes[i], lo, hi);
        if (spannedCells(lo, hi) > MaxCellsPerShape) {
            oversized.push_back(i);
            isOversized[i] = true;
            continue;
        }
        for (int z = cellCoord(lo.z); z <= cellCoord(hi.z); ++z) {
            for (int y = cellCoord(lo.y); y <= cellCoord(hi.y); ++y) {
                for (int x = cellCoord(lo.x); x <= cellCoord(hi.x); ++x) {
                    grid[cellKey(x, y, z)].push_back(static_cast<int>(i));
                }
            }
        }
    }

    // For each shape, gather partners from the cells it covers, dedup each pair
    // (it may share several cells), and AABB-reject before it reaches narrowphase.
    std::unordered_set<int64_t> seen;
    for (size_t i = 0; i < count; ++i) {
        if (isOversized[i]) {
            // Tier two owns this shape entirely. Querying the grid for it as well would
            // both re-emit every pair the second tier finds *and* walk the thousand-plus
            // cells its AABB covers — the two costs this split exists to avoid.
            continue;
        }
        glm::vec3 lo, hi;
        shapeAabb(shapes[i], lo, hi);
        for (int z = cellCoord(lo.z); z <= cellCoord(hi.z); ++z) {
            for (int y = cellCoord(lo.y); y <= cellCoord(hi.y); ++y) {
                for (int x = cellCoord(lo.x); x <= cellCoord(hi.x); ++x) {
                    const auto bucket = grid.find(cellKey(x, y, z));
                    if (bucket == grid.end()) {
                        continue;
                    }
                    for (int otherInt : bucket->second) {
                        const size_t other = static_cast<size_t>(otherInt);
                        if (other <= i) {
                            continue; // only emit each unordered pair once, as a<b
                        }
                        const int64_t pairId = static_cast<int64_t>(i) * static_cast<int64_t>(count) + static_cast<int64_t>(other);
                        if (!seen.insert(pairId).second) {
                            continue;
                        }
                        glm::vec3 olo, ohi;
                        shapeAabb(shapes[other], olo, ohi);
                        outCandidates++;
                        const bool overlap = lo.x <= ohi.x && hi.x >= olo.x &&
                                             lo.y <= ohi.y && hi.y >= olo.y &&
                                             lo.z <= ohi.z && hi.z >= olo.z;
                        if (overlap) {
                            pairs.emplace_back(i, other);
                        }
                    }
                }
            }
        }
    }

    // Second tier: an oversized shape is in no bucket, so it is paired against every
    // other shape here. Each unordered pair is still emitted once — a grid shape is
    // never the `i` of one of these, and two oversized shapes are ordered by index.
    for (const size_t big : oversized) {
        glm::vec3 lo, hi;
        shapeAabb(shapes[big], lo, hi);
        for (size_t other = 0; other < count; ++other) {
            if (other == big) {
                continue;
            }
            if (isOversized[other] && other < big) {
                continue; // the lower-indexed one already emitted this pair
            }
            glm::vec3 olo, ohi;
            shapeAabb(shapes[other], olo, ohi);
            outCandidates++;
            const bool overlap = lo.x <= ohi.x && hi.x >= olo.x &&
                                 lo.y <= ohi.y && hi.y >= olo.y &&
                                 lo.z <= ohi.z && hi.z >= olo.z;
            if (overlap) {
                pairs.emplace_back(std::min(big, other), std::max(big, other));
            }
        }
    }

    // Sort so narrowphase + resolution run in a deterministic order.
    std::sort(pairs.begin(), pairs.end());
    if (std::getenv("SUGAR_PHYSDBG") != nullptr) {
        static int probe = 0;
        if ((probe++ % 60) == 0) {
            std::cerr << "[physdbg] shapes=" << count << " cell=" << cellSize
                      << " buckets=" << grid.size() << " pairs=" << pairs.size() << std::endl;
        }
    }
    return pairs;
}

} // namespace

void PhysicsWorld::step(Registry& registry, float deltaTime) {
    collisionEvents.clear();

    // 1) Integrate every dynamic body (semi-implicit Euler).
    for (auto& [entity, body] : registry.rigidBodies.getAll()) {
        if (body.isStatic || !registry.transforms.has(entity)) {
            body.accumulatedForce = glm::vec3(0.0f);
            continue;
        }

        const float safeMass = body.mass > 0.0001f ? body.mass : 0.0001f;
        glm::vec3 acceleration = body.accumulatedForce / safeMass;
        if (body.useGravity) {
            acceleration += gravity;
        }

        body.velocity += acceleration * deltaTime;
        registry.transforms.get(entity).transform.position += body.velocity * deltaTime;
        body.accumulatedForce = glm::vec3(0.0f);
    }

    // 1b) Nothing moved? Then nothing can have collided differently.
    //
    // Defect #49: a world of static colliders rebuilt the shape array and the uniform
    // grid, and narrowphased every candidate pair, on every fixed step -- recomputing an
    // unchanging answer sixty times a second. 186 static boxes with no rigid bodies at
    // all measured 2.975 ms/frame in the turn-based crawler.
    //
    // The check is a VALUE comparison of every collider and its pose against what the
    // last rebuild saw, not a dirty flag: gameplay may write a transform directly, and a
    // missed invalidation would be a collider that silently stops colliding. A dynamic
    // body under gravity moves every step and so invalidates this immediately, which is
    // the intended behaviour -- the cache only survives when literally nothing changed.
    const Registry& readOnly = registry;
    if (!colliderStateChanged(readOnly)) {
        collisionEvents = cachedEvents;
        return;
    }

    // 2) Broadphase: gather world shapes, then let the uniform grid produce only
    //    the candidate pairs that share a cell (was O(n^2) all-pairs). Read through
    //    a const view so colliders are recorded as a read, not a write — physics
    //    declares Collider read-only (Phase 13B).
    std::vector<WorldShape> shapes;
    shapes.reserve(readOnly.colliders.getAll().size());
    for (const auto& [entity, collider] : readOnly.colliders.getAll()) {
        if (!readOnly.transforms.has(entity)) {
            continue;
        }
        shapes.push_back(makeWorldShape(entity, readOnly.transforms.get(entity).transform, collider));
    }

    // 3) Narrowphase + resolution over the broadphase's candidate pairs (sorted, so
    //    resolution order is deterministic). Every hit becomes a CollisionEvent so
    //    gameplay (behaviors) can react; the contact point is approximated as the
    //    midpoint of the two centers (good enough for triggers/sfx; refine if needed).
    size_t candidates = 0;
    const std::vector<std::pair<size_t, size_t>> candidatePairs =
        broadphasePairs(shapes, candidates);
    broadphaseCandidateCount = candidates;
    for (const auto& [i, k] : candidatePairs) {
        // Collision filtering: layers that don't interact produce no contact and no
        // event at all — as if the other shape weren't there.
        if (!layersInteract(shapes[i], shapes[k])) {
            continue;
        }

        const Contact contact = narrowphase(shapes[i], shapes[k]);
        if (!contact.hit) {
            continue;
        }

        // A trigger (sensor) still reports the overlap but is never pushed apart, and
        // never pushes the other body. Only a pair of non-triggers resolves physically.
        const bool trigger = shapes[i].isTrigger || shapes[k].isTrigger;
        const float impulse = trigger ? 0.0f : resolveContact(registry, shapes[i], shapes[k], contact);

        CollisionEvent event;
        event.a = shapes[i].entity;
        event.b = shapes[k].entity;
        event.point = contact.point; // real per-shape contact point (was centre midpoint)
        event.normal = contact.normal;
        event.impulse = impulse;
        collisionEvents.push_back(event);
    }

    rememberColliderState(readOnly);
}

// True if any collider was added, removed, moved or edited since the last rebuild.
// Deliberately exact: bitwise-equal floats only, so a transform written by gameplay is
// caught even when the change is a fraction of a millimetre.
bool PhysicsWorld::colliderStateChanged(const Registry& registry) {
    if (!hasCache) {
        return true;
    }
    const auto& colliders = registry.colliders.getAll();
    if (colliders.size() != cachedBodies.size()) {
        return true; // one added or removed
    }
    for (const auto& [entity, collider] : colliders) {
        if (!registry.transforms.has(entity)) {
            return true;
        }
        const auto it = cachedBodies.find(entity);
        if (it == cachedBodies.end()) {
            return true; // same count, different membership
        }
        const CachedBody& was = it->second;
        const Transform& now = registry.transforms.get(entity).transform;
        if (now.position != was.transform.position || now.rotation != was.transform.rotation ||
            now.scale != was.transform.scale) {
            return true;
        }
        if (collider.type != was.collider.type || collider.halfExtents != was.collider.halfExtents ||
            collider.radius != was.collider.radius || collider.isTrigger != was.collider.isTrigger ||
            collider.layer != was.collider.layer || collider.mask != was.collider.mask) {
            return true;
        }
    }
    return false;
}

void PhysicsWorld::rememberColliderState(const Registry& registry) {
    cachedBodies.clear();
    for (const auto& [entity, collider] : registry.colliders.getAll()) {
        if (!registry.transforms.has(entity)) {
            continue;
        }
        cachedBodies[entity] = CachedBody{ registry.transforms.get(entity).transform, collider };
    }
    cachedEvents = collisionEvents;
    hasCache = true;
    ++broadphaseRebuilds;
}
