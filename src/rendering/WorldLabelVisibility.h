#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "ecs/Entity.h"
#include "physics/PhysicsQuery.h"

class Registry;

// Should a world-space label be hidden because something solid stands between the
// camera and the point it is anchored to?
//
// Deferred since the world labels landed (M4 L3: "depth occlusion for world labels —
// they show through walls"), and the reason it stayed deferred is that "solid" is a
// GAME's word, not the engine's: an arena wall should hide a nameplate, another enemy
// probably should not. So the engine does not decide — `WorldLabelComponent::occluderMask`
// names the collision layers that count, and a mask of 0 means "never occlude", which is
// exactly the behaviour every existing scene already has.
//
// Core and pure: it is a raycast against colliders the ECS already holds, so it is
// testable with no device, unlike the projection-and-layout half that lives in the view.
namespace WorldLabelVisibility {

// True when `anchor` is hidden from `eye`. `owner` is the labelled entity, whose own
// collider never counts — a nameplate floating above a body would otherwise be occluded
// by the body it belongs to.
inline bool occluded(const Registry& registry,
                     const glm::vec3& eye,
                     const glm::vec3& anchor,
                     Entity owner,
                     uint32_t occluderMask) {
    if (occluderMask == 0u) {
        return false; // opted out (the default): identical to the pre-occlusion behaviour
    }

    const glm::vec3 toAnchor = anchor - eye;
    const float distance = glm::length(toAnchor);
    if (!(distance > 1e-4f)) {
        return false; // the camera is on top of the anchor; nothing can be between them
    }

    // Stop just short of the anchor so a collider *at* the anchor (a wall the label is
    // pinned to) does not occlude the label it carries.
    constexpr float kBackOff = 0.05f;
    RaycastHit hit;
    if (!PhysicsQuery::raycast(registry, eye, toAnchor / distance, distance - kBackOff, hit,
                               occluderMask)) {
        return false;
    }
    return hit.entity != owner;
}

} // namespace WorldLabelVisibility
