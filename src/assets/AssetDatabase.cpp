#include "assets/AssetDatabase.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "assets/AssetHash.h"
#include "assets/AssetPath.h"

namespace {

std::string lowerAscii(std::string value) {
    // ASCII-only and locale-independent, for the same reason AssetPath lowers this way:
    // std::tolower under a Turkish locale folds 'I' to a dotless 'i', and the catalog
    // would then differ per machine.
    for (char& character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte >= 'A' && byte <= 'Z') {
            character = static_cast<char>(byte - 'A' + 'a');
        }
    }
    return value;
}

void fillEntryContent(AssetEntry& entry, std::vector<std::string>* problems) {
    uint64_t hash = 0;
    entry.hashValid = AssetHash::hashFile(entry.path, hash);
    entry.contentHash = entry.hashValid ? hash : 0;
    if (!entry.hashValid && problems != nullptr) {
        problems->push_back("could not read asset file: " + entry.path);
    }

    AssetMeta meta;
    meta.type = entry.type;
    const std::string metaPath = AssetMetaIO::sidecarPath(entry.path);

    entry.hasMeta = false;
    if (std::filesystem::exists(metaPath)) {
        std::string errorMessage;
        if (AssetMetaIO::read(metaPath, meta, errorMessage)) {
            entry.hasMeta = true;
        } else if (problems != nullptr) {
            // A malformed sidecar falls back to defaults for *cooking*, but it is
            // reported rather than rewritten: overwriting it would destroy settings the
            // developer meant to keep, and the fix is to look at the file.
            problems->push_back(errorMessage);
        }
    }

    if (meta.type == AssetType::Unknown) {
        meta.type = entry.type;
    }
    entry.meta = meta;
    entry.type = meta.type;
    entry.cookKey = AssetDatabase::computeCookKey(
        entry.key,
        entry.contentHash,
        AssetMetaIO::serialize(entry.meta)
    );
}

} // namespace

bool AssetDatabase::isMetaFile(const std::string& path) {
    constexpr const char* suffix = ".meta";
    constexpr size_t suffixLength = 5;
    return path.size() > suffixLength &&
           lowerAscii(path.substr(path.size() - suffixLength)) == suffix;
}

uint64_t AssetDatabase::computeCookKey(
    const std::string& key,
    uint64_t contentHash,
    const std::string& serializedMeta
) {
    // Fixed input order -- this IS the cook format's key, so the order is part of it.
    // The cooker version is folded in last so a format change (or a change of hash
    // algorithm, which bumps the same counter) invalidates every entry at once.
    uint64_t hash = AssetHash::hashString(key);
    hash = AssetHash::hashCombine(hash, &contentHash, sizeof(contentHash));
    hash = AssetHash::hashCombineString(hash, serializedMeta);
    const uint32_t version = AssetHash::CookerVersion;
    hash = AssetHash::hashCombine(hash, &version, sizeof(version));
    return hash;
}

void AssetDatabase::scan(const std::string& root) {
    assets.clear();
    keyToIndex.clear();
    problems.clear();
    dependencies.clear();

    std::error_code errorCode;
    const std::filesystem::path rootPath(root);
    const std::filesystem::path projectPath = std::filesystem::current_path(errorCode);

    std::filesystem::recursive_directory_iterator iterator(rootPath, errorCode);
    if (errorCode) {
        problems.push_back("could not scan asset root: " + root);
        return;
    }

    for (const auto& directoryEntry : iterator) {
        std::error_code fileError;
        if (!directoryEntry.is_regular_file(fileError) || fileError) {
            continue;
        }

        // Sidecars are settings for an asset, not assets. They are read through the
        // asset they belong to, so a stray .meta with no asset is simply invisible.
        const std::string rawPath = directoryEntry.path().generic_string();
        if (isMetaFile(rawPath)) {
            continue;
        }

        // Project-relative when the asset is under the project, absolute otherwise. A
        // relative path that climbs out with ".." is not usable as a key -- it would
        // normalize away to nothing -- and an asset root outside the working directory
        // is a legitimate case (a cook tool run from elsewhere, a test tree in temp).
        // The absolute form still anchors at "assets/", so both spell the same key.
        std::error_code relativeError;
        const std::filesystem::path relativePath =
            std::filesystem::relative(directoryEntry.path(), projectPath, relativeError);
        const std::string relativeText = relativePath.generic_string();
        const bool climbsOut = relativeText.rfind("..", 0) == 0;
        const std::string displayPath =
            (relativeError || relativeText.empty() || climbsOut) ? rawPath : relativeText;

        if (!AssetPath::isSupportedAscii(displayPath)) {
            problems.push_back("asset path is not ASCII (unsupported): " + displayPath);
            continue;
        }

        AssetEntry entry;
        entry.key = AssetPath::normalize(displayPath);
        if (entry.key.empty()) {
            problems.push_back("asset path does not normalize to a key: " + displayPath);
            continue;
        }
        entry.path = displayPath;
        entry.name = directoryEntry.path().filename().string();
        entry.extension = lowerAscii(directoryEntry.path().extension().string());
        entry.type = assetTypeFromExtension(entry.extension);

        fillEntryContent(entry, &problems);
        assets.push_back(std::move(entry));
    }

    // Byte-wise sort (std::string::operator<), never a locale collation: the catalog's
    // order is part of what packaging and the cooker walk, and it must not depend on
    // the machine's locale.
    std::sort(assets.begin(), assets.end(), [](const AssetEntry& left, const AssetEntry& right) {
        return left.key < right.key;
    });

    indexAssets();
    std::sort(problems.begin(), problems.end());
}

void AssetDatabase::indexAssets() {
    keyToIndex.clear();
    for (size_t i = 0; i < assets.size(); i++) {
        const auto inserted = keyToIndex.emplace(assets[i].key, i);
        if (!inserted.second) {
            // Two files normalize to one key -- almost always a case-only difference,
            // which is one asset on Windows/macOS and two on Linux. Reported instead of
            // picked between, because either choice silently breaks one machine.
            problems.push_back(
                "two files claim the asset key '" + assets[i].key + "': " +
                assets[inserted.first->second].path + " and " + assets[i].path
            );
        }
    }
}

const AssetEntry* AssetDatabase::find(const std::string& rawKey) const {
    const std::string key = AssetPath::pathOf(rawKey);
    if (key.empty()) {
        return nullptr;
    }
    const auto found = keyToIndex.find(key);
    return found == keyToIndex.end() ? nullptr : &assets[found->second];
}

bool AssetDatabase::refresh(const std::string& rawKey) {
    const std::string key = AssetPath::pathOf(rawKey);
    if (key.empty()) {
        return false;
    }
    const auto found = keyToIndex.find(key);
    if (found == keyToIndex.end()) {
        return false;
    }

    AssetEntry& entry = assets[found->second];
    const uint64_t previousCookKey = entry.cookKey;
    fillEntryContent(entry, nullptr);
    return entry.cookKey != previousCookKey;
}

bool AssetDatabase::ensureMetaOnDisk(const std::string& rawKey, std::string& errorMessage) {
    const std::string key = AssetPath::pathOf(rawKey);
    const auto found = keyToIndex.find(key);
    if (key.empty() || found == keyToIndex.end()) {
        errorMessage = "unknown asset key: " + rawKey;
        return false;
    }

    AssetEntry& entry = assets[found->second];
    if (entry.hasMeta) {
        return true;
    }
    if (!AssetMetaIO::write(AssetMetaIO::sidecarPath(entry.path), entry.meta, errorMessage)) {
        return false;
    }
    entry.hasMeta = true;
    return true;
}

void AssetDatabase::setDependencies(
    const std::string& rawKey,
    const std::vector<std::string>& dependencyKeys
) {
    const std::string key = AssetPath::pathOf(rawKey);
    if (key.empty()) {
        return;
    }

    std::vector<std::string> normalized;
    normalized.reserve(dependencyKeys.size());
    for (const std::string& dependency : dependencyKeys) {
        const std::string dependencyKey = AssetPath::normalize(dependency);
        // Self-edges and unnormalizable paths are dropped rather than stored: a graph
        // that can contain "assets/x -> assets/x" makes every later walk defensive.
        if (!dependencyKey.empty() && dependencyKey != key) {
            normalized.push_back(dependencyKey);
        }
    }

    // Sorted and de-duplicated so the edge list is a function of the *set* of
    // references, not of the order the parser happened to walk the file -- the same
    // determinism obligation the cooked bytes are held to.
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());

    if (normalized.empty()) {
        dependencies.erase(key);
        return;
    }
    dependencies[key] = std::move(normalized);
}

const std::vector<std::string>& AssetDatabase::dependenciesOf(const std::string& rawKey) const {
    static const std::vector<std::string> none;
    const auto found = dependencies.find(AssetPath::pathOf(rawKey));
    return found == dependencies.end() ? none : found->second;
}

std::vector<std::string> AssetDatabase::dependentsOf(const std::string& rawKey) const {
    const std::string key = AssetPath::pathOf(rawKey);
    std::vector<std::string> dependents;
    if (key.empty()) {
        return dependents;
    }

    for (const auto& edge : dependencies) {
        if (std::find(edge.second.begin(), edge.second.end(), key) != edge.second.end()) {
            dependents.push_back(edge.first);
        }
    }

    // Deterministic order: the map is unordered, and packaging walks this.
    std::sort(dependents.begin(), dependents.end());
    return dependents;
}

std::string AssetDatabase::assetKeyForMetaPath(const std::string& metaPath) const {
    if (!isMetaFile(metaPath)) {
        return std::string();
    }

    // Sidecars are "<source path>.meta", so the owning asset is the path with the
    // suffix removed -- not an extension swap, which is why "hero.gltf" and "hero.png"
    // can both have sidecars without colliding.
    const std::string ownerPath = metaPath.substr(0, metaPath.size() - 5);
    const AssetEntry* owner = find(ownerPath);
    return owner != nullptr ? owner->key : std::string();
}
