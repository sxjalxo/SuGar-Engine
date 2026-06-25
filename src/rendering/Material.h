#pragma once

#include "assets/AssetHandle.h"

struct Material {
    AssetHandle albedo = INVALID_HANDLE;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
};
