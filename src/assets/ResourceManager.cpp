#include "assets/ResourceManager.h"
#include "assets/GltfLoader.h"
#include "assets/ModelLoader.h"
#include "audio/AudioClip.h"
#include "audio/AudioLoader.h"
#include "rendering/Mesh.h"
#include "rendering/Texture.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <vector>

#include "stb_image.h"

namespace {
constexpr int ReloadRetryCount = 10;
constexpr auto ReloadRetryDelay = std::chrono::milliseconds(50);

// Picks the geometry loader by file extension. glTF parsing is fully isolated in
// GltfLoader; everything else falls back to the OBJ loader. A "<path>#<index>"
// key selects a single glTF mesh by index (used by the model->ECS importer so
// each node references its own sub-mesh and prefabs round-trip).
void loadMeshGeometry(const std::string& key, Mesh& mesh) {
    std::string filePath = key;
    int meshIndex = -1;

    const size_t hash = key.find('#');
    if (hash != std::string::npos) {
        filePath = key.substr(0, hash);
        try {
            meshIndex = std::stoi(key.substr(hash + 1));
        } catch (...) {
            meshIndex = -1;
        }
    }

    std::string extension = std::filesystem::path(filePath).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (extension == ".gltf" || extension == ".glb") {
        if (meshIndex >= 0) {
            GltfLoader::loadGltfMesh(filePath, meshIndex, mesh);
        } else {
            GltfLoader::loadGltf(filePath, mesh);
        }
    } else {
        ModelLoader::loadObj(filePath, mesh);
    }
}

std::string sanitizeResourceKey(std::string key) {
    std::replace(key.begin(), key.end(), '\\', '/');
    while (key.rfind("./", 0) == 0) {
        key.erase(0, 2);
    }

    const size_t assetsRoot = key.find("assets/");
    if (assetsRoot != std::string::npos) {
        key = key.substr(assetsRoot);
    }

    std::transform(
        key.begin(),
        key.end(),
        key.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return key;
}

// Decodes an image file to tightly-packed RGBA8 pixels via stb_image.
// Cross-platform (replaces the previous Windows-only WIC path).
bool decodeTextureFile(
    const std::string& path,
    std::vector<uint8_t>& pixels,
    uint32_t& width,
    uint32_t& height,
    std::string& errorMessage
) {
    int decodedWidth = 0;
    int decodedHeight = 0;
    int sourceChannels = 0;

    // Force 4 channels (RGBA8) so the upload path matches VK_FORMAT_R8G8B8A8_SRGB.
    stbi_uc* data = stbi_load(path.c_str(), &decodedWidth, &decodedHeight, &sourceChannels, STBI_rgb_alpha);
    if (data == nullptr) {
        const char* reason = stbi_failure_reason();
        errorMessage = "failed to load texture file: " + path +
                       (reason != nullptr ? std::string(" (") + reason + ")" : std::string());
        return false;
    }

    if (decodedWidth <= 0 || decodedHeight <= 0) {
        stbi_image_free(data);
        errorMessage = "texture file has an invalid size: " + path;
        return false;
    }

    const size_t pixelBytes = static_cast<size_t>(decodedWidth) * static_cast<size_t>(decodedHeight) * 4;
    pixels.resize(pixelBytes);
    std::memcpy(pixels.data(), data, pixelBytes);
    stbi_image_free(data);

    width = static_cast<uint32_t>(decodedWidth);
    height = static_cast<uint32_t>(decodedHeight);
    return true;
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
    loadMeshGeometry(cacheKey, *mesh);
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

        if (!decodeTextureFile(cacheKey, pixels, width, height, errorMessage)) {
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
    if (!AudioLoader::loadClip(cacheKey, *clip)) {
        throw std::runtime_error("failed to load audio clip: " + cacheKey);
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

    const auto meshHandleIt = meshPathToHandle.find(cacheKey);
    if (meshHandleIt != meshPathToHandle.end()) {
        auto mesh = std::make_shared<Mesh>();
        retryTransientLoad([&]() {
            loadMeshGeometry(path, *mesh);
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

                if (!decodeTextureFile(path, pixels, width, height, errorMessage)) {
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
            if (!AudioLoader::loadClip(cacheKey, *clip)) {
                throw std::runtime_error("failed to reload audio clip: " + cacheKey);
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
    if (path.rfind("builtin://", 0) == 0) {
        return sanitizeResourceKey(path);
    }

    const std::filesystem::path resourcePath(path);
    std::error_code errorCode;
    const std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(resourcePath, errorCode);

    if (!errorCode && !normalizedPath.empty()) {
        return sanitizeResourceKey(normalizedPath.string());
    }

    return sanitizeResourceKey(path);
}

void ResourceManager::ensureInitialized() {
    if (!initialized) {
        throw std::runtime_error("resource manager must be initialized before loading assets.");
    }
}
