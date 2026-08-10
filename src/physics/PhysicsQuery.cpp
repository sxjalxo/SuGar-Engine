#include "physics/PhysicsQuery.h"

#include <cmath>
#include <limits>
#include <glm/geometric.hpp>

#include "ecs/Registry.h"

namespace {

// Ray vs world-space AABB (slab method). On hit, writes the entry distance and the
// face normal. A ray that starts inside the box hits at distance 0 with a normal
// facing back along the ray. `dir` is assumed normalized.
bool rayAabb(const glm::vec3& origin, const glm::vec3& dir,
             const glm::vec3& boxMin, const glm::vec3& boxMax,
             float maxDistance, float& outDistance, glm::vec3& outNormal) {
    float tEntry = 0.0f;
    float tExit = maxDistance;
    int entryAxis = -1;
    float entrySign = 0.0f;

    for (int axis = 0; axis < 3; ++axis) {
        const float o = origin[axis];
        const float d = dir[axis];
        const float lo = boxMin[axis];
        const float hi = boxMax[axis];

        if (std::abs(d) < 1e-8f) {
            // Parallel to this slab: miss if the origin is outside it.
            if (o < lo || o > hi) {
                return false;
            }
            continue;
        }

        float t1 = (lo - o) / d;
        float t2 = (hi - o) / d;
        float sign = -1.0f; // hitting the `lo` face means the normal points -axis
        if (t1 > t2) {
            std::swap(t1, t2);
            sign = 1.0f;
        }
        if (t1 > tEntry) {
            tEntry = t1;
            entryAxis = axis;
            entrySign = sign;
        }
        if (t2 < tExit) {
            tExit = t2;
        }
        if (tEntry > tExit) {
            return false;
        }
    }

    if (tEntry > maxDistance) {
        return false;
    }

    outDistance = tEntry;
    outNormal = glm::vec3(0.0f);
    if (entryAxis >= 0) {
        outNormal[entryAxis] = entrySign;
    } else {
        // Started inside the box: face back toward the ray.
        outNormal = -dir;
    }
    return true;
}

// Ray vs sphere. Returns the nearest non-negative intersection distance. `dir`
// assumed normalized.
bool raySphere(const glm::vec3& origin, const glm::vec3& dir,
               const glm::vec3& center, float radius,
               float maxDistance, float& outDistance, glm::vec3& outNormal) {
    const glm::vec3 oc = origin - center;
    const float b = glm::dot(oc, dir);
    const float c = glm::dot(oc, oc) - radius * radius;
    const float disc = b * b - c;
    if (disc < 0.0f) {
        return false;
    }
    const float sqrtDisc = std::sqrt(disc);
    float t = -b - sqrtDisc;
    if (t < 0.0f) {
        t = -b + sqrtDisc; // origin inside the sphere: take the exit
    }
    if (t < 0.0f || t > maxDistance) {
        return false;
    }
    outDistance = t;
    const glm::vec3 hit = origin + dir * t;
    const glm::vec3 n = hit - center;
    const float len = glm::length(n);
    outNormal = len > 1e-6f ? n / len : -dir;
    return true;
}

} // namespace

namespace PhysicsQuery {

bool raycast(const Registry& registry,
             const glm::vec3& origin,
             const glm::vec3& direction,
             float maxDistance,
             RaycastHit& outHit,
             uint32_t mask) {
    const float dirLen = glm::length(direction);
    if (dirLen < 1e-8f || maxDistance <= 0.0f) {
        return false;
    }
    const glm::vec3 dir = direction / dirLen;

    float bestDistance = std::numeric_limits<float>::max();
    RaycastHit best;
    bool hitAny = false;

    for (const auto& [entity, collider] : registry.colliders.getAll()) {
        if ((mask & collider.layer) == 0u) {
            continue; // filtered out by layer
        }
        if (!registry.transforms.has(entity)) {
            continue;
        }
        const Transform& transform = registry.transforms.get(entity).transform;
        const glm::vec3 center = transform.position;
        const glm::vec3 absScale = glm::abs(transform.scale);

        float distance = 0.0f;
        glm::vec3 normal(0.0f);
        bool hit = false;

        if (collider.type == ColliderType::Sphere) {
            const float maxScale = std::max(absScale.x, std::max(absScale.y, absScale.z));
            hit = raySphere(origin, dir, center, collider.radius * maxScale,
                            maxDistance, distance, normal);
        } else {
            const glm::vec3 halfExtents = collider.halfExtents * absScale;
            hit = rayAabb(origin, dir, center - halfExtents, center + halfExtents,
                          maxDistance, distance, normal);
        }

        if (hit && distance < bestDistance) {
            bestDistance = distance;
            best.entity = entity;
            best.distance = distance;
            best.point = origin + dir * distance;
            best.normal = normal;
            hitAny = true;
        }
    }

    if (hitAny) {
        outHit = best;
    }
    return hitAny;
}

} // namespace PhysicsQuery
