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

private:
    SuGarApp* app;
    Renderer* renderer;
    
    // Owned by BasicTrianglePass
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    
    // Uniform buffer
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uniformBufferMemory = VK_NULL_HANDLE;

    // Camera state owned by this pass and uploaded through the scene uniform buffer.
    Camera camera{};
    const DrawList* drawList = nullptr;

    void createRenderPass();
    void createShadowRenderPass();
    void createGraphicsPipeline();
    void createShadowPipeline();
    void createUniformBuffer();
    void updateUniformBuffer();
    void renderShadowPass(VkCommandBuffer cmd, uint32_t imageIndex);
    void renderScenePass(VkCommandBuffer cmd, uint32_t imageIndex);
};
