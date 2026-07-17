#pragma once

#include <string>
#include "ecs/Entity.h"

class Registry;

// Imports a glTF/.glb file as an ECS subtree: one entity per glTF node, with its
// local transform, a mesh reference ("<path>#<meshIndex>") and a material built
// from the glTF PBR factors / base-color texture. Any animation clips are
// registered in AnimationClipRegistry (Phase 17B). Returns the root entity, or
// INVALID_ENTITY on failure. Engine-side only — no tinygltf types involved.
class ModelImporter {
public:
    static Entity importGltf(Registry& registry, const std::string& path);

    // The AnimationClipRegistry key for a clip in a model: "<path>#<clipName>",
    // mirroring the "<path>#<meshIndex>" mesh key. Name rather than index so a
    // re-export that reorders animations doesn't silently repoint a saved scene at
    // a different clip — the name is the stable identity. Duplicate clip names
    // within one file collide (last registered wins); glTF permits that, and the
    // fix if it ever bites is a unique name at export, not an index here.
    static std::string animationClipKey(const std::string& modelPath, const std::string& clipName);

    // The SkinRegistry key for a skin in a model: "<path>#<skinName>". Same scheme,
    // same reasoning as animationClipKey.
    static std::string skinKey(const std::string& modelPath, const std::string& skinName);

    // Ensures the clips/skins of the model named by `assetKey` ("<path>#<name>") are
    // in their registries, parsing the model once if not.
    //
    // Import registers assets as a side effect, but a scene *loaded from disk* never
    // ran the importer: its components name clips and skins that nothing put in the
    // tables, so animation would silently do nothing and skinned meshes would render
    // in bind pose. Components hold names precisely so they can be re-resolved like
    // this — the same reason MeshComponent holds a resource key.
    //
    // Cheap to call repeatedly: it returns immediately once the key is present, so
    // the per-scrub snapshot-patch path does no file I/O.
    static void ensureModelAssets(const std::string& assetKey);
};
