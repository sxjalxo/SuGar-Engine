#pragma once

#include <string>
#include <vector>
#include "rendering/Material.h"
#include "assets/AssetHandle.h"
#include "ecs/Entity.h"
#include "scene/Transform.h"

struct NameComponent {
    std::string name;
};

struct TransformComponent {
    Transform transform;
};

struct MeshComponent {
    AssetHandle mesh = INVALID_HANDLE;
};

struct MaterialComponent {
    Material material;
};

struct HierarchyComponent {
    Entity parent = INVALID_ENTITY;
    std::vector<Entity> children;
};

// Attaches named gameplay logic to an entity. The behavior itself is looked up
// in BehaviorRegistry by name; `started` is per-entity lifecycle state and lives
// here (in the component) so it serializes and survives snapshot/restore.
struct ScriptComponent {
    std::string behavior;
    bool started = false;
};

// Marks an entity as an instance of a prefab. Stores the source .prefab path so
// the editor can re-sync ("Revert to Prefab"). Edits to the instance are
// implicit overrides; reverting respawns the entity from the prefab.
struct PrefabInstanceComponent {
    std::string prefab;
};
