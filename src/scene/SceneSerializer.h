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

    // Restores a snapshot by *patching component data into the existing entities*
    // instead of destroying and recreating them (Phase 14A). Entities are matched
    // by serialization order (sorted entity id), so entity ids are preserved and
    // editor selection / undo history survive a restore. Resource-backed components
    // (mesh, material texture, audio clip) are only reloaded when their key actually
    // changed, so a frame-to-frame scrub doesn't churn ResourceManager ref counts.
    //
    // Only valid when the snapshot's structure matches the live registry (same
    // entity count — true within a Play session, where ids don't change frame to
    // frame). On a structural mismatch it returns false WITHOUT mutating the
    // registry, so the caller can fall back to loadFromString (full rebuild).
    static bool patchFromString(Registry& registry, std::vector<Light>& lights, const std::string& text);

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
    // entity in object order (root first) — the editor records these as the ids to
    // recreate the subtree into on a later undo/redo (see instantiateFromStringWithIds).
    static std::string savePrefabToString(const Registry& registry, Entity root);
    static Entity instantiateFromString(Registry& registry, const std::string& text,
                                        std::vector<Entity>* outCreated = nullptr);

    // Like instantiateFromString, but recreates the subtree with the caller-supplied
    // entity ids (one per serialized object, same DFS order) instead of fresh ones
    // (Phase 14B). Lets delete-undo / duplicate-redo restore a subtree into its
    // *original* ids, so editor-command references survive without a remap layer.
    // Returns the root, or INVALID_ENTITY if `ids` doesn't match the object count.
    static Entity instantiateFromStringWithIds(Registry& registry, const std::string& text,
                                               const std::vector<Entity>& ids);

    // Gathers `root` + its descendants in the same order serialization uses
    // (root first). Lets the editor record the pre-delete id order for remapping.
    static void collectSubtreeEntities(const Registry& registry, Entity root, std::vector<Entity>& out);
};
