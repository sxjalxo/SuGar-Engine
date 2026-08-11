#pragma once

#include <glm/glm.hpp>

#include "scene/Light.h"

// A light on an entity (M4 L3 — docs/DESIGN_LIGHTING.md). The seam that lets game code,
// which links only Core, run a day-night cycle and place torches: a game drives the sun by
// rotating an entity and lights a cave by creating one.
//
// **No position, no direction.** Both are derived from the entity's world transform every
// frame — position = translation, direction = rotation * -Z — exactly as CameraComponent
// derives the eye pose. Authoring a pose here would create a second owner that a snapshot
// restore could desync from the transform.
struct LightComponent {
    LightType type = LightType::Point;

    glm::vec3 color = {1.0f, 0.95f, 0.85f};

    // Multiplied into the colour before upload. Separate from the colour because a game
    // fades a light (dawn, a dying torch) without wanting to touch its hue.
    float intensity = 1.0f;

    // Point only: the distance at which the light contributes nothing. Range-limited
    // rather than physical falloff so a torch's reach is what the author said it is.
    float range = 20.0f;

    // Honoured for the one shadow caster the shadow pass supports; a directional caster
    // is preferred, which is what makes a sun cast the scene's shadows.
    bool castsShadow = false;

    // A light switched off stays on its entity (with its colour and range intact) instead
    // of being deleted and rebuilt — the same reason CameraComponent has `active`.
    bool active = true;
};
