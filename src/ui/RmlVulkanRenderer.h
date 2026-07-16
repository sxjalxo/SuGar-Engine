#pragma once

#include <RmlUi/Core/RenderInterface.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

// Complete type required: TextureEntry holds unique_ptr<Texture>, so the implicit
// destructor needs to see it.
#include "rendering/Texture.h"

// SuGar's RmlUi RenderInterface (Phase 16B.2): translates RmlUi's geometry and
// textures into draw calls on *our* Vulkan renderer. Deliberately not RmlUi's
// reference RmlUi_Renderer_VK backend — that one creates and owns its own Vulkan
// device/swapchain, so it can't compose with an existing renderer.
//
// Lifetime: init() once the device + UI render pass exist; beginFrame() each frame
// with the recording command buffer, then Rml::Context::Render() drives the
// callbacks below; shutdown() before device teardown. Engine layer only — RmlUi
// never reaches SuGarCore (RULES.md Rule 15).
class RmlVulkanRenderer : public Rml::RenderInterface {
public:
    void init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool,
              VkQueue graphicsQueue, VkRenderPass renderPass);
    void shutdown();
    bool isReady() const { return pipeline != VK_NULL_HANDLE; }

    // Binds the command buffer RmlUi's draw callbacks will record into this frame.
    void beginFrame(VkCommandBuffer commandBuffer, VkExtent2D extent);

    // --- Rml::RenderInterface ---------------------------------------------------
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

private:
    struct Geometry {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };

    struct TextureEntry {
        std::unique_ptr<Texture> texture;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    void createPipeline(VkRenderPass renderPass);
    void createDescriptorResources();
    void createWhiteTexture();
    VkDescriptorSet createDescriptorSet(const Texture& texture);
    // Host-visible buffer; UI geometry is small and RmlUi caches it across frames.
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory);
    Rml::TextureHandle registerTexture(std::unique_ptr<Texture> texture);

    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    // RmlUi releases geometry mid-frame (a re-layout drops old buffers) while those
    // buffers may still be referenced by command buffers in flight. Destroying them
    // immediately trips "vkDestroyBuffer(): can't be called on VkBuffer ... currently
    // in use". So retire them here and free once enough frames have passed.
    struct RetiredGeometry {
        Geometry geometry;
        uint64_t retiredAtFrame = 0;
    };
    void destroyGeometry(Geometry& geometry);
    void collectRetiredGeometry(bool force);

    std::unordered_map<uintptr_t, Geometry> geometries;
    std::vector<RetiredGeometry> retiredGeometries;
    uint64_t frameCounter = 0;
    // Swapchain uses 2 frames in flight; 3 gives a safety margin.
    static constexpr uint64_t FramesInFlightMargin = 3;
    std::unordered_map<uintptr_t, TextureEntry> textures;
    uintptr_t nextGeometryHandle = 1;
    uintptr_t nextTextureHandle = 1;

    // The 1x1 white texture bound for untextured geometry (RmlUi passes texture=0).
    Rml::TextureHandle whiteTextureHandle = 0;

    VkCommandBuffer currentCommandBuffer = VK_NULL_HANDLE;
    VkExtent2D currentExtent{0, 0};
};
