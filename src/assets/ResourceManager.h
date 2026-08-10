#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include "assets/AssetHandle.h"

class Mesh;
class Texture;
class AudioClip;

template<typename T>
struct ResourceEntry {
    std::shared_ptr<T> resource;
    std::string resourceKey;
    uint32_t refCount = 0;
};

class ResourceManager {
public:
    static constexpr const char* CheckerboardTextureId = "builtin://checkerboard";
    // A 1x1 opaque-white texture. Pair with Material::baseColor for a flat solid
    // colour (albedo * tint = tint), the readable option for 2D games where every
    // entity would otherwise share the checkerboard.
    static constexpr const char* WhiteTextureId = "builtin://white";
    // A procedural unit cube. The engine's guaranteed mesh fallback: a scene that
    // references a missing/uncookable mesh loads this instead of failing, and it
    // ships in no file, so it can never itself be the missing asset.
    static constexpr const char* CubeMeshId = "builtin://cube";

    static void init(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue
    );

    // True once init() has supplied a Vulkan upload context. Public so callers can
    // *ask* instead of discovering it by catching an exception — a headless run
    // (self-tests, CI, a future asset-cooking tool) is a legitimate state, not an
    // error, and code that can degrade should be able to see it coming (Rule 13).
    static bool isInitialized();

    static AssetHandle loadMesh(const std::string& path);
    static AssetHandle loadTexture(const std::string& path);
    static AssetHandle loadAudioClip(const std::string& path);
    static bool reloadAsset(const std::string& path);
    static std::shared_ptr<Mesh> getMesh(AssetHandle handle);
    static std::shared_ptr<Texture> getTexture(AssetHandle handle);
    static std::shared_ptr<AudioClip> getAudioClip(AssetHandle handle);
    static void release(AssetHandle handle);
    static bool isValid(AssetHandle handle);

    static void shutdown();

private:
    static std::string normalizeResourceKey(const std::string& path);
    static void ensureInitialized();

    static VkDevice device;
    static VkPhysicalDevice physicalDevice;
    static VkCommandPool commandPool;
    static VkQueue graphicsQueue;
    static bool initialized;
    static AssetHandle nextHandle;

    static std::unordered_map<AssetHandle, ResourceEntry<Mesh>> meshTable;
    static std::unordered_map<AssetHandle, ResourceEntry<Texture>> textureTable;
    static std::unordered_map<AssetHandle, ResourceEntry<AudioClip>> audioClipTable;
    static std::unordered_map<std::string, AssetHandle> meshPathToHandle;
    static std::unordered_map<std::string, AssetHandle> texturePathToHandle;
    static std::unordered_map<std::string, AssetHandle> audioClipPathToHandle;
};
