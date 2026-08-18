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

    // Binds the command buffer RmlUi's draw callbacks record into this frame, and opens
    // the UI layer-0 render pass on the scene image (framebuffer + layerPass). endFrame
    // closes whatever pass is open. Between them the compositor may open/close offscreen
    // layer passes for effects (box-shadow/blur).
    void beginFrame(VkCommandBuffer commandBuffer, VkExtent2D extent, VkFramebuffer framebuffer,
                    VkRenderPass layerPass, VkImage sceneImage);
    void endFrame();

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

    // --- Clip masks (DevDocs/DESIGN_UI_CLIP_MASK.md) --------------------------------
    // RmlUi draws an outer box-shadow as "mask to everything OUTSIDE the element, then
    // draw the shadow". With no mask the shadow covers the element too -- invisible
    // under an opaque background, and measured at -53/-62 RGB through a translucent
    // one, which is what forced this.
    //
    // The mask is a coverage TEXTURE, not a stencil buffer: the UI pass writes into an
    // image whose depth attachment is D32_SFLOAT (no stencil aspect) and offscreen
    // effect layers carry no depth at all, so stencil would have meant changing the
    // engine's depth format for the scene pass. Everything here stays inside src/ui/.
    void EnableClipMask(bool enable) override;
    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                          Rml::Vector2f translation) override;

    // --- Layer compositor (RmlUi effects: box-shadow / blur) ---------------------
    // The seam: RmlUi renders effects by pushing offscreen layers, drawing into them,
    // compositing them back (optionally through a filter), and saving one as a texture.
    // We honour that with offscreen colour targets + a fullscreen composite/blur pass.
    Rml::LayerHandle PushLayer() override;
    void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination,
                         Rml::BlendMode blendMode,
                         Rml::Span<const Rml::CompiledFilterHandle> filters) override;
    void PopLayer() override;
    Rml::TextureHandle SaveLayerAsTexture() override;
    Rml::CompiledFilterHandle CompileFilter(const Rml::String& name,
                                            const Rml::Dictionary& parameters) override;
    void ReleaseFilter(Rml::CompiledFilterHandle filter) override;

private:
    struct Geometry {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };

    struct TextureEntry {
        std::unique_ptr<Texture> texture;   // font atlases / loaded images (owns its image)
        VkImage rawImage = VK_NULL_HANDLE;  // compositor-saved layer copies (raw, owned here)
        VkDeviceMemory rawMemory = VK_NULL_HANDLE;
        VkImageView rawView = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    void createPipeline(VkRenderPass renderPass);
    void createOffscreenResources(); // offscreen pass + UI-into-offscreen + composite pipelines
    void createDescriptorResources();
    void createWhiteTexture();

    // One offscreen colour target (an RmlUi layer). Colour only -- no depth/stencil,
    // since clip masking is not implemented. Sampled by the composite pass and, once
    // saved, by RenderGeometry (as the box-shadow texture).
    struct LayerTarget {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet sampleSet = VK_NULL_HANDLE; // samples this target's colour
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkExtent2D extent{0, 0};
        bool inUse = false;
    };
    // --- clip mask ---------------------------------------------------------------
    // Two R8 targets that ping-pong: Intersect must read the previous coverage while
    // writing the new one, and sampling the attachment being written is a feedback loop.
    struct MaskTarget {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet sampleSet = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };
    bool ensureMaskTargets(VkExtent2D extent);
    void destroyMaskTargets();
    // Binds set 1 for UI draws: the active mask, or a 1x1 white texture when nothing is
    // masked -- which is what makes the multiply in rml.frag inert by default.
    VkDescriptorSet activeMaskSet() const;
    void resetClipMask();                     // called each frame; RmlUi re-declares masks

    int acquireLayer();                       // index into layerPool; grows the pool as needed
    void beginLayerPass(int layerIndex, bool clear);
    void endActivePass();
    void ensureActivePass();                  // (re)open the pass for the current target
    void transitionLayerToSampled(int layerIndex);
    void destroyLayerTargets();
    float filters_lookup(uintptr_t handle) const; // sigma of a blur filter, or -1
    VkDescriptorSet createDescriptorSet(const Texture& texture);
    VkDescriptorSet createDescriptorSetForView(VkImageView view, VkSampler sampler);
    bool createColorTarget(VkExtent2D extent, VkImage& image, VkDeviceMemory& memory, VkImageView& view);
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

    // Textures have exactly the same problem, and it was found by the validation layer
    // rather than reasoned about: RmlUi drops a cached box-shadow/layer texture whenever
    // the HUD re-lays-out — every frame for a HUD whose text changes — while in-flight
    // command buffers still reference its descriptor set:
    //   "vkFreeDescriptorSets(): pDescriptorSets[0] can't be called on VkDescriptorSet
    //    ... that is currently in use by VkCommandBuffer ..."
    // Same retire-and-collect answer as geometry, so there is one policy, not two.
    struct RetiredTexture {
        TextureEntry entry;
        uint64_t retiredAtFrame = 0;
    };
    void destroyTexture(TextureEntry& entry);
    void collectRetiredTextures(bool force);

    std::unordered_map<uintptr_t, Geometry> geometries;
    std::vector<RetiredGeometry> retiredGeometries;
    std::vector<RetiredTexture> retiredTextures;
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
    // The UI layer-0 pass (scene image). Stage 1: the only pass. Offscreen effect layers
    // (Stage 2+) will push their own passes on top of this.
    VkRenderPass layer0Pass = VK_NULL_HANDLE;
    VkFramebuffer layer0Framebuffer = VK_NULL_HANDLE;
    VkImage sceneImage = VK_NULL_HANDLE; // layer 0's colour image; barriered to SHADER_READ in endFrame

    // Compositor state. The "current target" is either layer 0 (the scene image, index
    // -1) or the top of layerStack (an offscreen target). A pass is opened lazily on the
    // current target the first time geometry is drawn, and closed on any target switch.
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkRenderPass offscreenPass = VK_NULL_HANDLE;   // colour-only, for offscreen layers
    VkPipeline offscreenUiPipeline = VK_NULL_HANDLE; // UI geometry into an offscreen layer
    VkPipeline compositePipeline = VK_NULL_HANDLE;   // fullscreen copy/blur
    VkPipelineLayout compositePipelineLayout = VK_NULL_HANDLE;
    VkSampler layerSampler = VK_NULL_HANDLE;

    MaskTarget maskTargets[2];
    int currentMask = 0;          // index of the mask UI draws sample
    bool maskActive = false;      // false => set 1 binds the 1x1 white mask
    VkExtent2D maskExtent{0, 0};
    VkRenderPass maskPass = VK_NULL_HANDLE;       // colour-only R8
    VkPipeline maskPipeline = VK_NULL_HANDLE;      // write a constant coverage
    VkPipelineLayout maskPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSet whiteMaskSet = VK_NULL_HANDLE;        // the inert 1x1 mask

    std::vector<LayerTarget> layerPool;
    std::vector<int> layerStack;   // active offscreen layer indices; back() == top
    int activePassLayer = -2;      // -2 none, -1 layer0, >=0 offscreen pool index
    bool renderPassActive = false;

    struct BlurFilter { float sigma = 0.0f; };
    std::unordered_map<uintptr_t, BlurFilter> filters;
    uintptr_t nextFilterHandle = 1;

    // Last scissor RmlUi set; SaveLayerAsTexture copies exactly this region of the layer.
    VkOffset2D lastScissorOffset{0, 0};
    VkExtent2D lastScissorExtent{0, 0};
};
