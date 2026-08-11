#pragma once

#include "RenderPass.h"
#include "rendering/Camera.h"
#include "rendering/Material.h"
#include "scene/DrawList.h"

class SuGarApp;
class Renderer;

class BasicTrianglePass : public RenderPass {
public:
    BasicTrianglePass(SuGarApp* app, Renderer* renderer);
    ~BasicTrianglePass() override;

    void setup() override;
    void execute(VkCommandBuffer cmd, uint32_t imageIndex) override;
    void setDrawList(const DrawList* newDrawList) { drawList = newDrawList; }
    void moveCameraForward(float deltaTime);
    void moveCameraBackward(float deltaTime);
    void moveCameraLeft(float deltaTime);
    void moveCameraRight(float deltaTime);
    void rotateCamera(float xOffset, float yOffset);
    void setCameraMode(CameraMode mode) { camera.mode = mode; }
    // Exposed so the editor can build picking rays / gizmo matrices from the same
    // camera the scene is rendered with.
    Camera& getCamera() { return camera; }
    void setOrbitTarget(const glm::vec3& target) { camera.target = target; }
    void setFollowTargetPosition(const glm::vec3& position) {
        camera.followTargetPosition = position;
        camera.hasFollowTarget = true;
    }
    
    // Getters for command buffer recording
    VkRenderPass getRenderPass() const { return renderPass; }
    VkRenderPass getShadowRenderPass() const { return shadowRenderPass; }
    // Draw calls actually submitted last frame (shadow + scene). Reported rather than
    // inferred from the item count: with instancing the two stopped being the same
    // number, and an editor stat that silently means something else is worse than none.
    int getSubmittedDrawCalls() const { return submittedDrawCalls; }

    VkBuffer getUniformBuffer() const { return uniformBuffer; }
    // The descriptor's range is one slice; the dynamic offset selects which frame's.
    VkDeviceSize getUniformSliceSize() const { return uniformSliceSize; }

private:
    SuGarApp* app;
    Renderer* renderer;
    
    // Owned by BasicTrianglePass
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    // Skinned shadow variant (Phase 17C.2): otherwise an animated character casts its
    // bind-pose shadow.
    VkPipeline skinnedShadowPipeline = VK_NULL_HANDLE;

    // Scene pipelines indexed [skinned][blend bucket]. Blend buckets (BlendMode →
    // GPU state, Rule 22 seam): 0 = Opaque/Masked (no blend, depth write), 1 =
    // Translucent (alpha blend, no depth write), 2 = Additive. Opaque and Masked share
    // bucket 0 — Masked differs only by a shader `discard`. Skinned shares the fragment
    // stage, so it gets the identical set of blend states.
    static constexpr int SceneBucketCount = 3;
    VkPipeline scenePipelines[2][SceneBucketCount] = {};

    // BlendMode → scene pipeline bucket. Opaque and Masked both use bucket 0.
    static int blendBucket(BlendMode mode) {
        switch (mode) {
            case BlendMode::Translucent: return 1;
            case BlendMode::Additive:    return 2;
            default:                     return 0; // Opaque, Masked
        }
    }

    // Scene uniform buffer: one slice per frame in flight, written at the current
    // frame's offset and bound with a dynamic offset. A single buffer rewritten each
    // frame races the GPU still reading the previous frame's copy — the fence only
    // guarantees the frame *two* submissions ago has finished. Same lifetime model as
    // the joint buffer below.
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uniformBufferMemory = VK_NULL_HANDLE;
    void* uniformBufferMapped = nullptr;
    VkDeviceSize uniformSliceSize = 0; // sizeof(UniformBufferObject), padded to alignment

    // Joint matrices for this frame's skinned draws: one dynamic-UBO slice per
    // skinned item, per frame in flight. The pass *transports* poses to the GPU; it
    // never computes or owns one (they arrive on the DrawList, derived from ECS).
    VkDescriptorSetLayout jointSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool jointDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet jointDescriptorSet = VK_NULL_HANDLE;
    VkBuffer jointBuffer = VK_NULL_HANDLE;
    VkDeviceMemory jointBufferMemory = VK_NULL_HANDLE;
    void* jointBufferMapped = nullptr;
    VkDeviceSize jointSliceSize = 0; // one skin's matrices, padded to UBO alignment
    // Byte offset into jointBuffer per drawList item index; 0xFFFFFFFF = unskinned.
    std::vector<uint32_t> jointOffsets;

    // Instanced draws (M4 L3). Consecutive draw-list items that agree on mesh, texture,
    // material and blend mode — and are unskinned — collapse into ONE draw call, with the
    // model matrix and base colour arriving per instance from this buffer. A pooled
    // particle system is the case that forced it: 4 000 pooled particles were 4 000 draw
    // calls, and the frame rate fell 411 -> 173 FPS.
    //
    // Same lifetime model as the uniform and joint buffers: one slice per frame in flight,
    // because a single buffer rewritten each frame races the GPU still reading last frame's.
    VkPipeline instancedPipelines[SceneBucketCount] = {};
    VkPipeline shadowInstancedPipeline = VK_NULL_HANDLE;
    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory instanceBufferMemory = VK_NULL_HANDLE;
    void* instanceBufferMapped = nullptr;
    VkDeviceSize instanceSliceSize = 0;
    static constexpr uint32_t MaxInstancesPerFrame = 16384;

    // One entry per run of draw-list items collapsed into a single draw. `count == 1`
    // means "not batched" and takes the ordinary push-constant path, so nothing about the
    // existing renderer changes for scenes that do not repeat a mesh.
    struct InstanceBatch {
        size_t firstItem = 0;
        uint32_t count = 0;
        uint32_t firstInstance = 0;
    };
    std::vector<InstanceBatch> instanceBatches;
    int submittedDrawCalls = 0;

    // Camera state owned by this pass and uploaded through the scene uniform buffer.
    Camera camera{};
    const DrawList* drawList = nullptr;

    void createRenderPass();
    void createShadowRenderPass();
    void createGraphicsPipeline();
    void createShadowPipeline();
    void createUniformBuffer();
    void createJointResources();
    void createInstanceBuffer();
    // Groups this frame's draw list into instanced batches and uploads their per-instance
    // data. Derived per frame from the draw list — no state survives the frame.
    void buildInstanceBatches();
    void updateUniformBuffer();
    // Packs this frame's joint matrices into jointBuffer and fills jointOffsets.
    void uploadJointMatrices();
    void renderShadowPass(VkCommandBuffer cmd, uint32_t imageIndex);
    void renderScenePass(VkCommandBuffer cmd, uint32_t imageIndex);
};
