#include "assets/AssetGateway.h"

#include <utility>

namespace {
// A single Core-owned instance shared across the exe, Core, and the game DLL (defined
// here in a .cpp for the same reason as BehaviorRegistry's table). `g_installed`
// records whether a real backend is wired, so available() can be answered without
// probing the std::functions.
AssetGateway::Backend g_backend;
bool g_installed = false;
} // namespace

namespace AssetGateway {

void install(Backend backend) {
    g_backend = std::move(backend);
    // A usable backend must at least resolve meshes; treat a missing acquirer as "clear".
    g_installed = static_cast<bool>(g_backend.acquireMesh);
}

bool available() {
    return g_installed;
}

AssetHandle acquireMesh(const std::string& key) {
    return g_backend.acquireMesh ? g_backend.acquireMesh(key) : INVALID_HANDLE;
}

AssetHandle acquireTexture(const std::string& key) {
    return g_backend.acquireTexture ? g_backend.acquireTexture(key) : INVALID_HANDLE;
}

AssetHandle acquireAudioClip(const std::string& key) {
    return g_backend.acquireAudioClip ? g_backend.acquireAudioClip(key) : INVALID_HANDLE;
}

void release(AssetHandle handle) {
    if (g_backend.release) {
        g_backend.release(handle);
    }
}

} // namespace AssetGateway
