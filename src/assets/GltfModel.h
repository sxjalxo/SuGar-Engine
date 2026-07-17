#pragma once

#include <string>
#include <vector>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#include "animation/AnimationClip.h"
#include "animation/Skin.h"

// Engine-owned description of a glTF scene graph. Contains NO tinygltf types —
// the loader fills this and the rest of the engine consumes it. Geometry is not
// stored here; it is loaded on demand through ResourceManager using the
// "<path>#<meshIndex>" mesh resource key. This struct carries only structure:
// the node hierarchy, each node's transform + mesh reference, PBR material
// factors / base-color texture path, and any animation clips.

struct GltfNodeData {
    std::string name;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // identity (w, x, y, z)
    glm::vec3 scale{1.0f};
    int meshIndex = -1;            // glTF mesh index, or -1 for a pure group node
    int skinIndex = -1;            // index into GltfModelData::skins, or -1
    std::vector<int> children;     // indices into GltfModelData::nodes
};

struct GltfMaterialData {
    float metallic = 1.0f;
    float roughness = 1.0f;
    std::string baseColorTexture;  // project-relative path, empty if none/embedded
};

struct GltfModelData {
    std::vector<GltfNodeData> nodes;
    std::vector<int> roots;              // root node indices (scene roots)
    std::vector<GltfMaterialData> materials;
    std::vector<int> meshMaterialIndex;  // per glTF mesh -> material index, or -1

    // Animation clips, already in engine form (Phase 17B). glTF channels target
    // node *indices*; the loader resolves those to node **names** here, so nothing
    // downstream depends on glTF's numbering — a re-export that reorders nodes
    // doesn't invalidate a saved scene. See docs/DESIGN_ANIMATION.md.
    std::vector<AnimationClip> animations;

    // Skins, already in engine form (Phase 17C). glTF lists a skin's joints as node
    // *indices*; those are resolved to node **names** here for the same reason
    // animation targets are — joint order is preserved (JOINTS_0 indexes into it),
    // but glTF's node numbering does not escape the loader.
    std::vector<Skin> skins;
};
