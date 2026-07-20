#pragma once

#include <map>
#include <string>

// Import settings for one asset, stored beside it:
//   assets/models/hero.gltf  ->  assets/models/hero.gltf.meta
//
// Sidecar rather than one central manifest: no merge conflicts on a shared file, and
// moving an asset moves its settings with it. These files are SOURCE, not cache --
// they are committed, and two developers must cook the same bytes from the same
// checkout (docs/DESIGN_ASSET_PIPELINE.md).
//
// A missing .meta is legal: defaults apply and the file is written on first import.
// Dropping an asset into the project must never require ceremony (Rule 1).
enum class AssetType {
    Unknown,
    Model,
    Texture,
    Audio,
    Prefab,
    Scene,
    UI,
    Font,
    NavMesh,
};

// Type <-> string, for the .meta file and for editor display. Stable spellings: they
// are written to disk.
const char* assetTypeName(AssetType type);
AssetType assetTypeFromName(const std::string& name);

// The type implied by a file extension, for the default .meta of a new asset.
AssetType assetTypeFromExtension(const std::string& lowercaseExtension);

struct AssetMeta {
    // Bumped when the .meta *schema* changes (not when a setting's value changes).
    // Read tolerates older versions; write always emits Version.
    static constexpr int Version = 1;

    int version = Version;
    AssetType type = AssetType::Unknown;

    // Import settings, untyped on purpose in 19A: the database's job is to carry them
    // and hash them, and 19C is where each importer claims the keys it understands.
    // std::map (not unordered_map) because the file must serialize in a stable order --
    // cook keys hash these bytes, and a hash that depends on bucket layout is not a
    // function of the content (Rule 10).
    std::map<std::string, std::string> settings;

    std::string get(const std::string& key, const std::string& fallback = std::string()) const;
    void set(const std::string& key, const std::string& value);

    // Typed reads for the importers (19C). A malformed or missing value yields the
    // fallback rather than an error: a .meta is hand-editable, and a typo must not stop
    // an asset from cooking -- it should cook with the default and be visible in the
    // editor. Locale-independent parsing (no std::stof / std::locale).
    float getFloat(const std::string& key, float fallback) const;
    bool getBool(const std::string& key, bool fallback) const;
};

// The setting keys each importer understands (19C). Named here rather than spelled as
// literals at the use site, because these strings are written into .meta files on disk
// and are therefore part of the project format.
namespace AssetSettings {
constexpr const char* ModelScale = "scale";   // float, default 1.0 -- scales vertex positions
constexpr const char* TextureFlipY = "flipY"; // bool,  default false -- flips row order
constexpr const char* AudioGain = "gain";     // float, default 1.0 -- scales samples
} // namespace AssetSettings

namespace AssetMetaIO {

// The sidecar path for a source path: "<source>.meta". Deliberately appended rather
// than extension-replacing, so "hero.gltf" and "hero.png" don't collide on "hero.meta".
std::string sidecarPath(const std::string& sourcePath);

// Reads a sidecar. Returns false if the file is absent or malformed; `out` is left with
// whatever defaults the caller set (a malformed .meta must not silently become an empty
// one that then overwrites the developer's settings).
bool read(const std::string& metaPath, AssetMeta& out, std::string& errorMessage);

// Writes a sidecar deterministically: fixed key order, '\n' line endings, no timestamps.
// Two machines writing the same settings produce byte-identical files.
bool write(const std::string& metaPath, const AssetMeta& meta, std::string& errorMessage);

// The exact bytes write() would produce. This is what the cook key hashes, so staleness
// is a function of the settings rather than of the file's incidental formatting.
std::string serialize(const AssetMeta& meta);

} // namespace AssetMetaIO
