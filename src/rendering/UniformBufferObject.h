#pragma once
#include <glm/glm.hpp>

// Eight, not four: a torch-lit scene needs more than four and the game hit the old cap
// immediately (DevDocs/DESIGN_LIGHTING.md). Not "many" — the renderer is forward and
// single-pass, so every light costs every fragment; clustered/deferred lighting is a
// renderer rewrite and nothing has forced it.
constexpr int MAX_LIGHTS = 8;

struct UniformBufferObject
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 lightSpaceMatrix;
    alignas(16) glm::vec4 viewPos;
    // xyz = world position (point) or direction TOWARD the light (directional).
    // w    = 0 marks a directional light; otherwise it is the point light's range.
    alignas(16) glm::vec4 lightPositions[MAX_LIGHTS];
    // rgb = colour premultiplied by intensity, so the shader never sees an "intensity".
    alignas(16) glm::vec4 lightColors[MAX_LIGHTS];
    // rgb = ambient term (the sky light), replacing a hardcoded constant in the shader.
    alignas(16) glm::vec4 ambient = glm::vec4(0.12f, 0.12f, 0.12f, 1.0f);
    alignas(4) int lightCount = 0;
    alignas(4) float padding[3] = {0.0f, 0.0f, 0.0f};
};
