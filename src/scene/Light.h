#pragma once

#include <string>

#include <glm/glm.hpp>

// How a light reaches a surface. See docs/DESIGN_LIGHTING.md.
//   Directional - sun/moon: a direction, no position, no falloff.
//   Point       - torch/lamp: a position and a range it fades out over.
//   Ambient     - the sky term: no position, no direction; lifts everything equally.
enum class LightType {
    Directional = 0,
    Point = 1,
    Ambient = 2,
};

// One light as the renderer consumes it. This is the *render list* form: scene-level
// lights (the `lights` array in a scene file) and LightComponent entities both reduce to
// it, so the renderer has exactly one thing to upload.
//
// `direction` points the way the light travels (a sun at noon points straight down); the
// shader is given the negated, normalized form. For a Point light it is unused, and for a
// component light neither position nor direction is authored — both are derived from the
// entity's world transform every frame (Rule 21b).
struct Light {
    glm::vec3 position = {2.0f, 2.0f, 2.0f};
    glm::vec3 color = {1.0f, 0.95f, 0.85f};
    glm::vec3 direction = {0.0f, -1.0f, 0.0f};
    LightType type = LightType::Point;
    float intensity = 1.0f;
    // Point only: the distance at which it contributes nothing. **Zero means unlimited**,
    // which is what every light authored before the lighting seam expected — the shader
    // had no falloff at all, so a scene-level light must keep reaching the whole scene.
    float range = 0.0f;
    bool castsShadow = false; // honoured for the single shadow caster the pass supports
};

// Serialized as a string, like ColliderType and NavAgentStatus: an integer would make the
// scene file depend on enumerator order, so inserting a type would silently reinterpret
// every saved light.
inline const char* lightTypeName(LightType type) {
    switch (type) {
        case LightType::Directional: return "directional";
        case LightType::Ambient:     return "ambient";
        case LightType::Point:       break;
    }
    return "point";
}

inline LightType lightTypeFromName(const std::string& name) {
    if (name == "directional") return LightType::Directional;
    if (name == "ambient") return LightType::Ambient;
    return LightType::Point;
}
