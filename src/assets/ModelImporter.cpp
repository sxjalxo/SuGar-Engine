#include "assets/ModelImporter.h"
#include "animation/AnimationClipRegistry.h"
#include "animation/AnimationComponents.h"
#include "animation/SkinRegistry.h"
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

// Skins are assets too, and register independently of animations: a skinned model
// with no clips is perfectly legal (posed by gameplay, or animated from another
// file), so this must not hang off the animation path.
void registerSkins(const GltfModelData& model, const std::string& path) {
    for (const Skin& skin : model.skins) {
        SkinRegistry::registerSkin(ModelImporter::skinKey(path, skin.name), skin);
    }
}

// Clips are assets, not state: they go into the name-keyed AnimationClipRegistry,
// and the component only ever holds the key string (docs/DESIGN_ANIMATION.md). So a
// re-import replaces the clip data underneath any entity already playing it, and
// nothing dangles.
void registerAnimations(const GltfModelData& model, const std::string& path,
                        Registry& registry, Entity root) {
    if (model.animations.empty() || root == INVALID_ENTITY) {
        return;
    }

    for (const AnimationClip& clip : model.animations) {
        AnimationClipRegistry::registerClip(ModelImporter::animationClipKey(path, clip.name), clip);
    }

    // Attach a player for the first clip, stopped. Registering the clips without
    // one would leave the animation invisible until hand-wired; starting it would
    // have the *importer* decide gameplay, and it would fight the editor (a model
    // that pirouettes the moment you drop it in is not an authoring tool). Stopped
    // is the honest middle: it's discoverable in the inspector, and Play is opt-in.
    AnimationPlayerComponent player;
    player.clip = ModelImporter::animationClipKey(path, model.animations.front().name);
    player.playing = false;
    registry.animations.add(root, player);
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

    registerSkins(model, path);

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

        // A skinned node names its bind data; the joints themselves are just other
        // entities in this same import, posed by the animation system.
        if (nodeData.skinIndex >= 0 && nodeData.skinIndex < static_cast<int>(model.skins.size())) {
            registry.skinnedMeshes.add(entity, {
                ModelImporter::skinKey(path, model.skins[static_cast<size_t>(nodeData.skinIndex)].name)
            });
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

    // Single root → use it directly; otherwise wrap under a container entity
    // named after the file so the whole import is one prefab-able subtree.
    Entity root = INVALID_ENTITY;
    if (model.roots.size() == 1 &&
        model.roots[0] >= 0 &&
        model.roots[0] < static_cast<int>(nodeEntities.size())) {
        root = nodeEntities[model.roots[0]];
    } else {
        root = registry.createEntity();
        const std::string stem = std::filesystem::path(path).stem().string();
        registry.names.add(root, { stem.empty() ? "Model" : stem });
        registry.transforms.add(root, { Transform{} });
        registry.hierarchy.add(root, {});
        for (int child : model.roots) {
            if (child >= 0 && child < static_cast<int>(nodeEntities.size())) {
                registry.setParent(nodeEntities[child], root);
            }
        }
    }

    registerAnimations(model, path, registry, root);
    return root;
}

std::string ModelImporter::animationClipKey(const std::string& modelPath, const std::string& clipName) {
    return modelPath + "#" + clipName;
}

std::string ModelImporter::skinKey(const std::string& modelPath, const std::string& skinName) {
    return modelPath + "#" + skinName;
}

void ModelImporter::ensureModelAssets(const std::string& assetKey) {
    if (assetKey.empty() ||
        AnimationClipRegistry::has(assetKey) ||
        SkinRegistry::has(assetKey)) {
        return; // already registered (or nothing to do)
    }

    const size_t separator = assetKey.find('#');
    if (separator == std::string::npos || separator == 0) {
        return; // not a model-backed key (a test's synthetic clip, say)
    }
    const std::string path = assetKey.substr(0, separator);

    GltfModelData model;
    try {
        GltfLoader::loadModel(path, model);
    } catch (...) {
        return; // missing/broken model: leave the component unresolved
    }

    registerSkins(model, path);
    for (const AnimationClip& clip : model.animations) {
        AnimationClipRegistry::registerClip(animationClipKey(path, clip.name), clip);
    }
}
