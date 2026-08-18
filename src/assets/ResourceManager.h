#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>
#include "assets/AssetHandle.h"
#include "assets/RuntimeMeshData.h"

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

    // Create a DERIVED GPU mesh from game-generated vertex data (runtime-mesh seam,
    // DevDocs/DESIGN_RUNTIME_MESH.md). Validates, copies into the engine vertex format,
    // uploads, and stores under a synthetic `runtime://mesh/<id>` key (never a source
    // asset — not scanned/cooked/packaged). Returns an increfed handle, or
    // INVALID_HANDLE with `error` set. Main-thread + device only.
    static AssetHandle createRuntimeMesh(const RuntimeMeshData& data, std::string& error);
    static bool reloadAsset(const std::string& path);
    static std::shared_ptr<Mesh> getMesh(AssetHandle handle);
    static std::shared_ptr<Texture> getTexture(AssetHandle handle);
    static std::shared_ptr<AudioClip> getAudioClip(AssetHandle handle);
    static void release(AssetHandle handle);
    static bool isValid(AssetHandle handle);

    // --- GPU retirement (DevDocs/DESIGN_GPU_RETIREMENT.md) -----------------------
    // Dropping the last reference to a mesh or texture cannot destroy it on the spot:
    // the command buffers for the frames still in flight may reference it, and freeing
    // it there is a use-after-free of GPU memory (found by the L3 arena, whose thrown
    // bolts are the last holder of their mesh for well under a frame).
    //
    // So `release` retires the resource instead, and it is destroyed once every frame
    // that could still be reading it has completed. The table entry and key mapping are
    // dropped immediately, so the key reloads fresh in the same step.

    // How many frames a retired resource must survive. Set once at startup from
    // Renderer::framesInFlight(); the default covers a renderer with two.
    static void setFramesInFlight(uint32_t frames);

    // Advances the retirement queue one frame and destroys whatever has outlived every
    // frame in flight. **Called once per frame by the renderer** — a build that forgets
    // leaks GPU memory rather than corrupting it, which is the right way round.
    static void endFrame();

    // Resources waiting to be destroyed. For tests and diagnostics.
    static size_t retiredCount();

    // Live (still referenced) resources per table. Diagnostics only: a torture run
    // proves an asset lifetime is balanced by watching these stay flat across
    // thousands of create/destroy cycles, which is not something a headless test can
    // observe. Reported through SUGAR_FPSLOG beside the frame counters.
    static size_t liveMeshCount();
    static size_t liveTextureCount();
    static size_t liveAudioClipCount();

    static void shutdown();

private:
    static std::string normalizeResourceKey(const std::string& path);
    static void ensureInitialized();

    // A resource whose last reference is gone, still owned until the GPU is done with
    // it. Exactly one of the two pointers is set; `framesRemaining` counts down in
    // endFrame().
    struct RetiredResource {
        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Texture> texture;
        uint32_t framesRemaining = 0;
    };
    static void retire(RetiredResource resource);

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
    static std::vector<RetiredResource> retiredResources;
    static uint32_t framesInFlight;
};
