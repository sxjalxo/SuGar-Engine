#pragma once
#include <glm/glm.hpp>

constexpr int MAX_LIGHTS = 4;

struct UniformBufferObject
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 lightSpaceMatrix;
    alignas(16) glm::vec4 viewPos;
    alignas(16) glm::vec4 lightPositions[MAX_LIGHTS];
    alignas(16) glm::vec4 lightColors[MAX_LIGHTS];
    alignas(4) int lightCount = 0;
    alignas(4) float padding[3] = {0.0f, 0.0f, 0.0f};
};
