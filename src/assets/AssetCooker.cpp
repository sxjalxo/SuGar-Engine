#include "assets/AssetCooker.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <unordered_map>

#include "assets/AssetDatabase.h"
#include "assets/AssetHash.h"
#include "assets/AssetMeta.h"
#include "assets/AssetPath.h"
#include "assets/CookedAsset.h"
#include "assets/GltfLoader.h"
#include "assets/GltfModel.h"
#include "assets/ModelLoader.h"
#include "audio/AudioClip.h"
#include "audio/AudioLoader.h"
#include "rendering/Mesh.h"
#include "stb_image.h"

namespace {

std::string cacheDirectoryPath = "build/assetcache";
AssetDatabase* catalog = nullptr;

// Keys whose artifact this process already produced or verified. Purely a speed memo:
// deleting it costs a hash and a stat, never correctness, which is why invalidate() is
// cheap to call liberally from the hot-reload path.
std::unordered_map<std::string, uint64_t> ensuredArtifacts;

std::string lowerExtension(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    for (char& character : extension) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte >= 'A' && byte <= 'Z') {
            character = static_cast<char>(byte - 'A' + 'a');
        }
    }
    return extension;
}

// The on-disk file for a resource key. The catalog knows the real spelling (keys are
// lowercased, filenames are not); without it, the key is the best guess available.
std::string sourcePathFor(const std::string& pathPart) {
    if (catalog != nullptr) {
        const AssetEntry* entry = catalog->find(pathPart);
        if (entry != nullptr) {
            return entry->path;
        }
    }
    return pathPart;
}

// The cook key for the *file*, before the sub-selector is folded in. From the catalog
// when it has one (it already hashed the source and the .meta), otherwise computed the
// same way from the file itself -- one formula, two callers, so a tool that never
// scanned a tree still agrees with the editor about what an artifact is called.
uint64_t fileCookKey(const std::string& pathPart) {
    if (catalog != nullptr) {
        const AssetEntry* entry = catalog->find(pathPart);
        if (entry != nullptr && entry->hashValid) {
            return entry->cookKey;
        }
    }

    const std::string path = sourcePathFor(pathPart);
    uint64_t contentHash = 0;
    if (!AssetHash::hashFile(path, contentHash)) {
        return 0;
    }

    AssetMeta meta;
    meta.type = assetTypeFromExtension(lowerExtension(path));
    std::string metaError;
    AssetMetaIO::read(AssetMetaIO::sidecarPath(path), meta, metaError);

    return AssetDatabase::computeCookKey(
        AssetPath::normalize(pathPart),
        contentHash,
        AssetMetaIO::serialize(meta)
    );
}

// The import settings for an asset. From the catalog when it has them, otherwise read
// from the sidecar directly -- same two-source pattern as fileCookKey, so a tool that
// never scanned a tree still cooks with the developer's settings rather than defaults.
AssetMeta metaFor(const std::string& pathPart) {
    if (catalog != nullptr) {
        const AssetEntry* entry = catalog->find(pathPart);
        if (entry != nullptr) {
            return entry->meta;
        }
    }

    const std::string path = sourcePathFor(pathPart);
    AssetMeta meta;
    meta.type = assetTypeFromExtension(lowerExtension(path));
    std::string metaError;
    AssetMetaIO::read(AssetMetaIO::sidecarPath(path), meta, metaError);
    return meta;
}

// True if a model file has geometry for the mesh cooker to produce. A glTF may carry
// only animation clips or only a skeleton (assets/models/AnimatedSpinner.gltf is one):
// its clips reach the engine through ModelImporter and AnimationClipRegistry, never
// through a Mesh. Cooking such a file is not a failure, it is a no-op -- and a build
// that exits nonzero over a legitimate asset trains developers to ignore the build.
// The extra parse happens only in cookAll (a build step), never on a load.
bool modelHasGeometry(const std::string& sourcePath, const std::string& extension) {
    if (extension != ".gltf" && extension != ".glb") {
        return true; // OBJ is geometry by definition
    }

    GltfModelData model;
    try {
        GltfLoader::loadModel(sourcePath, model);
    } catch (...) {
        return true; // let the real cook report the real error
    }

    for (const GltfNodeData& node : model.nodes) {
        if (node.meshIndex >= 0) {
            return true;
        }
    }
    return false;
}

bool cookMesh(
    const std::string& sourcePath,
    const std::string& sub,
    const AssetMeta& meta,
    Mesh& out,
    std::string& errorMessage
) {
    const std::string extension = lowerExtension(sourcePath);
    try {
        if (extension == ".gltf" || extension == ".glb") {
            // "<path>#<meshIndex>" selects one glTF mesh; no selector means "flatten
            // the whole file". Parsing the selector here is what keeps ResourceManager
            // from knowing glTF exists at all.
            int meshIndex = -1;
            if (!sub.empty()) {
                try {
                    meshIndex = std::stoi(sub);
                } catch (...) {
                    meshIndex = -1;
                }
            }
            if (meshIndex >= 0) {
                GltfLoader::loadGltfMesh(sourcePath, meshIndex, out);
            } else {
                GltfLoader::loadGltf(sourcePath, out);
            }
        } else {
            ModelLoader::loadObj(sourcePath, out);
        }
    } catch (const std::exception& exception) {
        errorMessage = "failed to cook mesh '" + sourcePath + "': " + exception.what();
        return false;
    }

    // Import settings are applied HERE, not at load: the result is baked into the
    // artifact, the runtime does no per-load work, and the setting is already in the
    // cook key (the .meta bytes are hashed), so changing it renames the artifact.
    const float scale = meta.getFloat(AssetSettings::ModelScale, 1.0f);
    if (scale != 1.0f) {
        for (Vertex& vertex : out.vertices) {
            vertex.pos[0] *= scale;
            vertex.pos[1] *= scale;
            vertex.pos[2] *= scale;
        }
    }
    return true;
}

bool cookTexture(
    const std::string& sourcePath,
    const AssetMeta& meta,
    CookedAsset::CookedTexture& out,
    std::string& errorMessage
) {
    int width = 0;
    int height = 0;
    int sourceChannels = 0;

    // Force RGBA8 so the cooked payload matches VK_FORMAT_R8G8B8A8_SRGB exactly and the
    // runtime upload path never has to convert.
    stbi_uc* data = stbi_load(sourcePath.c_str(), &width, &height, &sourceChannels, STBI_rgb_alpha);
    if (data == nullptr) {
        const char* reason = stbi_failure_reason();
        errorMessage = "failed to cook texture '" + sourcePath + "'" +
                       (reason != nullptr ? std::string(": ") + reason : std::string());
        return false;
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(data);
        errorMessage = "texture has an invalid size: " + sourcePath;
        return false;
    }

    const size_t pixelBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    out.width = static_cast<uint32_t>(width);
    out.height = static_cast<uint32_t>(height);
    out.pixels.resize(pixelBytes);
    std::memcpy(out.pixels.data(), data, pixelBytes);
    stbi_image_free(data);

    // flipY answers the GL-vs-Vulkan texture-origin question per asset instead of
    // globally: some sources are authored bottom-up, and a project-wide flag would be
    // wrong for half of them. Done at cook time, so the runtime never flips.
    if (meta.getBool(AssetSettings::TextureFlipY, false)) {
        const size_t rowBytes = static_cast<size_t>(out.width) * 4;
        for (uint32_t row = 0; row < out.height / 2; row++) {
            uint8_t* top = out.pixels.data() + static_cast<size_t>(row) * rowBytes;
            uint8_t* bottom = out.pixels.data() +
                              static_cast<size_t>(out.height - 1 - row) * rowBytes;
            std::swap_ranges(top, top + rowBytes, bottom);
        }
    }
    return true;
}

bool cookAudio(
    const std::string& sourcePath,
    const AssetMeta& meta,
    AudioClip& out,
    std::string& errorMessage
) {
    if (!AudioLoader::loadClip(sourcePath, out)) {
        errorMessage = "failed to cook audio clip: " + sourcePath;
        return false;
    }

    // Baked gain, not a runtime multiply: the mixer stays a mixer, and a clip that is
    // simply too loud is fixed by the asset rather than by every entity that plays it.
    const float gain = meta.getFloat(AssetSettings::AudioGain, 1.0f);
    if (gain != 1.0f) {
        for (float& sample : out.samples) {
            sample *= gain;
        }
    }
    return true;
}

} // namespace

void AssetCooker::setCacheDirectory(const std::string& directory) {
    cacheDirectoryPath = directory;
    ensuredArtifacts.clear();
}

const std::string& AssetCooker::cacheDirectory() {
    return cacheDirectoryPath;
}

void AssetCooker::setDatabase(AssetDatabase* database) {
    catalog = database;
    ensuredArtifacts.clear();
}

uint64_t AssetCooker::artifactKey(const std::string& resourceKey) {
    const std::string pathPart = AssetPath::pathOf(resourceKey);
    if (pathPart.empty()) {
        return 0;
    }

    const uint64_t base = fileCookKey(pathPart);
    if (base == 0) {
        return 0;
    }

    // Sub-assets of one file are separate artifacts that invalidate together: the
    // file's cook key is the seed, the selector distinguishes the outputs.
    const std::string sub = AssetPath::subOf(resourceKey);
    return sub.empty() ? base : AssetHash::hashCombineString(base, sub);
}

std::string AssetCooker::artifactPath(uint64_t key) {
    return cacheDirectoryPath + "/" + AssetHash::toHex(key) + ".sgc";
}

std::string AssetCooker::ensureCooked(const std::string& resourceKey, std::string& errorMessage) {
    const std::string pathPart = AssetPath::pathOf(resourceKey);
    if (pathPart.empty()) {
        errorMessage = "not an asset key: " + resourceKey;
        return std::string();
    }

    const auto memo = ensuredArtifacts.find(resourceKey);
    if (memo != ensuredArtifacts.end()) {
        return artifactPath(memo->second);
    }

    const uint64_t key = artifactKey(resourceKey);
    if (key == 0) {
        errorMessage = "no readable source for asset: " + resourceKey;
        return std::string();
    }

    const std::string cooked = artifactPath(key);
    if (std::filesystem::exists(cooked)) {
        // The filename IS the cook key, so an artifact that exists is by construction
        // current: source bytes, .meta and cooker version all fed the name.
        ensuredArtifacts[resourceKey] = key;
        return cooked;
    }

    const std::string sourcePath = sourcePathFor(pathPart);
    const std::string sub = AssetPath::subOf(resourceKey);
    const AssetType type = assetTypeFromExtension(lowerExtension(sourcePath));
    const AssetMeta meta = metaFor(pathPart);

    bool cooked_ok = false;
    switch (type) {
        case AssetType::Model: {
            Mesh mesh;
            cooked_ok = cookMesh(sourcePath, sub, meta, mesh, errorMessage) &&
                        CookedAsset::writeMesh(cooked, mesh, errorMessage);
            break;
        }
        case AssetType::Texture: {
            CookedAsset::CookedTexture texture;
            cooked_ok = cookTexture(sourcePath, meta, texture, errorMessage) &&
                        CookedAsset::writeTexture(cooked, texture, errorMessage);
            break;
        }
        case AssetType::Audio: {
            AudioClip clip;
            cooked_ok = cookAudio(sourcePath, meta, clip, errorMessage) &&
                        CookedAsset::writeAudio(cooked, clip, errorMessage);
            break;
        }
        default:
            errorMessage = "no cooker for asset type: " + resourceKey;
            return std::string();
    }

    if (!cooked_ok) {
        return std::string();
    }

    ensuredArtifacts[resourceKey] = key;
    return cooked;
}

int AssetCooker::cookAll(AssetDatabase& database, std::vector<std::string>& errors) {
    AssetDatabase* previous = catalog;
    catalog = &database;

    int cookedCount = 0;
    for (const AssetEntry& entry : database.getAssets()) {
        if (entry.type != AssetType::Model &&
            entry.type != AssetType::Texture &&
            entry.type != AssetType::Audio) {
            continue; // prefabs, scenes and UI documents are consumed as-is
        }
        if (entry.type == AssetType::Model) {
            // Edges first: an animation-only model still references textures, and
            // packaging needs those even though there is no mesh to cook.
            discoverDependencies(entry.key);

            if (!modelHasGeometry(entry.path, lowerExtension(entry.path))) {
                continue; // animation- or skeleton-only model: nothing for the mesh cooker
            }
        }

        const uint64_t key = artifactKey(entry.key);
        const bool alreadyCooked = key != 0 && std::filesystem::exists(artifactPath(key));

        std::string errorMessage;
        if (ensureCooked(entry.key, errorMessage).empty()) {
            errors.push_back(errorMessage);
            continue;
        }
        if (!alreadyCooked) {
            cookedCount++;
        }
    }

    catalog = previous;
    return cookedCount;
}

std::vector<std::string> AssetCooker::discoverDependencies(const std::string& resourceKey) {
    std::vector<std::string> dependencies;

    const std::string pathPart = AssetPath::pathOf(resourceKey);
    if (pathPart.empty()) {
        return dependencies;
    }

    const std::string sourcePath = sourcePathFor(pathPart);
    const std::string extension = lowerExtension(sourcePath);
    if (extension == ".gltf" || extension == ".glb") {
        GltfModelData model;
        try {
            GltfLoader::loadModel(sourcePath, model);
        } catch (...) {
            return dependencies; // the cook itself reports the real error
        }

        // Base-colour textures only. Not every reference a format can express -- the
        // ones a real project has needed (Rule 18). Adding normal/ORM maps later is a
        // line each, and nothing about the graph's shape changes.
        for (const GltfMaterialData& material : model.materials) {
            if (!material.baseColorTexture.empty()) {
                dependencies.push_back(material.baseColorTexture);
            }
        }
    }

    // Report, do not keep: the database owns dependency metadata (19C). A table here
    // would be a second answer to "what does this reference", and second answers drift.
    if (catalog != nullptr) {
        catalog->setDependencies(pathPart, dependencies);
    }
    return dependencies;
}

void AssetCooker::invalidate(const std::string& resourceKey) {
    ensuredArtifacts.erase(resourceKey);

    // Sub-assets share the file: a changed .gltf invalidates every "<path>#<sub>" memo
    // that was taken from it, not only the exact key the watcher reported.
    const std::string pathPart = AssetPath::pathOf(resourceKey);
    for (auto it = ensuredArtifacts.begin(); it != ensuredArtifacts.end();) {
        it = AssetPath::pathOf(it->first) == pathPart ? ensuredArtifacts.erase(it) : std::next(it);
    }
}

void AssetCooker::clearMemo() {
    ensuredArtifacts.clear();
}
