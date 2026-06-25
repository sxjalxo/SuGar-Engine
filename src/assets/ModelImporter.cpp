#include "assets/ModelImporter.h"
#include "assets/GltfLoader.h"
#include "assets/GltfModel.h"
#include "assets/ResourceManager.h"
#include "ecs/Registry.h"
#include "rendering/Material.h"
#include "scene/Transform.h"

#include <filesystem>
#include <vector>

namespace {

Material buildMaterial(const GltfModelData& model, int meshIndex) {
    Material material{};
    material.metallic = 0.0f;
    material.roughness = 0.5f;
    material.ao = 1.0f;

    const int materialIndex = meshIndex < static_cast<int>(model.meshMaterialIndex.size())
        ? model.meshMaterialIndex[meshIndex]
        : -1;

    const GltfMaterialData* data = (materialIndex >= 0 && materialIndex < static_cast<int>(model.materials.size()))
        ? &model.materials[materialIndex]
        : nullptr;

    if (data != nullptr) {
        material.metallic = data->metallic;
        material.roughness = data->roughness;
    }

    try {
        if (data != nullptr && !data->baseColorTexture.empty()) {
            material.albedo = ResourceManager::loadTexture(data->baseColorTexture);
        } else {
            material.albedo = ResourceManager::loadTexture(ResourceManager::CheckerboardTextureId);
        }
    } catch (...) {
        material.albedo = ResourceManager::loadTexture(ResourceManager::CheckerboardTextureId);
    }

    return material;
}

} // namespace

Entity ModelImporter::importGltf(Registry& registry, const std::string& path) {
    GltfModelData model;
    try {
        GltfLoader::loadModel(path, model);
    } catch (...) {
        return INVALID_ENTITY;
    }

    if (model.nodes.empty()) {
        return INVALID_ENTITY;
    }

    // One entity per glTF node.
    std::vector<Entity> nodeEntities(model.nodes.size(), INVALID_ENTITY);
    for (size_t i = 0; i < model.nodes.size(); i++) {
        const GltfNodeData& nodeData = model.nodes[i];
        const Entity entity = registry.createEntity();
        nodeEntities[i] = entity;

        registry.names.add(entity, { nodeData.name });

        Transform transform;
        transform.position = nodeData.position;
        transform.rotation = nodeData.rotation;
        transform.scale = nodeData.scale;
        registry.transforms.add(entity, { transform });
        registry.hierarchy.add(entity, {});

        if (nodeData.meshIndex >= 0) {
            const std::string meshKey = path + "#" + std::to_string(nodeData.meshIndex);
            try {
                registry.meshes.add(entity, { ResourceManager::loadMesh(meshKey) });
            } catch (...) {
                // Leave the node mesh-less if the sub-mesh failed to load.
            }
            registry.materials.add(entity, { buildMaterial(model, nodeData.meshIndex) });
        }
    }

    // Parent-child wiring.
    for (size_t i = 0; i < model.nodes.size(); i++) {
        for (int child : model.nodes[i].children) {
            if (child >= 0 && child < static_cast<int>(nodeEntities.size())) {
                registry.setParent(nodeEntities[child], nodeEntities[i]);
            }
        }
    }

    // Single root → return it directly; otherwise wrap under a container entity
    // named after the file so the whole import is one prefab-able subtree.
    if (model.roots.size() == 1 &&
        model.roots[0] >= 0 &&
        model.roots[0] < static_cast<int>(nodeEntities.size())) {
        return nodeEntities[model.roots[0]];
    }

    const Entity container = registry.createEntity();
    const std::string stem = std::filesystem::path(path).stem().string();
    registry.names.add(container, { stem.empty() ? "Model" : stem });
    registry.transforms.add(container, { Transform{} });
    registry.hierarchy.add(container, {});
    for (int root : model.roots) {
        if (root >= 0 && root < static_cast<int>(nodeEntities.size())) {
            registry.setParent(nodeEntities[root], container);
        }
    }
    return container;
}
