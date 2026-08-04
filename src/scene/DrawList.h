#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
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

    // Phase 17C.2 — the pose this item is drawn in, one matrix per joint in the
    // skin's joint order. Empty means "not skinned", which is the common case.
    //
    // Derived, and recomputed every frame from ECS transforms + bind data
    // (Skinning::computeJointMatrices). It rides on the draw list — a per-frame
    // description of what to draw — precisely so the renderer never has to reach
    // into the ECS for a pose, and never becomes the thing that owns one.
    std::vector<glm::mat4> jointMatrices;
};

struct DrawList {
    std::vector<RenderItem> items;
    std::vector<Light> lights;
};

// cameraPosition orders the translucent/additive tail back-to-front (painter's order);
// opaque/masked draws are depth-sorted by the GPU and only batched by material here.
void buildDrawListFromECS(const Registry& registry, const std::vector<Light>& lights,
                          const glm::vec3& cameraPosition, DrawList& out);
