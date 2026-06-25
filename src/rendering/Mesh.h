#pragma once

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

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;

    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;

    void upload(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue
    );
    void destroy(VkDevice device);

    void setResourceKey(std::string key) { resourceKey = std::move(key); }
    const std::string& getResourceKey() const { return resourceKey; }
    bool isUploaded() const { return uploaded; }

private:
    std::string resourceKey;
    bool uploaded = false;
};
