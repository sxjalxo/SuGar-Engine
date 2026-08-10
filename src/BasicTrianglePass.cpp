#include "BasicTrianglePass.h"
#include "rendering/Material.h"
#include "rendering/Mesh.h"
#include "rendering/Vertex.h"
#include "rendering/UniformBufferObject.h"
#include "SuGarApp.h"
#include "Renderer.h"
#include <stdexcept>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

struct ObjectPushConstants {
    glm::mat4 model{1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    // BlendMode as a float (occupies the old padding slot at offset 76, so the push
    // constant stays 96 B): the frag reads it to decide cutout vs blend output.
    float blendMode = 0.0f;
    // vec4 keeps the 16-byte std430 alignment the shader's push_constant block uses;
    // .w is unused. Flat-colour tint (see Material::baseColor).
    glm::vec4 baseColor{1.0f};
};

// Must match MAX_JOINTS in shaders/skinned.vert and shaders/skinned_shadow.vert.
// 64 mat4 = 4 KiB, well inside the 16 KiB UBO range every Vulkan implementation
// guarantees. A skin with more joints is clamped (see uploadJointMatrices).
constexpr uint32_t MAX_JOINTS = 64;
// Skinned draws per frame that can carry a distinct pose. Beyond this, the extra
// characters render unskinned rather than reading another character's pose.
constexpr uint32_t MAX_SKINNED_DRAWS = 64;
// Marks a draw-list item as having no pose.
constexpr uint32_t NO_JOINT_OFFSET = 0xFFFFFFFFu;

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();

    return buffer;
}

static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
}

BasicTrianglePass::BasicTrianglePass(SuGarApp* app, Renderer* renderer) : app(app), renderer(renderer) {}

BasicTrianglePass::~BasicTrianglePass() {
    VkDevice device = app->getDevice();

    if (uniformBufferMapped != nullptr) {
        vkUnmapMemory(device, uniformBufferMemory);
        uniformBufferMapped = nullptr;
    }

    if (uniformBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, uniformBuffer, nullptr);
        uniformBuffer = VK_NULL_HANDLE;
    }

    if (uniformBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, uniformBufferMemory, nullptr);
        uniformBufferMemory = VK_NULL_HANDLE;
    }

    for (auto& row : scenePipelines) {
        for (VkPipeline& pipeline : row) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
        }
    }

    if (shadowPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, shadowPipeline, nullptr);
        shadowPipeline = VK_NULL_HANDLE;
    }

    if (skinnedShadowPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, skinnedShadowPipeline, nullptr);
        skinnedShadowPipeline = VK_NULL_HANDLE;
    }

    if (jointBufferMapped != nullptr) {
        vkUnmapMemory(device, jointBufferMemory);
        jointBufferMapped = nullptr;
    }

    if (jointBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, jointBuffer, nullptr);
        jointBuffer = VK_NULL_HANDLE;
    }

    if (jointBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, jointBufferMemory, nullptr);
        jointBufferMemory = VK_NULL_HANDLE;
    }

    if (jointDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, jointDescriptorPool, nullptr);
        jointDescriptorPool = VK_NULL_HANDLE;
    }

    if (jointSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, jointSetLayout, nullptr);
        jointSetLayout = VK_NULL_HANDLE;
    }

    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }

    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }

    if (shadowRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, shadowRenderPass, nullptr);
        shadowRenderPass = VK_NULL_HANDLE;
    }
}

void BasicTrianglePass::setup() {
    createShadowRenderPass();
    createRenderPass();
    // Joint resources first: the pipeline layout includes the joint set layout.
    createJointResources();
    createGraphicsPipeline();
    createShadowPipeline();
    createUniformBuffer();
}

void BasicTrianglePass::execute(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (drawList == nullptr) {
        return;
    }

    updateUniformBuffer();
    // Once per frame, not once per pass: the shadow and scene passes must skin
    // identically, so they read the same uploaded matrices.
    uploadJointMatrices();
    renderShadowPass(cmd, imageIndex);
    renderScenePass(cmd, imageIndex);
}

void BasicTrianglePass::renderShadowPass(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkRenderPassBeginInfo shadowPassInfo{};
    shadowPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    shadowPassInfo.renderPass = shadowRenderPass;
    shadowPassInfo.framebuffer = renderer->getShadowFramebuffer();
    shadowPassInfo.renderArea.offset = {0, 0};
    shadowPassInfo.renderArea.extent = renderer->getShadowExtent();

    VkClearValue clearDepth{};
    clearDepth.depthStencil = {1.0f, 0};
    shadowPassInfo.clearValueCount = 1;
    shadowPassInfo.pClearValues = &clearDepth;

    vkCmdBeginRenderPass(cmd, &shadowPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport shadowViewport{};
    shadowViewport.x = 0.0f;
    shadowViewport.y = 0.0f;
    shadowViewport.width = static_cast<float>(renderer->getShadowExtent().width);
    shadowViewport.height = static_cast<float>(renderer->getShadowExtent().height);
    shadowViewport.minDepth = 0.0f;
    shadowViewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &shadowViewport);

    VkRect2D shadowScissor{};
    shadowScissor.offset = {0, 0};
    shadowScissor.extent = renderer->getShadowExtent();
    vkCmdSetScissor(cmd, 0, 1, &shadowScissor);
    vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 1.75f);

    AssetHandle lastTexture = INVALID_HANDLE;
    AssetHandle lastMesh = INVALID_HANDLE;
    VkPipeline lastPipeline = VK_NULL_HANDLE;

    for (size_t i = 0; i < drawList->items.size(); i++) {
        const auto& item = drawList->items[i];
        if (!item.mesh || item.material.albedo == INVALID_HANDLE) {
            continue;
        }

        const uint32_t jointOffset = i < jointOffsets.size() ? jointOffsets[i] : NO_JOINT_OFFSET;
        const bool skinned = jointOffset != NO_JOINT_OFFSET;

        // Both pipelines share one layout, so switching between them leaves set 0
        // and the push constants bound — no rebinding, no invalidation.
        const VkPipeline wantedPipeline = skinned ? skinnedShadowPipeline : shadowPipeline;
        if (wantedPipeline != lastPipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wantedPipeline);
            lastPipeline = wantedPipeline;
        }

        if (skinned) {
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout,
                1,
                1,
                &jointDescriptorSet,
                1,
                &jointOffset
            );
        }

        if (item.material.albedo != lastTexture) {
            const VkDescriptorSet descriptorSet = renderer->getDescriptorSet(item.material.albedo, imageIndex);
            // Dynamic offset selects this frame's scene-UBO slice. Constant within a
            // frame; re-supplied on every set-0 bind because the whole set rebinds.
            const uint32_t sceneUboOffset = renderer->getCurrentFrame() * static_cast<uint32_t>(uniformSliceSize);
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout,
                0,
                1,
                &descriptorSet,
                1,
                &sceneUboOffset
            );
            lastTexture = item.material.albedo;
        }

        if (item.meshHandle != lastMesh) {
            VkBuffer vertexBuffers[] = {item.mesh->vertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, item.mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            lastMesh = item.meshHandle;
        }

        ObjectPushConstants pushConstants{};
        pushConstants.model = item.model;

        vkCmdPushConstants(
            cmd,
            pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(ObjectPushConstants),
            &pushConstants
        );

        vkCmdDrawIndexed(cmd, static_cast<uint32_t>(item.mesh->indices.size()), 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void BasicTrianglePass::renderScenePass(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (drawList == nullptr) {
        return;
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = renderer->getViewportFramebuffer();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = renderer->getRenderExtent();

    VkClearValue clearColor = {{{0.10f, 0.10f, 0.10f, 1.0f}}};
    VkClearValue clearDepth{};
    clearDepth.depthStencil.depth = 1.0f;
    clearDepth.depthStencil.stencil = 0;
    
    std::array<VkClearValue, 2> clearValues = {clearColor, clearDepth};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)renderer->getRenderExtent().width;
    viewport.height = (float)renderer->getRenderExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = renderer->getRenderExtent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    AssetHandle lastTexture = INVALID_HANDLE;
    AssetHandle lastMesh = INVALID_HANDLE;
    VkPipeline lastPipeline = VK_NULL_HANDLE;

    for (size_t i = 0; i < drawList->items.size(); i++) {
        const auto& item = drawList->items[i];
        if (!item.mesh || item.material.albedo == INVALID_HANDLE) {
            continue;
        }

        const uint32_t jointOffset = i < jointOffsets.size() ? jointOffsets[i] : NO_JOINT_OFFSET;
        const bool skinned = jointOffset != NO_JOINT_OFFSET;

        // Shared pipeline layout: switching keeps set 0 and the push constants. The
        // blend bucket comes from the material; the draw list is already ordered so all
        // opaque/masked precede all translucent/additive (and the blended tail is sorted
        // back-to-front), so this bind pattern stays coherent.
        const VkPipeline wantedPipeline = scenePipelines[skinned ? 1 : 0][blendBucket(item.material.blendMode)];
        if (wantedPipeline != lastPipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wantedPipeline);
            lastPipeline = wantedPipeline;
        }

        if (skinned) {
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout,
                1,
                1,
                &jointDescriptorSet,
                1,
                &jointOffset
            );
        }

        if (item.material.albedo != lastTexture) {
            VkDescriptorSet descriptorSet = renderer->getDescriptorSet(item.material.albedo, imageIndex);
            const uint32_t sceneUboOffset = renderer->getCurrentFrame() * static_cast<uint32_t>(uniformSliceSize);
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout,
                0,
                1,
                &descriptorSet,
                1,
                &sceneUboOffset
            );
            lastTexture = item.material.albedo;
        }

        if (item.meshHandle != lastMesh) {
            VkBuffer vertexBuffers[] = { item.mesh->vertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, item.mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            lastMesh = item.meshHandle;
        }

        ObjectPushConstants pushConstants{};
        pushConstants.model = item.model;
        pushConstants.metallic = item.material.metallic;
        pushConstants.roughness = item.material.roughness;
        pushConstants.ao = item.material.ao;
        pushConstants.blendMode = static_cast<float>(item.material.blendMode);
        pushConstants.baseColor = glm::vec4(item.material.baseColor, 1.0f);

        vkCmdPushConstants(
            cmd,
            pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(ObjectPushConstants),
            &pushConstants
        );

        vkCmdDrawIndexed(cmd, static_cast<uint32_t>(item.mesh->indices.size()), 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);

    // Player UI (RmlUi) composites onto the finished game image in its OWN pass (the scene
    // pass is now closed), so the RmlUi compositor is free to open offscreen layer passes
    // for effects like box-shadow. It LOADs the scene image and leaves it SHADER_READ_ONLY.
    renderer->renderRuntimeUIViewport(cmd);
}

void BasicTrianglePass::moveCameraForward(float deltaTime) {
    camera.moveForward(deltaTime);
}

void BasicTrianglePass::moveCameraBackward(float deltaTime) {
    camera.moveBackward(deltaTime);
}

void BasicTrianglePass::moveCameraLeft(float deltaTime) {
    camera.moveLeft(deltaTime);
}

void BasicTrianglePass::moveCameraRight(float deltaTime) {
    camera.moveRight(deltaTime);
}

void BasicTrianglePass::rotateCamera(float xOffset, float yOffset) {
    camera.rotate(xOffset, yOffset);
}

void BasicTrianglePass::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = renderer->getSwapChainImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // The runtime UI now renders in a following pass (see Renderer::createUiLayerPass), so
    // the scene image is handed off in COLOR_ATTACHMENT_OPTIMAL for that pass to LOAD; the
    // UI pass is what finally transitions it to SHADER_READ_ONLY for ImGui.
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = renderer->getDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(app->getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }

}

void BasicTrianglePass::createShadowRenderPass() {
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = renderer->getShadowFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(app->getDevice(), &renderPassInfo, nullptr, &shadowRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow render pass!");
    }
}

void BasicTrianglePass::createGraphicsPipeline() {
    auto vertShaderCode = readFile("build/shaders/basic.vert.spv");
    auto fragShaderCode = readFile("build/shaders/basic.frag.spv");
    auto skinnedVertShaderCode = readFile("build/shaders/skinned.vert.spv");

    VkShaderModule vertShaderModule = createShaderModule(app->getDevice(), vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(app->getDevice(), fragShaderCode);
    VkShaderModule skinnedVertShaderModule = createShaderModule(app->getDevice(), skinnedVertShaderCode);

    // One layout for every pipeline in this pass: set 0 = scene UBO + textures,
    // set 1 = the pose. Static pipelines simply never bind set 1 (Vulkan only
    // requires sets a pipeline statically uses). Sharing the layout means switching
    // between static and skinned mid-pass doesn't disturb set 0 or the push
    // constants.
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    const std::array<VkDescriptorSetLayout, 2> setLayouts = {
        renderer->getDescriptorSetLayout(),
        jointSetLayout
    };
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ObjectPushConstants);
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(app->getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pDynamicState = &dynamicState;
    
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    pipelineInfo.pRasterizationState = &rasterizer;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    pipelineInfo.pDepthStencilState = &depthStencil;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    pipelineInfo.pMultisampleState = &multisampling;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    pipelineInfo.pColorBlendState = &colorBlending;

    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    // One pipeline per blend bucket (BlendMode collapses to three GPU states; Opaque and
    // Masked share a state because Masked's only difference is a shader `discard`). This
    // is the seam (Rule 22): a new blend mode is a new row here + a case in the frag, not
    // a rewrite of the draw path. Buckets: 0 = Opaque/Masked, 1 = Translucent, 2 = Additive.
    struct BlendBucket {
        VkBool32 blendEnable;
        VkBlendFactor srcColor;
        VkBlendFactor dstColor;
        VkBool32 depthWrite; // blended geometry tests depth but must not occlude what's behind it
    };
    const BlendBucket buckets[SceneBucketCount] = {
        { VK_FALSE, VK_BLEND_FACTOR_ONE,       VK_BLEND_FACTOR_ZERO,                VK_TRUE  }, // Opaque/Masked
        { VK_TRUE,  VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_FALSE }, // Translucent
        { VK_TRUE,  VK_BLEND_FACTOR_ONE,       VK_BLEND_FACTOR_ONE,                 VK_FALSE }, // Additive
    };

    const auto staticAttributes = Vertex::getAttributeDescriptions();
    const auto skinnedAttributes = Vertex::getSkinnedAttributeDescriptions();
    VkPipelineShaderStageCreateInfo skinnedVertStageInfo = vertShaderStageInfo;
    skinnedVertStageInfo.module = skinnedVertShaderModule;
    const VkPipelineShaderStageCreateInfo skinnedStages[] = {skinnedVertStageInfo, fragShaderStageInfo};

    for (int bucket = 0; bucket < SceneBucketCount; ++bucket) {
        colorBlendAttachment.blendEnable = buckets[bucket].blendEnable;
        colorBlendAttachment.srcColorBlendFactor = buckets[bucket].srcColor;
        colorBlendAttachment.dstColorBlendFactor = buckets[bucket].dstColor;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        // Only the RGB result is blended. The framebuffer *alpha* must stay whatever the
        // opaque pass left (1.0 from the clear), because the scene renders to an offscreen
        // image ImGui later composites by that alpha — writing a sprite's cutout alpha (0
        // in transparent texels) here would punch holes the panel shows through. So keep
        // the destination alpha: srcAlpha*0 + dstAlpha*1. (Opaque bucket has blend off and
        // writes alpha 1 directly.)
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        depthStencil.depthWriteEnable = buckets[bucket].depthWrite;

        // Static (index 0): base attributes, basic vertex shader.
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(staticAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = staticAttributes.data();
        pipelineInfo.pStages = shaderStages;
        if (vkCreateGraphicsPipelines(app->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                      &scenePipelines[0][bucket]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create scene graphics pipeline!");
        }

        // Skinned (index 1): same in every respect except the vertex shader and the two
        // extra vertex attributes — so a skinned mesh (e.g. a translucent ghost) is lit,
        // shadowed and blended by identical code.
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(skinnedAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = skinnedAttributes.data();
        pipelineInfo.pStages = skinnedStages;
        if (vkCreateGraphicsPipelines(app->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                      &scenePipelines[1][bucket]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create skinned scene graphics pipeline!");
        }
    }

    vkDestroyShaderModule(app->getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(app->getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(app->getDevice(), skinnedVertShaderModule, nullptr);
}

void BasicTrianglePass::createShadowPipeline() {
    auto vertShaderCode = readFile("build/shaders/shadow.vert.spv");
    auto fragShaderCode = readFile("build/shaders/shadow.frag.spv");
    auto skinnedVertShaderCode = readFile("build/shaders/skinned_shadow.vert.spv");

    VkShaderModule vertShaderModule = createShaderModule(app->getDevice(), vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(app->getDevice(), fragShaderCode);
    VkShaderModule skinnedVertShaderModule = createShaderModule(app->getDevice(), skinnedVertShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription positionAttribute{};
    positionAttribute.binding = 0;
    positionAttribute.location = 0;
    positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    positionAttribute.offset = offsetof(Vertex, pos);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.pVertexAttributeDescriptions = &positionAttribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    std::array<VkDynamicState, 3> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BIAS
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;
    colorBlending.pAttachments = nullptr;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(app->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow graphics pipeline!");
    }

    // Skinned shadow variant. The depth pass only needs position, but it must skin
    // that position with the same matrices the scene pass uses — otherwise the
    // character animates and its shadow stays in bind pose.
    const std::array<VkVertexInputAttributeDescription, 3> skinnedShadowAttributes = {
        positionAttribute,
        Vertex::getSkinnedAttributeDescriptions()[3], // joints
        Vertex::getSkinnedAttributeDescriptions()[4]  // weights
    };
    vertexInputInfo.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(skinnedShadowAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = skinnedShadowAttributes.data();

    VkPipelineShaderStageCreateInfo skinnedVertStageInfo = vertShaderStageInfo;
    skinnedVertStageInfo.module = skinnedVertShaderModule;
    VkPipelineShaderStageCreateInfo skinnedStages[] = {skinnedVertStageInfo, fragShaderStageInfo};
    pipelineInfo.pStages = skinnedStages;

    if (vkCreateGraphicsPipelines(app->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skinnedShadowPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create skinned shadow graphics pipeline!");
    }

    vkDestroyShaderModule(app->getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(app->getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(app->getDevice(), skinnedVertShaderModule, nullptr);
}

void BasicTrianglePass::createJointResources() {
    VkDevice device = app->getDevice();

    // Set 1, binding 0: the pose. Dynamic so one buffer serves every skinned draw in
    // the frame — bind the set once, then supply a byte offset per draw, instead of
    // allocating a descriptor set per character.
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &jointSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create joint descriptor set layout!");
    }

    // A dynamic offset must be a multiple of the device's UBO offset alignment, so
    // each skin's slice is padded up to it.
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(app->getPhysicalDevice(), &properties);
    const VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
    const VkDeviceSize sliceBytes = sizeof(glm::mat4) * MAX_JOINTS;
    jointSliceSize = alignment > 0
        ? ((sliceBytes + alignment - 1) / alignment) * alignment
        : sliceBytes;

    // One region per frame in flight. The scene UBO gets away with a single buffer
    // rewritten every frame, but a torn pose is a visibly glitching character, and
    // this costs only a few hundred KiB.
    const VkDeviceSize bufferSize =
        jointSliceSize * MAX_SKINNED_DRAWS * static_cast<VkDeviceSize>(Renderer::framesInFlight());

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &jointBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create joint buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, jointBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        app->getPhysicalDevice(),
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    if (vkAllocateMemory(device, &allocInfo, nullptr, &jointBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate joint buffer memory!");
    }
    vkBindBufferMemory(device, jointBuffer, jointBufferMemory, 0);
    // Persistently mapped: this is rewritten every frame, so mapping per frame would
    // be pure overhead. Host-coherent, so no explicit flush.
    vkMapMemory(device, jointBufferMemory, 0, bufferSize, 0, &jointBufferMapped);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &jointDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create joint descriptor pool!");
    }

    VkDescriptorSetAllocateInfo setAllocInfo{};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = jointDescriptorPool;
    setAllocInfo.descriptorSetCount = 1;
    setAllocInfo.pSetLayouts = &jointSetLayout;
    if (vkAllocateDescriptorSets(device, &setAllocInfo, &jointDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate joint descriptor set!");
    }

    VkDescriptorBufferInfo descriptorBufferInfo{};
    descriptorBufferInfo.buffer = jointBuffer;
    descriptorBufferInfo.offset = 0;
    // The *range* is one slice; the dynamic offset picks which slice.
    descriptorBufferInfo.range = sizeof(glm::mat4) * MAX_JOINTS;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = jointDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    write.pBufferInfo = &descriptorBufferInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void BasicTrianglePass::uploadJointMatrices() {
    jointOffsets.assign(drawList->items.size(), NO_JOINT_OFFSET);
    if (jointBufferMapped == nullptr) {
        return;
    }

    // This frame's region — never the one the previous frame may still be reading.
    const VkDeviceSize frameBase =
        static_cast<VkDeviceSize>(renderer->getCurrentFrame()) * jointSliceSize * MAX_SKINNED_DRAWS;

    uint32_t slot = 0;
    for (size_t i = 0; i < drawList->items.size(); i++) {
        const auto& item = drawList->items[i];
        if (item.jointMatrices.empty()) {
            continue; // unskinned: the common case
        }
        if (slot >= MAX_SKINNED_DRAWS) {
            // Out of slices. Leaving the offset unset draws this character
            // unskinned, which is wrong but local — reusing another character's
            // slice would pose it with someone else's skeleton. Warn once so the
            // silent bind-pose fallback (#29) is at least visible to the developer.
            static bool warned = false;
            if (!warned) {
                warned = true;
                std::cerr << "[renderer] more than " << MAX_SKINNED_DRAWS
                          << " skinned draws this frame; extra characters render in bind pose.\n";
            }
            break;
        }

        const VkDeviceSize offset = frameBase + static_cast<VkDeviceSize>(slot) * jointSliceSize;
        // A skin with more joints than the shader array is truncated rather than
        // overrunning the slice into the next character's.
        const size_t count = std::min<size_t>(item.jointMatrices.size(), MAX_JOINTS);
        std::memcpy(
            static_cast<char*>(jointBufferMapped) + offset,
            item.jointMatrices.data(),
            count * sizeof(glm::mat4)
        );

        jointOffsets[i] = static_cast<uint32_t>(offset);
        slot++;
    }
}

void BasicTrianglePass::createUniformBuffer() {
    VkDevice device = app->getDevice();

    // One slice per frame in flight, each padded to the device's dynamic-offset
    // alignment. Previously this was a single copy rewritten every frame while the
    // GPU could still be reading the previous frame's — a real race that showed as
    // an occasional one-frame camera/light pop.
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(app->getPhysicalDevice(), &properties);
    const VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
    uniformSliceSize = alignment > 0
        ? ((sizeof(UniformBufferObject) + alignment - 1) / alignment) * alignment
        : sizeof(UniformBufferObject);

    const VkDeviceSize bufferSize =
        uniformSliceSize * static_cast<VkDeviceSize>(Renderer::framesInFlight());

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &uniformBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create uniform buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, uniformBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        app->getPhysicalDevice(),
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (vkAllocateMemory(device, &allocInfo, nullptr, &uniformBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate uniform buffer memory!");
    }

    vkBindBufferMemory(device, uniformBuffer, uniformBufferMemory, 0);
    // Persistently mapped: rewritten every frame, so mapping per frame is pure
    // overhead. Host-coherent, so no explicit flush.
    vkMapMemory(device, uniformBufferMemory, 0, bufferSize, 0, &uniformBufferMapped);
}

void BasicTrianglePass::updateUniformBuffer() {
    UniformBufferObject ubo{};

    ubo.view = camera.getViewMatrix();
    ubo.proj = camera.getProjectionMatrix(
        renderer->getRenderExtent().width / static_cast<float>(renderer->getRenderExtent().height)
    );
    ubo.viewPos = glm::vec4(camera.position, 1.0f);

    glm::vec3 lightPosition = {2.5f, 4.0f, 2.5f};
    glm::vec3 lightTarget = {0.0f, 0.0f, 0.0f};

    if (drawList != nullptr) {
        ubo.lightCount = static_cast<int>(std::min<size_t>(drawList->lights.size(), MAX_LIGHTS));
        for (int i = 0; i < ubo.lightCount; i++) {
            ubo.lightPositions[i] = glm::vec4(drawList->lights[static_cast<size_t>(i)].position, 1.0f);
            ubo.lightColors[i] = glm::vec4(drawList->lights[static_cast<size_t>(i)].color, 1.0f);
        }

        if (!drawList->lights.empty()) {
            lightPosition = drawList->lights.front().position;
        }

        if (!drawList->items.empty()) {
            lightTarget = glm::vec3(0.0f);
            for (const auto& item : drawList->items) {
                lightTarget += glm::vec3(item.model[3]);
            }
            lightTarget /= static_cast<float>(drawList->items.size());
        }
    }

    // Fit the shadow frustum to the scene instead of a hardcoded [-10,10]x[0.1,50]
    // box, which left anything past ~20 units unshadowed and starved texel density.
    // Bound the scene by a sphere around the light target: radius = farthest object
    // center from the target, plus a margin for mesh extent (object origins ignore
    // the geometry that surrounds them). Empty scene keeps a sane default extent.
    float sceneRadius = 5.0f;
    if (drawList != nullptr && !drawList->items.empty()) {
        float maxDistSq = 0.0f;
        for (const auto& item : drawList->items) {
            const glm::vec3 delta = glm::vec3(item.model[3]) - lightTarget;
            maxDistSq = std::max(maxDistSq, glm::dot(delta, delta));
        }
        sceneRadius = std::sqrt(maxDistSq);
    }
    // Margin so a mesh whose *center* sits on the bound still has its body covered,
    // and so a lone-object scene (radius 0) still gets a usable frustum.
    const float shadowExtent = sceneRadius * 1.25f + 5.0f;

    // Guard the up vector: a light pointing (near) straight down is parallel to the
    // default (0,1,0) up, which makes glm::lookAt degenerate and produces a NaN
    // matrix — and a top-down light is the common case for a top-down game. Fall
    // back to a Z-up basis when the light direction is nearly vertical.
    glm::vec3 lightDir = lightTarget - lightPosition;
    const float lightDirLen = glm::length(lightDir);
    lightDir = lightDirLen > 1e-6f ? lightDir / lightDirLen : glm::vec3(0.0f, -1.0f, 0.0f);
    const glm::vec3 up = std::abs(lightDir.y) > 0.999f
        ? glm::vec3(0.0f, 0.0f, 1.0f)
        : glm::vec3(0.0f, 1.0f, 0.0f);

    // Push the light back far enough that the whole bounding sphere is in front of
    // the near plane, then size near/far to span it. Distance to the target plus the
    // radius on each side, clamped so near stays positive.
    const float lightDistance = std::max(lightDirLen, shadowExtent + 1.0f);
    const glm::vec3 fitLightPos = lightTarget - lightDir * lightDistance;
    const float nearPlane = std::max(lightDistance - shadowExtent, 0.05f);
    const float farPlane = lightDistance + shadowExtent;

    const glm::mat4 lightView = glm::lookAt(fitLightPos, lightTarget, up);
    const glm::mat4 lightProj =
        glm::ortho(-shadowExtent, shadowExtent, -shadowExtent, shadowExtent, nearPlane, farPlane);
    ubo.lightSpaceMatrix = lightProj * lightView;

    if (uniformBufferMapped == nullptr) {
        return;
    }
    // This frame's slice — never the one the previous frame may still be reading.
    std::memcpy(
        static_cast<char*>(uniformBufferMapped) +
            static_cast<VkDeviceSize>(renderer->getCurrentFrame()) * uniformSliceSize,
        &ubo,
        sizeof(ubo)
    );
}
