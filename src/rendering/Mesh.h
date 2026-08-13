#pragma once

#include "rendering/DeviceMemoryPool.h"
#include "rendering/Vertex.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class Mesh {
public:
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // The buffers are the mesh's; the memory behind them is a placement in a pooled block
    // (DeviceMemoryPool), handed back on destroy. The mesh stays the owner of what it
    // created — the pool only owns blocks and bookkeeping.
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    DeviceMemoryPool::Allocation vertexMemory;

    VkBuffer indexBuffer = VK_NULL_HANDLE;
    DeviceMemoryPool::Allocation indexMemory;

    void upload(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue
    );
    void destroy(VkDevice device);

    // A procedural unit cube (side 1, centered on origin, per-face normals + UVs).
    // The engine's built-in mesh: a guaranteed, file-free fallback so a scene that
    // references a missing mesh still loads with *something* visible instead of
    // failing the whole load. Pure CPU data — call upload() before rendering.
    static Mesh makeUnitCube();

    // Frees the upload machinery shared by every mesh (the reused staging buffer). Call
    // once, after the last mesh is destroyed and before the device goes away —
    // ResourceManager::shutdown does. Safe to call twice.
    static void shutdownUploadResources(VkDevice device);

    void setResourceKey(std::string key) { resourceKey = std::move(key); }
    const std::string& getResourceKey() const { return resourceKey; }
    bool isUploaded() const { return uploaded; }

private:
    std::string resourceKey;
    bool uploaded = false;
};
