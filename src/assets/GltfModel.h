#pragma once

#include <string>
#include <vector>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

// Engine-owned description of a glTF scene graph. Contains NO tinygltf types —
// the loader fills this and the rest of the engine consumes it. Geometry is not
// stored here; it is loaded on demand through ResourceManager using the
// "<path>#<meshIndex>" mesh resource key. This struct carries only structure:
// the node hierarchy, each node's transform + mesh reference, and PBR material
// factors / base-color texture path.

struct GltfNodeData {
    std::string name;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // identity (w, x, y, z)
    glm::vec3 scale{1.0f};
    int meshIndex = -1;            // glTF mesh index, or -1 for a pure group node
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
};
