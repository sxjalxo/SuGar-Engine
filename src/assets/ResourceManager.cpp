#include "assets/ResourceManager.h"
#include "assets/AssetCooker.h"
#include "assets/AssetPath.h"
#include "assets/CookedAsset.h"
#include "audio/AudioClip.h"
#include "rendering/Mesh.h"
#include "rendering/Texture.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
constexpr int ReloadRetryCount = 10;
constexpr auto ReloadRetryDelay = std::chrono::milliseconds(50);

// Runtime = f(cooked) (docs/DESIGN_ASSET_PIPELINE.md). Every load below goes through
// the cooker and reads a cooked artifact; no source format is parsed here any more.
// That is what 19B bought: this file used to know about glTF mesh indices, OBJ and
// stb_image, so every new source format touched the runtime load path. Cooking happens
// on demand, which keeps the editor's "drop a file in and it appears" behaviour and
// means no developer has to know a cook step exists (Rule 1).
void loadCookedMesh(const std::string& key, Mesh& mesh) {
    std::string errorMessage;
    const std::string cooked = AssetCooker::ensureCooked(key, errorMessage);
    if (cooked.empty() || !CookedAsset::readMesh(cooked, mesh, errorMessage)) {
        throw std::runtime_error(errorMessage);
    }
}

bool loadCookedTexture(
    const std::string& key,
    std::vector<uint8_t>& pixels,
    uint32_t& width,
    uint32_t& height,
    std::string& errorMessage
) {
    const std::string cooked = AssetCooker::ensureCooked(key, errorMessage);
    if (cooked.empty()) {
        return false;
    }

    CookedAsset::CookedTexture texture;
    if (!CookedAsset::readTexture(cooked, texture, errorMessage)) {
        return false;
    }

    pixels = std::move(texture.pixels);
    width = texture.width;
    height = texture.height;
    return true;
}

bool loadCookedAudio(const std::string& key, AudioClip& clip, std::string& errorMessage) {
    const std::string cooked = AssetCooker::ensureCooked(key, errorMessage);
    return !cooked.empty() && CookedAsset::readAudio(cooked, clip, errorMessage);
}

template <typename Loader>
void retryTransientLoad(const Loader& loader) {
    std::exception_ptr lastError;

    for (int attempt = 0; attempt < ReloadRetryCount; ++attempt) {
        try {
            loader();
            return;
        } catch (...) {
            lastError = std::current_exception();
            std::this_thread::sleep_for(ReloadRetryDelay);
        }
    }

    if (lastError) {
        std::rethrow_exception(lastError);
    }
}
} // namespace

VkDevice ResourceManager::device = VK_NULL_HANDLE;
VkPhysicalDevice ResourceManager::physicalDevice = VK_NULL_HANDLE;
VkCommandPool ResourceManager::commandPool = VK_NULL_HANDLE;
VkQueue ResourceManager::graphicsQueue = VK_NULL_HANDLE;
bool ResourceManager::initialized = false;
AssetHandle ResourceManager::nextHandle = 1;
std::unordered_map<AssetHandle, ResourceEntry<Mesh>> ResourceManager::meshTable;
std::unordered_map<AssetHandle, ResourceEntry<Texture>> ResourceManager::textureTable;
std::unordered_map<AssetHandle, ResourceEntry<AudioClip>> ResourceManager::audioClipTable;
std::unordered_map<std::string, AssetHandle> ResourceManager::meshPathToHandle;
std::unordered_map<std::string, AssetHandle> ResourceManager::texturePathToHandle;
std::unordered_map<std::string, AssetHandle> ResourceManager::audioClipPathToHandle;

void ResourceManager::init(
    VkDevice newDevice,
    VkPhysicalDevice newPhysicalDevice,
    VkCommandPool newCommandPool,
    VkQueue newGraphicsQueue
) {
    if (newDevice == VK_NULL_HANDLE ||
        newPhysicalDevice == VK_NULL_HANDLE ||
        newCommandPool == VK_NULL_HANDLE ||
        newGraphicsQueue == VK_NULL_HANDLE) {
        throw std::runtime_error("resource manager requires a valid Vulkan upload context.");
    }

    device = newDevice;
    physicalDevice = newPhysicalDevice;
    commandPool = newCommandPool;
    graphicsQueue = newGraphicsQueue;
    initialized = true;
}

AssetHandle ResourceManager::loadMesh(const std::string& path) {
    ensureInitialized();

    const std::string cacheKey = normalizeResourceKey(path);
    const auto existingHandle = meshPathToHandle.find(cacheKey);
    if (existingHandle != meshPathToHandle.end()) {
        meshTable.at(existingHandle->second).refCount++;
        return existingHandle->second;
    }

    auto mesh = std::make_shared<Mesh>();
    loadCookedMesh(cacheKey, *mesh);
    mesh->setResourceKey(cacheKey);
    mesh->upload(device, physicalDevice, commandPool, graphicsQueue);

    const AssetHandle handle = nextHandle++;
    meshTable.emplace(handle, ResourceEntry<Mesh>{mesh, cacheKey, 1});
    meshPathToHandle.emplace(cacheKey, handle);
    return handle;
}

AssetHandle ResourceManager::loadTexture(const std::string& path) {
    ensureInitialized();

    const std::string cacheKey = normalizeResourceKey(path);
    const auto existingHandle = texturePathToHandle.find(cacheKey);
    if (existingHandle != texturePathToHandle.end()) {
        textureTable.at(existingHandle->second).refCount++;
        return existingHandle->second;
    }

    auto texture = std::make_shared<Texture>();
    texture->setResourceKey(cacheKey);

    if (cacheKey == CheckerboardTextureId) {
        texture->createCheckerboard(device, physicalDevice, commandPool, graphicsQueue);
    } else {
        std::vector<uint8_t> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
        std::string errorMessage;

        if (!loadCookedTexture(cacheKey, pixels, width, height, errorMessage)) {
            throw std::runtime_error(errorMessage);
        }

        texture->createFromPixels(
            device,
            physicalDevice,
            commandPool,
            graphicsQueue,
            pixels,
            width,
            height
        );
    }

    const AssetHandle handle = nextHandle++;
    textureTable.emplace(handle, ResourceEntry<Texture>{texture, cacheKey, 1});
    texturePathToHandle.emplace(cacheKey, handle);
    return handle;
}

AssetHandle ResourceManager::loadAudioClip(const std::string& path) {
    ensureInitialized();

    const std::string cacheKey = normalizeResourceKey(path);
    const auto existingHandle = audioClipPathToHandle.find(cacheKey);
    if (existingHandle != audioClipPathToHandle.end()) {
        audioClipTable.at(existingHandle->second).refCount++;
        return existingHandle->second;
    }

    auto clip = std::make_shared<AudioClip>();
    std::string audioError;
    if (!loadCookedAudio(cacheKey, *clip, audioError)) {
        throw std::runtime_error(audioError);
    }
    clip->setResourceKey(cacheKey);

    const AssetHandle handle = nextHandle++;
    audioClipTable.emplace(handle, ResourceEntry<AudioClip>{clip, cacheKey, 1});
    audioClipPathToHandle.emplace(cacheKey, handle);
    return handle;
}

bool ResourceManager::reloadAsset(const std::string& path) {
    ensureInitialized();

    const std::string cacheKey = normalizeResourceKey(path);
    bool reloaded = false;

    // The source changed, so every artifact cooked from it is stale. Dropping the
    // cooker's memo is what makes the reload below cook the new bytes instead of
    // handing back the artifact this process already verified.
    AssetCooker::invalidate(cacheKey);

    const auto meshHandleIt = meshPathToHandle.find(cacheKey);
    if (meshHandleIt != meshPathToHandle.end()) {
        auto mesh = std::make_shared<Mesh>();
        retryTransientLoad([&]() {
            loadCookedMesh(cacheKey, *mesh);
        });
        mesh->setResourceKey(cacheKey);
        mesh->upload(device, physicalDevice, commandPool, graphicsQueue);

        auto& entry = meshTable.at(meshHandleIt->second);
        if (entry.resource) {
            entry.resource->destroy(device);
        }
        entry.resource = std::move(mesh);
        entry.resourceKey = cacheKey;
        reloaded = true;
    }

    const auto textureHandleIt = texturePathToHandle.find(cacheKey);
    if (textureHandleIt != texturePathToHandle.end()) {
        auto texture = std::make_shared<Texture>();
        texture->setResourceKey(cacheKey);

        if (cacheKey == CheckerboardTextureId) {
            texture->createCheckerboard(device, physicalDevice, commandPool, graphicsQueue);
        } else {
            std::vector<uint8_t> pixels;
            uint32_t width = 0;
            uint32_t height = 0;
            std::string errorMessage;

            retryTransientLoad([&]() {
                pixels.clear();
                width = 0;
                height = 0;
                errorMessage.clear();

                if (!loadCookedTexture(cacheKey, pixels, width, height, errorMessage)) {
                    throw std::runtime_error(errorMessage);
                }
            });

            texture->createFromPixels(
                device,
                physicalDevice,
                commandPool,
                graphicsQueue,
                pixels,
                width,
                height
            );
        }

        auto& entry = textureTable.at(textureHandleIt->second);
        if (entry.resource) {
            entry.resource->destroy(device);
        }
        entry.resource = std::move(texture);
        entry.resourceKey = cacheKey;
        reloaded = true;
    }

    const auto audioHandleIt = audioClipPathToHandle.find(cacheKey);
    if (audioHandleIt != audioClipPathToHandle.end()) {
        auto clip = std::make_shared<AudioClip>();
        retryTransientLoad([&]() {
            std::string audioError;
            if (!loadCookedAudio(cacheKey, *clip, audioError)) {
                throw std::runtime_error(audioError);
            }
        });
        clip->setResourceKey(cacheKey);

        // Decoded PCM is plain CPU data; swapping the shared_ptr is enough.
        // Voices already playing keep their old clip alive via their own
        // shared_ptr until they finish.
        audioClipTable.at(audioHandleIt->second).resource = std::move(clip);
        audioClipTable.at(audioHandleIt->second).resourceKey = cacheKey;
        reloaded = true;
    }

    return reloaded;
}

std::shared_ptr<Mesh> ResourceManager::getMesh(AssetHandle handle) {
    ensureInitialized();

    const auto it = meshTable.find(handle);
    return it == meshTable.end() ? std::shared_ptr<Mesh>{} : it->second.resource;
}

std::shared_ptr<Texture> ResourceManager::getTexture(AssetHandle handle) {
    ensureInitialized();

    const auto it = textureTable.find(handle);
    return it == textureTable.end() ? std::shared_ptr<Texture>{} : it->second.resource;
}

std::shared_ptr<AudioClip> ResourceManager::getAudioClip(AssetHandle handle) {
    ensureInitialized();

    const auto it = audioClipTable.find(handle);
    return it == audioClipTable.end() ? std::shared_ptr<AudioClip>{} : it->second.resource;
}

void ResourceManager::release(AssetHandle handle) {
    if (handle == INVALID_HANDLE) {
        return;
    }

    const auto meshIt = meshTable.find(handle);
    if (meshIt != meshTable.end()) {
        if (meshIt->second.refCount > 0) {
            meshIt->second.refCount--;
        }

        if (meshIt->second.refCount == 0) {
            if (meshIt->second.resource) {
                meshIt->second.resource->destroy(device);
            }
            meshPathToHandle.erase(meshIt->second.resourceKey);
            meshTable.erase(meshIt);
        }
        return;
    }

    const auto textureIt = textureTable.find(handle);
    if (textureIt != textureTable.end()) {
        if (textureIt->second.refCount > 0) {
            textureIt->second.refCount--;
        }

        if (textureIt->second.refCount == 0) {
            if (textureIt->second.resource) {
                textureIt->second.resource->destroy(device);
            }
            texturePathToHandle.erase(textureIt->second.resourceKey);
            textureTable.erase(textureIt);
        }
        return;
    }

    const auto audioIt = audioClipTable.find(handle);
    if (audioIt != audioClipTable.end()) {
        if (audioIt->second.refCount > 0) {
            audioIt->second.refCount--;
        }

        // AudioClip is CPU-only: dropping the shared_ptr frees the PCM. A voice
        // still playing this clip keeps its own shared_ptr, so audio doesn't cut
        // out mid-playback when an entity is destroyed.
        if (audioIt->second.refCount == 0) {
            audioClipPathToHandle.erase(audioIt->second.resourceKey);
            audioClipTable.erase(audioIt);
        }
    }
}

bool ResourceManager::isInitialized() {
    return initialized;
}

bool ResourceManager::isValid(AssetHandle handle) {
    return meshTable.find(handle) != meshTable.end() ||
           textureTable.find(handle) != textureTable.end() ||
           audioClipTable.find(handle) != audioClipTable.end();
}

void ResourceManager::shutdown() {
    if (!initialized) {
        return;
    }

    for (auto& [handle, entry] : meshTable) {
        (void)handle;
        if (entry.resource) {
            entry.resource->destroy(device);
        }
    }

    for (auto& [handle, entry] : textureTable) {
        (void)handle;
        if (entry.resource) {
            entry.resource->destroy(device);
        }
    }

    meshTable.clear();
    textureTable.clear();
    audioClipTable.clear(); // CPU data; no GPU destroy needed
    meshPathToHandle.clear();
    texturePathToHandle.clear();
    audioClipPathToHandle.clear();

    device = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    commandPool = VK_NULL_HANDLE;
    graphicsQueue = VK_NULL_HANDLE;
    initialized = false;
    nextHandle = 1;
}

std::string ResourceManager::normalizeResourceKey(const std::string& path) {
    // Built-ins are not files and have no path to normalize.
    if (path.rfind("builtin://", 0) == 0) {
        std::string key = path;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return key;
    }

    // One identity function for the whole engine (19A). This used to run
    // weakly_canonical, which resolved symlinks and so made a resource key depend on
    // machine-local filesystem layout -- the same objection that rejected a GUID
    // database. AssetPath anchors absolute paths at "assets/" without touching the
    // filesystem, so nothing is lost.
    const std::string key = AssetPath::normalize(path);
    return key.empty() ? path : key;
}

void ResourceManager::ensureInitialized() {
    if (!initialized) {
        throw std::runtime_error("resource manager must be initialized before loading assets.");
    }
}
