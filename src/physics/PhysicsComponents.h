#pragma once

#include <cstdint>
#include <glm/vec3.hpp>

// Rigid body for the hand-rolled physics step. Dynamic by default; set isStatic
// for immovable colliders (ground, walls). All state lives in the component so it
// serializes and survives snapshot/restore. `accumulatedForce` is transient
// (cleared every step) and intentionally not serialized.
struct RigidBodyComponent {
    glm::vec3 velocity{0.0f, 0.0f, 0.0f};
    glm::vec3 accumulatedForce{0.0f, 0.0f, 0.0f};
    float mass = 1.0f;
    float restitution = 0.3f; // bounciness [0,1]
    float friction = 0.5f;    // tangential friction coefficient [0,1]
    bool useGravity = true;
    bool isStatic = false;
};

enum class ColliderType {
    Box,
    Sphere
};

// Collider in the entity's local space, scaled by the transform's scale. Box uses
// halfExtents; Sphere uses radius. v1 ignores rotation for collision (boxes stay
// axis-aligned), so keep physics bodies top-level for correct world-space sim.
struct ColliderComponent {
    ColliderType type = ColliderType::Box;
    glm::vec3 halfExtents{0.5f, 0.5f, 0.5f};
    float radius = 0.5f;

    // Overlap-only ("sensor"): a trigger still emits a CollisionEvent when it
    // overlaps, but the physics step applies no impulse/positional correction to
    // either body. Pickups, damage volumes, checkpoints. Default off = solid.
    bool isTrigger = false;

    // Collision filtering (bitmasks). Two colliders interact only when each one's
    // `mask` includes the other's `layer` bit — i.e. (a.mask & b.layer) and
    // (b.mask & a.layer) are both non-zero. Default all-ones = collide with
    // everything (fully back-compatible with scenes that set neither field).
    // Lets bullets ignore bullets, players ignore each other, etc.
    uint32_t layer = 0xFFFFFFFFu;
    uint32_t mask = 0xFFFFFFFFu;
};
