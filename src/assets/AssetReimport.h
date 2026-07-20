#pragma once

#include <string>

class AssetDatabase;

// The one implementation of "this asset changed, bring everything back in line"
// (docs/DESIGN_ASSET_PIPELINE.md, Phase 19D).
//
// Two callers need it: the file watcher (a file changed on disk) and the editor (a
// developer pressed Reimport, or edited import settings). They must not be two code
// paths. An editor-only shortcut is how "it works when I save the file but not when I
// press the button" bugs are born, and it would give the engine a second answer to what
// importing means -- the same argument that keeps asset identity in one function and the
// dependency graph in one owner.
//
// Ownership is unchanged by this file. It orchestrates; it owns nothing:
//   AssetDatabase  re-reads bytes and .meta, and owns the dependency edges
//   AssetCooker    drops its memo, rediscovers edges, recooks on next load
//   ResourceManager reloads what it already had, and still never learns why
namespace AssetReimport {

struct Result {
    bool known = false;      // the path named a catalogued asset
    bool changed = false;    // the cook key moved (content or settings really differ)
    bool reloaded = false;   // a live resource was swapped
    std::string assetKey;    // the asset acted on, after ".meta" -> owner mapping
    std::string errorMessage;
};

// `pathOrKey` may be an asset path, an asset key, or a "<asset>.meta" sidecar path --
// the sidecar maps to the asset it configures, because import settings are source.
//
// `force` distinguishes the two callers: the watcher passes false, so a file that was
// touched but not edited does nothing (mtime is a trigger, the content hash is the
// answer). The editor's Reimport passes true, because "nothing changed" is exactly the
// state a developer presses that button to escape.
//
// Requires a Vulkan device only for the reload step, and checks rather than assumes:
// a headless caller reimports and recooks with no device and simply reloads nothing.
Result reimport(AssetDatabase& database, const std::string& pathOrKey, bool force);

} // namespace AssetReimport
