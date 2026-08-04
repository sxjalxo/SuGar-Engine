#pragma once

#include <glm/glm.hpp>

#include "assets/AssetHandle.h"

struct Material {
    AssetHandle albedo = INVALID_HANDLE;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    // Flat colour tint multiplied into the sampled albedo. Default white = the
    // texture shows unchanged; pair with builtin://white for a solid flat colour.
    glm::vec3 baseColor = glm::vec3(1.0f);
};
