#pragma once

#include <functional>
#include <string>

#include "assets/AssetHandle.h"
#include "assets/RuntimeMeshData.h"

// Core-safe asset-acquire seam (M4 L3). A game module links only Core and cannot see
// the engine-side ResourceManager (it owns Vulkan), so before this a behavior could
// not turn an asset *key* into a renderable *handle* at runtime — it could only reuse
// a handle already resolved on a loaded entity, which double-referenced the asset
// without increfing it (a refcount-underflow bug waiting to happen).
//
// This mirrors the existing dependency-inversion RELEASE hook (Registry::onReleaseAsset):
// the engine installs a backend of std::function callbacks at startup; game/behavior
// code ACQUIRES assets by key through this façade and gets back an opaque AssetHandle
// (already a Core type). Only std::string and AssetHandle cross the boundary — no
// Vulkan, no engine internals, leak into Core or the game DLL.
//
// Ownership is unchanged: ResourceManager stays the single owner + refcounter. Each
// acquire* increfs (loadMesh/loadTexture/loadAudioClip dedup by key and bump the
// refcount); an entity holding the handle is decrefed on destroy through the existing
// onReleaseAsset path, so acquire and destroy balance with no second owner and no
// handle cloning. Serialization is unchanged: an entity references its asset by key
// (the Mesh/Texture carries its resource key), so a gameplay-spawned entity round-trips
// exactly like an authored one.
namespace AssetGateway {

// The engine's resolver, installed once at startup. Each acquirer resolves a key to a
// handle with an incref; release drops one reference.
struct Backend {
    std::function<AssetHandle(const std::string& key)> acquireMesh;
    std::function<AssetHandle(const std::string& key)> acquireTexture;
    std::function<AssetHandle(const std::string& key)> acquireAudioClip;
    // Create + upload a mesh from game-generated vertices (a *derived*, non-source GPU
    // resource — docs/DESIGN_RUNTIME_MESH.md). The engine copies the data and returns an
    // increfed handle; the caller keeps ownership of `data`. Not for source assets.
    std::function<AssetHandle(const RuntimeMeshData& data)> createMesh;
    std::function<void(AssetHandle handle)> release;
};

// Installed by the engine after ResourceManager::init. Passing a default-constructed
// Backend (or one missing acquireMesh) clears it — the headless/self-test state.
// Defined in a Core .cpp so the exe, Core, and the game DLL share exactly one instance
// (the same reason BehaviorRegistry's table lives in a .cpp).
void install(Backend backend);

// True when a device-backed backend is installed. Headless runs and tests are false,
// so game code must degrade (e.g. spawn without a mesh) rather than assume a handle.
bool available();

// Acquire an asset by key, increfing it. Returns INVALID_HANDLE when no backend is
// installed or the key cannot be resolved (the backend swallows resolve errors).
AssetHandle acquireMesh(const std::string& key);
AssetHandle acquireTexture(const std::string& key);
AssetHandle acquireAudioClip(const std::string& key);

// Create a derived GPU mesh from CPU vertex data. Returns an increfed handle, or
// INVALID_HANDLE when no backend is installed or the data is rejected (engine-side
// validation: lengths, index range, caps). The caller retains ownership of `data`.
AssetHandle createMesh(const RuntimeMeshData& data);

// Drops one reference. Only needed to balance an acquire whose handle was NOT handed
// to an entity — an entity's handles are released automatically on destroy.
void release(AssetHandle handle);

} // namespace AssetGateway
