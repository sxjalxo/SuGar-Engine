#pragma once

#include "RenderPass.h"
#include "rendering/Camera.h"
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
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    // Skinned variants (Phase 17C.2). Same layout, same fragment stage, same push
    // constants — they differ only in the vertex shader and in declaring the two
    // extra vertex attributes, so a skinned mesh is lit and shadowed by identical
    // code. The shadow variant exists because otherwise an animated character casts
    // its bind-pose shadow.
    VkPipeline skinnedPipeline = VK_NULL_HANDLE;
    VkPipeline skinnedShadowPipeline = VK_NULL_HANDLE;

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

    // Camera state owned by this pass and uploaded through the scene uniform buffer.
    Camera camera{};
    const DrawList* drawList = nullptr;

    void createRenderPass();
    void createShadowRenderPass();
    void createGraphicsPipeline();
    void createShadowPipeline();
    void createUniformBuffer();
    void createJointResources();
    void updateUniformBuffer();
    // Packs this frame's joint matrices into jointBuffer and fills jointOffsets.
    void uploadJointMatrices();
    void renderShadowPass(VkCommandBuffer cmd, uint32_t imageIndex);
    void renderScenePass(VkCommandBuffer cmd, uint32_t imageIndex);
};
