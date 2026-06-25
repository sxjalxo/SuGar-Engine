#pragma once

#include <glm/mat4x4.hpp>
#include <memory>
#include <vector>
#include "assets/AssetHandle.h"
#include "rendering/Material.h"
#include "scene/Light.h"

class Registry;
class Mesh;

struct RenderItem {
    std::shared_ptr<Mesh> mesh;
    AssetHandle meshHandle = INVALID_HANDLE;
    Material material;
    glm::mat4 model{1.0f};
};

struct DrawList {
    std::vector<RenderItem> items;
    std::vector<Light> lights;
};

void buildDrawListFromECS(const Registry& registry, const std::vector<Light>& lights, DrawList& out);
