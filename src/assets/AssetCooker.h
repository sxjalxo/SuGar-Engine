#pragma once

#include <cstdint>
#include <string>
#include <vector>

class AssetDatabase;

// Source formats -> cooked artifacts (docs/DESIGN_ASSET_PIPELINE.md, Phase 19B).
//
//     Cooked  = f(source bytes, import settings, cooker version)
//     Runtime = f(cooked)
//
// Cooking is the ONLY place glTF, OBJ, PNG or WAV is parsed. ResourceManager asks for a
// cooked path and reads that; it no longer knows what a source format is.
//
// Headless but NOT Core: cooking needs tinygltf / stb_image / miniaudio, which Rule 15
// keeps out of Core, and it produces Mesh/Texture/AudioClip. What matters is that it
// needs no Vulkan device -- the cook tool, CI and the self-tests all run without one.
//
// The cache is derived (Rule 21b): every artifact is recomputable from the present
// source bytes, so it is never serialized into a scene or a snapshot, and deleting the
// whole directory is always safe.
class AssetCooker {
public:
    // Where artifacts live. Default "build/assetcache". Under build/ because it is
    // build output: already git-ignored, already deleted by a clean.
    static void setCacheDirectory(const std::string& directory);
    static const std::string& cacheDirectory();

    // Optional. When set, cook keys come from the catalog (which already knows each
    // asset's content hash and .meta). Without it the cooker still works -- it hashes
    // the file itself -- so a tool or a test can cook a tree it never scanned.
    static void setDatabase(AssetDatabase* database);

    // The artifact identity for a resource key, sub-selector included:
    //   H(entry cook key, sub)
    // so "hero.gltf#3" and "hero.gltf#7" are separate artifacts of one source file that
    // invalidate together when the file changes. Returns 0 if the source is unreadable.
    static uint64_t artifactKey(const std::string& resourceKey);

    // The cooked path for an artifact key: "<cache>/<16-hex>.sgc".
    static std::string artifactPath(uint64_t key);

    // Cooks `resourceKey` if it is missing or stale, and returns the cooked path.
    // Returns "" (with `errorMessage` set) if the source cannot be read or cooked.
    //
    // Cook-on-demand rather than cook-then-run: the editor must stay usable the instant
    // a file is dropped in, and a developer should never have to know a cook step
    // exists (Rule 1). Packaging (Phase 20) walks the catalog and calls cookAll first.
    static std::string ensureCooked(const std::string& resourceKey, std::string& errorMessage);

    // Cooks every catalogued asset the cooker understands. Returns the number cooked
    // (freshly written, not those already up to date); failures are appended to
    // `errors` rather than thrown, so one bad asset does not abort a build.
    static int cookAll(AssetDatabase& database, std::vector<std::string>& errors);

    // Discovers what `resourceKey` references (a model's base-colour textures today)
    // and reports the edges to the database, which owns them. Returns the dependency
    // keys. No-op without a database -- discovery is the cooker's job, storage is not.
    //
    // Called by cookAll, so a build produces the graph packaging needs; also callable
    // on its own when the editor wants edges without cooking.
    static std::vector<std::string> discoverDependencies(const std::string& resourceKey);

    // Drops the in-process "already checked this key" memo. The file cache on disk is
    // untouched -- this is for tests and for the hot-reload path, which knows a source
    // changed before the memo would notice.
    static void invalidate(const std::string& resourceKey);
    static void clearMemo();
};
