#pragma once

#include <string>
#include "ecs/Entity.h"

class Registry;

// Imports a glTF/.glb file as an ECS subtree: one entity per glTF node, with its
// local transform, a mesh reference ("<path>#<meshIndex>") and a material built
// from the glTF PBR factors / base-color texture. Returns the root entity, or
// INVALID_ENTITY on failure. Engine-side only — no tinygltf types involved.
class ModelImporter {
public:
    static Entity importGltf(Registry& registry, const std::string& path);
};
