#pragma once

#include <string>
#include <vector>
#include "ecs/Entity.h"

class Registry;
struct Light;

class SceneSerializer {
public:
    static bool save(const Registry& registry, const std::vector<Light>& lights, const std::string& path);
    static bool load(Registry& registry, std::vector<Light>& lights, const std::string& path);

    // In-memory variants used for Play-mode snapshot/restore. saveToString returns
    // the serialized scene (empty string on failure); loadFromString rebuilds the
    // registry/lights from such a string.
    static std::string saveToString(const Registry& registry, const std::vector<Light>& lights);
    static bool loadFromString(Registry& registry, std::vector<Light>& lights, const std::string& text);

    // Prefab = a reusable entity template. savePrefab serializes the subtree
    // rooted at `root` (root + descendants) to a .prefab file (same object schema
    // as a scene, no lights). instantiatePrefab spawns that subtree into the
    // registry additively (no reset) and returns the new root entity, or
    // INVALID_ENTITY on failure.
    static bool savePrefab(const Registry& registry, Entity root, const std::string& path);
    static Entity instantiatePrefab(Registry& registry, const std::string& path);

    // String variants of the prefab subtree round-trip (same schema, no file I/O).
    // Used by the editor's duplicate/undo so a copied subtree goes through the
    // exact same component + resource-refcount path as prefabs. Empty string /
    // INVALID_ENTITY on failure. `outCreated`, if given, receives every created
    // entity in object order (root first) — the editor uses this to build an
    // old->new id remap when a subtree is recreated.
    static std::string savePrefabToString(const Registry& registry, Entity root);
    static Entity instantiateFromString(Registry& registry, const std::string& text,
                                        std::vector<Entity>* outCreated = nullptr);

    // Gathers `root` + its descendants in the same order serialization uses
    // (root first). Lets the editor record the pre-delete id order for remapping.
    static void collectSubtreeEntities(const Registry& registry, Entity root, std::vector<Entity>& out);
};
