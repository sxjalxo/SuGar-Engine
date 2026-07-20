#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "assets/AssetMeta.h"

// The asset catalog (docs/DESIGN_ASSET_PIPELINE.md, Phase 19A).
//
// Owns exactly one thing: what assets exist, what their import settings are, and
// whether their cooked form is stale. It owns no loaded resource and no GPU object --
// that is ResourceManager -- and it does not cook, which is AssetCooker (19B). Core and
// headless: the cooker tool, CI and the self-tests must be able to ask these questions
// without a Vulkan device.
//
// Staleness is a content hash, never an mtime: touching a file, switching branches back
// and forth, or a fresh checkout must not recook (Rule 21b -- the cook cache is derived
// from the present source bytes, so it is never serialized and always safe to delete).
struct AssetEntry {
    // The identity (AssetPath::normalize). What a component stores, what the cook key
    // hashes, what every lookup goes through.
    std::string key;

    // The path as scanned, project-relative with '/' separators and original case --
    // for opening the file and for showing the developer. `key` is for identity;
    // `path` is for I/O and display, and the two differ whenever a filename has
    // uppercase in it.
    std::string path;
    std::string name;      // filename with extension
    std::string extension; // lowercase, with the dot

    AssetType type = AssetType::Unknown;

    // Import settings. Defaults when no sidecar exists on disk (`hasMeta == false`) --
    // a missing .meta is legal, not an error.
    AssetMeta meta;
    bool hasMeta = false;

    // FNV-1a of the source file's bytes. `hashValid` is false when the file could not
    // be read; such an entry is reported rather than silently cooked.
    uint64_t contentHash = 0;
    bool hashValid = false;

    // H(key, contentHash, serialized meta, cooker version) -- the name a cooked
    // artifact will be stored under in 19B, and the whole of the staleness test.
    uint64_t cookKey = 0;
};

class AssetDatabase {
public:
    // Rescans `root` (recursively) and rebuilds the catalog. Deterministic: entries are
    // sorted byte-wise by key, and problems are reported in the same order every run.
    // Sidecar .meta files are catalogued as settings for their asset, never as assets.
    void scan(const std::string& root);

    // Sorted by key. Stable across runs on the same tree.
    const std::vector<AssetEntry>& getAssets() const { return assets; }

    // Lookup by any spelling of the key -- normalized for you. Null if unknown.
    // The '#sub' selector is stripped: sub-assets share their file's entry.
    const AssetEntry* find(const std::string& rawKey) const;

    // Human-readable problems from the last scan: non-ASCII filenames, keys that two
    // files claim (a case-only collision), and malformed .meta files. Surfaced, never
    // silently swallowed (Rule 13) -- the editor shows these in 19D.
    const std::vector<std::string>& getProblems() const { return problems; }

    // Re-reads one asset's bytes and .meta and updates its entry. Returns true if the
    // cook key changed, i.e. the file watcher's mtime signal turned out to be a real
    // content change rather than a touch. Unknown paths return false.
    bool refresh(const std::string& rawKey);

    // Writes the default sidecar for an asset that has none, and adopts it. No-op (true)
    // if one already exists.
    bool ensureMetaOnDisk(const std::string& rawKey, std::string& errorMessage);

    // --- dependency edges (19C) ---------------------------------------------
    // The database OWNS this metadata; only the cooker can DISCOVER it, because the
    // edges come out of parsing a source format. So the cooker reports edges here
    // rather than keeping its own table -- storage stays with the catalog, discovery
    // stays with the parser (docs/DESIGN_ASSET_PIPELINE.md).
    //
    // Edges are derived (Rule 21b): recomputed from the present source bytes, never
    // serialized, never in a snapshot. A rescan clears them; a missing edge is a
    // rescan away, which is exactly why they can be trusted after a restore.
    void setDependencies(const std::string& rawKey, const std::vector<std::string>& dependencyKeys);

    // What this asset references. Empty if it has no edges, or none discovered yet --
    // the two are indistinguishable on purpose: "not cooked yet" is not a state callers
    // should branch on.
    const std::vector<std::string>& dependenciesOf(const std::string& rawKey) const;

    // What references this asset. The direction packaging needs (Phase 20: ship the
    // textures a shipped model reaches, even when no scene names them) and the one the
    // editor shows in 19D.
    std::vector<std::string> dependentsOf(const std::string& rawKey) const;

    // The mapping the file watcher needs: "<asset>.meta" -> the asset it configures.
    // Returns "" if the path is not a sidecar or names no catalogued asset. Editing
    // import settings must reimport the asset, and without this the watcher reports a
    // file the catalog deliberately does not know (sidecars are settings, not assets).
    std::string assetKeyForMetaPath(const std::string& metaPath) const;

    // The cook key formula, public so the cooker and the tests agree on one definition.
    // Order is part of the format: key, content hash, meta bytes, cooker version.
    static uint64_t computeCookKey(
        const std::string& key,
        uint64_t contentHash,
        const std::string& serializedMeta
    );

private:
    void indexAssets();
    static bool isMetaFile(const std::string& path);

    std::vector<AssetEntry> assets;
    std::unordered_map<std::string, size_t> keyToIndex;
    std::vector<std::string> problems;

    // key -> what it references. Cleared by scan(): edges are a function of the source
    // bytes that were just re-read, so carrying old ones across a rescan would make
    // them a function of history.
    std::unordered_map<std::string, std::vector<std::string>> dependencies;
};
