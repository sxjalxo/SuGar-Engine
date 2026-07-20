#include "assets/AssetReimport.h"

#include "assets/AssetCooker.h"
#include "assets/AssetDatabase.h"
#include "assets/ResourceManager.h"

AssetReimport::Result AssetReimport::reimport(
    AssetDatabase& database,
    const std::string& pathOrKey,
    bool force
) {
    Result result;

    // A changed .meta is a changed asset: import settings are source, and they are
    // baked in at cook time. The catalog deliberately does not list sidecars, so this
    // mapping is where a watcher report about one becomes an asset again.
    const std::string metaOwner = database.assetKeyForMetaPath(pathOrKey);
    const std::string key = metaOwner.empty() ? pathOrKey : metaOwner;

    const AssetEntry* entry = database.find(key);
    if (entry == nullptr) {
        // Not catalogued: a brand-new file the last scan never saw, or something
        // outside the asset root. The caller rescans; nothing here should guess.
        return result;
    }

    result.known = true;
    result.assetKey = entry->key;

    result.changed = database.refresh(result.assetKey);
    if (!result.changed && !force) {
        // Touched, not edited -- a save that wrote identical bytes, or a branch switch
        // and back. The cook key did not move, so every artifact on disk is still
        // current and there is nothing to reload.
        return result;
    }

    // The source may reference different assets now (a re-exported model pointing at a
    // new texture), so the graph is rediscovered before anything consumes it. The cooker
    // reports the edges; the database owns them.
    AssetCooker::invalidate(result.assetKey);
    AssetCooker::discoverDependencies(result.assetKey);

    // Reload only what is already loaded, and only with a device. Cooking happens on
    // demand inside reloadAsset, so a headless reimport still recooks -- it just has
    // nothing to swap.
    if (!ResourceManager::isInitialized()) {
        return result;
    }

    try {
        result.reloaded = ResourceManager::reloadAsset(result.assetKey);
    } catch (const std::exception& exception) {
        result.errorMessage = exception.what();
    }
    return result;
}
