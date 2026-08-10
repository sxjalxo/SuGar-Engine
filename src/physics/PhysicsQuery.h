#pragma once

#include <cstdint>
#include <glm/vec3.hpp>
#include "ecs/Entity.h"

class Registry;

// Result of a successful raycast. `distance` is along the (normalized) ray
// direction from `origin`; `point` is origin + dir * distance; `normal` faces back
// toward the ray on the surface that was hit.
struct RaycastHit {
    Entity entity = INVALID_ENTITY;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    float distance = 0.0f;
};

// Read-only spatial queries against the scene's colliders. Lives in Core (not the
// exe-side PhysicsWorld solver) precisely so gameplay behaviors — which link only
// Core and hold a Registry — can call it: hitscan weapons, ground/ledge checks,
// line-of-sight, world picking. Uses the same shape derivation the solver does
// (axis-aligned boxes, uniform-scaled spheres, transform.position centers).
namespace PhysicsQuery {

// Nearest collider the ray hits within [0, maxDistance]. `direction` need not be
// unit length (it is normalized internally). `mask` filters by collider layer,
// mirroring PhysicsWorld: a collider is tested only when (mask & collider.layer)
// != 0. Triggers ARE hit (a query is not a physical contact). Returns false (and
// leaves outHit untouched) when nothing is hit.
bool raycast(const Registry& registry,
             const glm::vec3& origin,
             const glm::vec3& direction,
             float maxDistance,
             RaycastHit& outHit,
             uint32_t mask = 0xFFFFFFFFu);

} // namespace PhysicsQuery
