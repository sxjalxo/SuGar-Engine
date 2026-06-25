#pragma once

#include <glm/vec3.hpp>

#include "ecs/Entity.h"

// One contact produced by PhysicsWorld::step during narrowphase resolution.
// PhysicsWorld accumulates these per step; SuGarApp dispatches them to the
// behaviors on the involved entities (Behavior::onCollision). This is the single
// event primitive that unlocks landing/footstep sounds, destruction, particle
// spawning, and gameplay triggers — audio is just the first consumer.
struct CollisionEvent {
    Entity a = INVALID_ENTITY;
    Entity b = INVALID_ENTITY;
    glm::vec3 point{0.0f};   // approximate contact point (midpoint of the pair)
    glm::vec3 normal{0.0f};  // unit normal pointing from a toward b
    float impulse = 0.0f;    // normal impulse magnitude (0 for resting/separating)
};
