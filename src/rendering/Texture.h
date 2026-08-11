#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class Texture {
public:
    Texture() = default;
    ~Texture() = default;

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void createCheckerboard(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue
    );

    // `pointFilter` selects VK_FILTER_NEAREST for magnification and minification — the
    // crisp sampling pixel art and texture atlases need. It comes from the asset's
    // import setting (AssetSettings::TextureFilter), carried in the cooked artifact.
    void createFromPixels(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        const std::vector<uint8_t>& pixels,
        uint32_t width,
        uint32_t height,
        bool pointFilter = false
    );

    void destroy(VkDevice device);

    VkImageView getImageView() const { return imageView; }
    VkSampler getSampler() const { return sampler; }
    bool isReady() const { return imageView != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE; }
    bool isUploaded() const { return uploaded; }
    void setResourceKey(std::string key) { resourceKey = std::move(key); }
    const std::string& getResourceKey() const { return resourceKey; }

private:
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string resourceKey;
    bool uploaded = false;
};
