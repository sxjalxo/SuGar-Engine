#include "Renderer.h"
#include "SuGarApp.h"
#include "BasicTrianglePass.h"
#include "assets/AssetRegistry.h"
#include "assets/ModelImporter.h"
#include "assets/ResourceManager.h"
#include "audio/AudioClip.h"
#include "ecs/Registry.h"
#include "rendering/Mesh.h"
#include "rendering/Camera.h"
#include "editor/EditorCommands.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "scene/DrawList.h"
#include "scene/SceneSerializer.h"
#include "scene/TransformMath.h"
#include "rendering/Texture.h"
#include "rendering/UniformBufferObject.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "ImGuizmo.h" // must follow imgui.h (relies on its types)
#include <array>
#include <vector>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <iostream>
#include <string>
#include <GLFW/glfw3.h>

namespace {
void checkImGuiVkResult(VkResult result) {
    if (result == VK_SUCCESS) {
        return;
    }

    std::cerr << "[imgui] Vulkan backend error: " << result << "\n";
}

std::string getEntityLabel(const Registry& registry, Entity entity) {
    if (registry.names.has(entity) && !registry.names.get(entity).name.empty()) {
        return registry.names.get(entity).name;
    }

    return "Entity " + std::to_string(entity);
}

std::string toLower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

// A reparent requested via hierarchy drag-and-drop, applied after the tree is
// drawn so we never mutate the hierarchy mid-iteration.
struct ReparentRequest {
    Entity child = INVALID_ENTITY;
    Entity newParent = INVALID_ENTITY;
    bool pending = false;
};

void drawEntityNodeRecursive(Registry& registry, Entity entity, Entity& selectedEntity,
                             std::vector<Entity>& selectedEntities, ReparentRequest& reparent) {
    const bool hasHierarchy = registry.hierarchy.has(entity);
    const auto& children = hasHierarchy ? registry.hierarchy.get(entity).children : std::vector<Entity>{};
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    const bool entitySelected =
        std::find(selectedEntities.begin(), selectedEntities.end(), entity) != selectedEntities.end();
    if (entitySelected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const std::string label = getEntityLabel(registry, entity);
    const bool opened = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uintptr_t>(entity)),
        flags,
        "%s",
        label.c_str()
    );

    if (ImGui::IsItemClicked()) {
        // Ctrl-click toggles membership; a plain click selects just this entity.
        if (ImGui::GetIO().KeyCtrl) {
            const auto it = std::find(selectedEntities.begin(), selectedEntities.end(), entity);
            if (it != selectedEntities.end()) {
                selectedEntities.erase(it);
                selectedEntity = selectedEntities.empty() ? INVALID_ENTITY : selectedEntities.back();
            } else {
                selectedEntities.push_back(entity);
                selectedEntity = entity;
            }
        } else {
            selectedEntities.clear();
            selectedEntities.push_back(entity);
            selectedEntity = entity;
        }
    }

    // Drag this entity...
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("ENTITY_NODE", &entity, sizeof(Entity));
        ImGui::Text("%s", label.c_str());
        ImGui::EndDragDropSource();
    }
    // ...and drop it onto another node to reparent under it.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE")) {
            reparent.child = *static_cast<const Entity*>(payload->Data);
            reparent.newParent = entity;
            reparent.pending = true;
        }
        ImGui::EndDragDropTarget();
    }

    if (!opened) {
        return;
    }

    for (Entity child : children) {
        if (registry.transforms.has(child)) {
            drawEntityNodeRecursive(registry, child, selectedEntity, selectedEntities, reparent);
        }
    }

    ImGui::TreePop();
}
} // namespace

void Renderer::setWindow(GLFWwindow* window) {
    this->window = window;
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void Renderer::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto renderer = reinterpret_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer) {
        renderer->framebufferResized = true;
    }
}

Renderer::Renderer(SuGarApp* app) : app(app) {}

Renderer::~Renderer() {
    shutdown();
}

void Renderer::init() {
    if (window == nullptr) {
        throw std::runtime_error("renderer requires a valid window before initialization.");
    }

    createSwapChain();
    createImageViews();
    createUiRenderPass();
    depthFormat = findDepthFormat();
    shadowFormat = findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
    );
    createDescriptorSetLayout();
    createDescriptorPool();
    createImGuiDescriptorPool();

    auto pass = std::make_unique<BasicTrianglePass>(app, this);
    activePass = pass.get();
    if (drawList != nullptr) {
        activePass->setDrawList(drawList);
    }
    pass->setup();
    mainRenderPass = std::move(pass);
    initImGui();
    viewportExtent = swapChainExtent;
    requestedViewportExtent = viewportExtent;
    createViewportResources();
    createShadowResources();

    createFramebuffers();
    createSyncObjects();
    createDescriptorSets();

}

void Renderer::shutdown() {
    VkDevice device = app->getDevice();
    if (device == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle(device);

    destroyViewportResources();
    destroyShadowResources();
    shutdownImGui();
    activePass = nullptr;
    mainRenderPass.reset();
    cleanupSwapChain();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (imageAvailableSemaphores.size() > i) {
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        }
        if (inFlightFences.size() > i) {
            vkDestroyFence(device, inFlightFences[i], nullptr);
        }
    }

    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();

    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (uiRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, uiRenderPass, nullptr);
        uiRenderPass = VK_NULL_HANDLE;
    }

    if (imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, imguiDescriptorPool, nullptr);
        imguiDescriptorPool = VK_NULL_HANDLE;
    }
}

void Renderer::drawFrame() {
    vkWaitForFences(app->getDevice(), 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    if (!deferredResourceReleases.empty()) {
        vkDeviceWaitIdle(app->getDevice());
        for (AssetHandle handle : deferredResourceReleases) {
            ResourceManager::release(handle);
        }
        deferredResourceReleases.clear();
    }

    if (descriptorRefreshRequested) {
        refreshDrawListResources();
        descriptorRefreshRequested = false;
    }

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        app->getDevice(),
        swapChain,
        UINT64_MAX,
        imageAvailableSemaphores[currentFrame],
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    updateViewportResourcesIfNeeded();
    beginImGuiFrame();

    vkResetFences(app->getDevice(), 1, &inFlightFences[currentFrame]);

    const auto& commandBuffers = app->getCommandBuffers();
    VkCommandBuffer cmd = commandBuffers[currentFrame];

    vkResetCommandBuffer(cmd, 0);
    recordCommandBuffer(cmd, imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[imageIndex] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(app->getGraphicsQueue(), 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = { swapChain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(app->getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::setDrawList(const DrawList* newDrawList) {
    drawList = newDrawList;

    if (activePass != nullptr) {
        activePass->setDrawList(drawList);
    }
}

void Renderer::refreshDrawListResources() {
    if (app->getDevice() == VK_NULL_HANDLE || descriptorSetLayout == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle(app->getDevice());

    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(app->getDevice(), descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    textureDescriptorSets.clear();
    createDescriptorPool();
    createDescriptorSets();
}

void Renderer::moveCameraForward(float deltaTime) {
    if (activePass != nullptr) {
        activePass->moveCameraForward(deltaTime);
    }
}

void Renderer::moveCameraBackward(float deltaTime) {
    if (activePass != nullptr) {
        activePass->moveCameraBackward(deltaTime);
    }
}

void Renderer::moveCameraLeft(float deltaTime) {
    if (activePass != nullptr) {
        activePass->moveCameraLeft(deltaTime);
    }
}

void Renderer::moveCameraRight(float deltaTime) {
    if (activePass != nullptr) {
        activePass->moveCameraRight(deltaTime);
    }
}

void Renderer::rotateCamera(float xOffset, float yOffset) {
    if (activePass != nullptr) {
        activePass->rotateCamera(xOffset, yOffset);
    }
}

void Renderer::setCameraMode(CameraMode mode) {
    if (activePass != nullptr) {
        activePass->setCameraMode(mode);
    }
}

void Renderer::setOrbitTarget(const glm::vec3& target) {
    if (activePass != nullptr) {
        activePass->setOrbitTarget(target);
    }
}

void Renderer::setFollowTargetPosition(const glm::vec3& position) {
    if (activePass != nullptr) {
        activePass->setFollowTargetPosition(position);
    }
}

void Renderer::recreateSwapChain() {
    vkDeviceWaitIdle(app->getDevice());

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    const VkFormat previousSwapChainFormat = swapChainImageFormat;
    cleanupSwapChain();
    createSwapChain();
    if (swapChainImageFormat != previousSwapChainFormat) {
        throw std::runtime_error("swapchain format changed during recreation; renderer reinit path required.");
    }
    if (imguiInitialized) {
        ImGui_ImplVulkan_SetMinImageCount(static_cast<uint32_t>(swapChainImages.size()));
    }
    createImageViews();
    createDescriptorPool();
    createFramebuffers();
    createDescriptorSets();
}

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (!mainRenderPass) {
        throw std::runtime_error("renderer has no active render pass to record.");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    mainRenderPass->execute(cmd, imageIndex);

    VkRenderPassBeginInfo uiRenderPassInfo{};
    uiRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    uiRenderPassInfo.renderPass = uiRenderPass;
    uiRenderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    uiRenderPassInfo.renderArea.offset = {0, 0};
    uiRenderPassInfo.renderArea.extent = swapChainExtent;

    VkClearValue uiClearColor = {{{0.08f, 0.08f, 0.09f, 1.0f}}};
    uiRenderPassInfo.clearValueCount = 1;
    uiRenderPassInfo.pClearValues = &uiClearColor;

    vkCmdBeginRenderPass(cmd, &uiRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    renderImGui(cmd);
    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void Renderer::cleanupSwapChain() {
    for (auto semaphore : renderFinishedSemaphores) {
        vkDestroySemaphore(app->getDevice(), semaphore, nullptr);
    }
    renderFinishedSemaphores.clear();

    for (auto framebuffer : swapChainFramebuffers) {
        vkDestroyFramebuffer(app->getDevice(), framebuffer, nullptr);
    }
    swapChainFramebuffers.clear();

    for (auto imageView : swapChainImageViews) {
        vkDestroyImageView(app->getDevice(), imageView, nullptr);
    }
    swapChainImageViews.clear();

    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(app->getDevice(), descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    textureDescriptorSets.clear();

    if (swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(app->getDevice(), swapChain, nullptr);
        swapChain = VK_NULL_HANDLE;
    }

    swapChainImages.clear();
}

void Renderer::createSwapChain() {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(app->getPhysicalDevice(), app->getSurface());

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = app->getSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = findQueueFamilies(app->getPhysicalDevice(), app->getSurface());
    uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(app->getDevice(), &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(app->getDevice(), swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(app->getDevice(), swapChain, &imageCount, swapChainImages.data());

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    renderFinishedSemaphores.resize(swapChainImages.size());
    for (size_t i = 0; i < swapChainImages.size(); i++) {
        if (vkCreateSemaphore(app->getDevice(), &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create renderFinishedSemaphore!");
        }
    }

    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;

}

void Renderer::createImageViews() {
    swapChainImageViews.resize(swapChainImages.size());

    for (size_t i = 0; i < swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(app->getDevice(), &createInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image views!");
        }
    }

}

void Renderer::createUiRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].dstAccessMask = 0;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(app->getDevice(), &renderPassInfo, nullptr, &uiRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create UI render pass!");
    }
}

void Renderer::createFramebuffers() {
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        VkImageView attachments[] = {
            swapChainImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = uiRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapChainExtent.width;
        framebufferInfo.height = swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(app->getDevice(), &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }

}

void Renderer::createViewportResources() {
    if (viewportExtent.width == 0 || viewportExtent.height == 0) {
        throw std::runtime_error("viewport resources require a non-zero extent.");
    }

    createImage(
        viewportExtent.width,
        viewportExtent.height,
        swapChainImageFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        viewportImage,
        viewportImageMemory
    );

    viewportImageView = createImageView(
        viewportImage,
        swapChainImageFormat,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    createDepthResources();

    VkImageView attachments[] = {
        viewportImageView,
        depthImageView
    };

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = getSceneRenderPass();
    framebufferInfo.attachmentCount = 2;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = viewportExtent.width;
    framebufferInfo.height = viewportExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(app->getDevice(), &framebufferInfo, nullptr, &viewportFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create viewport framebuffer!");
    }

    if (imguiInitialized) {
        viewportTextureDescriptor = ImGui_ImplVulkan_AddTexture(
            viewportImageView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
    }
}

void Renderer::destroyViewportResources() {
    VkDevice device = app->getDevice();

    if (imguiInitialized && viewportTextureDescriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(viewportTextureDescriptor);
        viewportTextureDescriptor = VK_NULL_HANDLE;
    }

    if (viewportFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, viewportFramebuffer, nullptr);
        viewportFramebuffer = VK_NULL_HANDLE;
    }

    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }

    if (depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, depthImage, nullptr);
        depthImage = VK_NULL_HANDLE;
    }

    if (depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, depthImageMemory, nullptr);
        depthImageMemory = VK_NULL_HANDLE;
    }

    if (viewportImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, viewportImageView, nullptr);
        viewportImageView = VK_NULL_HANDLE;
    }

    if (viewportImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, viewportImage, nullptr);
        viewportImage = VK_NULL_HANDLE;
    }

    if (viewportImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, viewportImageMemory, nullptr);
        viewportImageMemory = VK_NULL_HANDLE;
    }
}

void Renderer::createShadowResources() {
    createImage(
        shadowExtent.width,
        shadowExtent.height,
        shadowFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        shadowImage,
        shadowImageMemory
    );

    shadowImageView = createImageView(shadowImage, shadowFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    samplerInfo.maxAnisotropy = 1.0f;

    if (vkCreateSampler(app->getDevice(), &samplerInfo, nullptr, &shadowSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow sampler!");
    }

    VkImageView attachments[] = {
        shadowImageView
    };

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = getShadowRenderPass();
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = shadowExtent.width;
    framebufferInfo.height = shadowExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(app->getDevice(), &framebufferInfo, nullptr, &shadowFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow framebuffer!");
    }
}

void Renderer::destroyShadowResources() {
    VkDevice device = app->getDevice();

    if (shadowFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, shadowFramebuffer, nullptr);
        shadowFramebuffer = VK_NULL_HANDLE;
    }

    if (shadowSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, shadowSampler, nullptr);
        shadowSampler = VK_NULL_HANDLE;
    }

    if (shadowImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, shadowImageView, nullptr);
        shadowImageView = VK_NULL_HANDLE;
    }

    if (shadowImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, shadowImage, nullptr);
        shadowImage = VK_NULL_HANDLE;
    }

    if (shadowImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, shadowImageMemory, nullptr);
        shadowImageMemory = VK_NULL_HANDLE;
    }
}

void Renderer::updateViewportResourcesIfNeeded() {
    if (!viewportResourcesDirty) {
        return;
    }

    if (requestedViewportExtent.width == 0 || requestedViewportExtent.height == 0) {
        viewportResourcesDirty = false;
        return;
    }

    vkDeviceWaitIdle(app->getDevice());
    destroyViewportResources();
    viewportExtent = requestedViewportExtent;
    createViewportResources();
    viewportResourcesDirty = false;
}

void Renderer::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(app->getDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(app->getDevice(), &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects!");
        }
    }

}

Renderer::SwapChainSupportDetails Renderer::querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR Renderer::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR Renderer::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Renderer::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    width = std::clamp(width,
        static_cast<int>(capabilities.minImageExtent.width),
        static_cast<int>(capabilities.maxImageExtent.width));
    height = std::clamp(height,
        static_cast<int>(capabilities.minImageExtent.height),
        static_cast<int>(capabilities.maxImageExtent.height));

    VkExtent2D actualExtent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };
    return actualExtent;
}

VkFormat Renderer::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(app->getPhysicalDevice(), format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (props.linearTilingFeatures & features) == features) {
            return format;
        }

        if (tiling == VK_IMAGE_TILING_OPTIMAL &&
            (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format!");
}

VkFormat Renderer::findDepthFormat() {
    return findSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

bool Renderer::hasStencilComponent(VkFormat format) const {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

void Renderer::createDepthResources() {
    depthFormat = findDepthFormat();

    createImage(
        viewportExtent.width,
        viewportExtent.height,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        depthImage,
        depthImageMemory
    );

    depthImageView = createImageView(
        depthImage,
        depthFormat,
        hasStencilComponent(depthFormat)
            ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
            : VK_IMAGE_ASPECT_DEPTH_BIT
    );

}

Renderer::QueueFamilyIndices Renderer::findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }
        i++;
    }

    return indices;
}

void Renderer::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(app->getDevice(), &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(app->getDevice(), image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(app->getDevice(), &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate image memory!");
    }

    vkBindImageMemory(app->getDevice(), image, imageMemory, 0);
}

VkImageView Renderer::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(app->getDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image view!");
    }

    return imageView;
}

uint32_t Renderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(app->getPhysicalDevice(), &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

std::vector<AssetHandle> Renderer::collectDrawListTextures() const {
    std::vector<AssetHandle> textures;

    if (drawList == nullptr) {
        return textures;
    }

    for (const auto& item : drawList->items) {
        if (item.material.albedo == INVALID_HANDLE) {
            continue;
        }

        const auto texture = ResourceManager::getTexture(item.material.albedo);
        if (!texture || !texture->isReady()) {
            continue;
        }

        if (std::find(textures.begin(), textures.end(), item.material.albedo) == textures.end()) {
            textures.push_back(item.material.albedo);
        }
    }

    return textures;
}

VkDescriptorSet Renderer::getDescriptorSet(AssetHandle textureHandle, uint32_t imageIndex) const {
    auto it = textureDescriptorSets.find(textureHandle);
    if (it == textureDescriptorSets.end() || imageIndex >= it->second.size()) {
        throw std::runtime_error("texture descriptor set was not created for this swapchain image!");
    }

    return it->second[imageIndex];
}

void Renderer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding shadowSamplerLayoutBinding{};
    shadowSamplerLayoutBinding.binding = 2;
    shadowSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowSamplerLayoutBinding.descriptorCount = 1;
    shadowSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    shadowSamplerLayoutBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {
        uboLayoutBinding,
        samplerLayoutBinding,
        shadowSamplerLayoutBinding
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(app->getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout");
    }
}

void Renderer::createDescriptorPool() {
    const auto textures = collectDrawListTextures();
    const uint32_t descriptorSetCount = static_cast<uint32_t>(
        std::max<size_t>(textures.size(), 1) * swapChainImages.size()
    );

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = descriptorSetCount;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = descriptorSetCount;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[2].descriptorCount = descriptorSetCount;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = descriptorSetCount;

    if (vkCreateDescriptorPool(app->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool");
    }
}

void Renderer::createDescriptorSets() {
    textureDescriptorSets.clear();

    const auto textures = collectDrawListTextures();
    if (textures.empty()) {
        return;
    }

    if (activePass == nullptr) {
        throw std::runtime_error("descriptor sets require an active scene pass.");
    }

    const size_t descriptorSetCount = textures.size() * swapChainImages.size();
    std::vector<VkDescriptorSetLayout> layouts(descriptorSetCount, descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(descriptorSetCount);
    allocInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> allocatedDescriptorSets(descriptorSetCount);

    if (vkAllocateDescriptorSets(app->getDevice(), &allocInfo, allocatedDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets");
    }

    size_t descriptorIndex = 0;
    for (AssetHandle textureHandle : textures) {
        const auto texture = ResourceManager::getTexture(textureHandle);
        if (!texture) {
            throw std::runtime_error("texture handle in draw list no longer resolves to a texture resource.");
        }

        auto& setsForTexture = textureDescriptorSets[textureHandle];
        setsForTexture.resize(swapChainImages.size());

        for (size_t i = 0; i < swapChainImages.size(); i++) {
            VkDescriptorSet descriptorSet = allocatedDescriptorSets[descriptorIndex++];
            setsForTexture[i] = descriptorSet;

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = activePass->getUniformBuffer();
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(UniformBufferObject);

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = texture->getImageView();
            imageInfo.sampler = texture->getSampler();

            VkDescriptorImageInfo shadowImageInfo{};
            shadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            shadowImageInfo.imageView = shadowImageView;
            shadowImageInfo.sampler = shadowSampler;

            std::array<VkWriteDescriptorSet, 3> descriptorWrites{};

            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = descriptorSet;
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pBufferInfo = &bufferInfo;

            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = descriptorSet;
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pImageInfo = &imageInfo;

            descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[2].dstSet = descriptorSet;
            descriptorWrites[2].dstBinding = 2;
            descriptorWrites[2].dstArrayElement = 0;
            descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[2].descriptorCount = 1;
            descriptorWrites[2].pImageInfo = &shadowImageInfo;

            vkUpdateDescriptorSets(
                app->getDevice(),
                static_cast<uint32_t>(descriptorWrites.size()),
                descriptorWrites.data(),
                0,
                nullptr
            );
        }
    }
}

VkRenderPass Renderer::getSceneRenderPass() const {
    if (activePass == nullptr) {
        return VK_NULL_HANDLE;
    }

    return activePass->getRenderPass();
}

VkRenderPass Renderer::getShadowRenderPass() const {
    if (activePass == nullptr) {
        return VK_NULL_HANDLE;
    }

    return activePass->getShadowRenderPass();
}

void Renderer::createImGuiDescriptorPool() {
    std::array<VkDescriptorPoolSize, 3> poolSizes = {{
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 }
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 3000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(app->getDevice(), &poolInfo, nullptr, &imguiDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create ImGui descriptor pool");
    }
}

void Renderer::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
        throw std::runtime_error("failed to initialize ImGui GLFW backend");
    }

    const auto queueFamilies = findQueueFamilies(app->getPhysicalDevice(), app->getSurface());

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = app->getInstance();
    initInfo.PhysicalDevice = app->getPhysicalDevice();
    initInfo.Device = app->getDevice();
    initInfo.QueueFamily = queueFamilies.graphicsFamily.value();
    initInfo.Queue = app->getGraphicsQueue();
    initInfo.DescriptorPool = imguiDescriptorPool;
    initInfo.MinImageCount = static_cast<uint32_t>(swapChainImages.size());
    initInfo.ImageCount = static_cast<uint32_t>(swapChainImages.size());
    initInfo.PipelineInfoMain.RenderPass = uiRenderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = checkImGuiVkResult;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("failed to initialize ImGui Vulkan backend");
    }

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    imguiInitialized = true;
}

void Renderer::shutdownImGui() {
    if (!imguiInitialized) {
        return;
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    imguiInitialized = false;
}

void Renderer::beginImGuiFrame() {
    if (!imguiInitialized) {
        return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    buildEditorUi();
    ImGui::Render();
}

void Renderer::buildEditorUi() {
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    // Drop selection entries whose entities no longer exist (after undo/delete).
    if (registry != nullptr) {
        selectedEntities.erase(
            std::remove_if(selectedEntities.begin(), selectedEntities.end(),
                           [&](Entity entity) { return !registry->transforms.has(entity); }),
            selectedEntities.end());
        if (selectedEntity != INVALID_ENTITY && !registry->transforms.has(selectedEntity)) {
            selectedEntity = selectedEntities.empty() ? INVALID_ENTITY : selectedEntities.back();
        }
    }

    // Editor keyboard shortcuts (suppressed while typing into a field).
    if (registry != nullptr && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
            commandHistory.undo(*registry);
            descriptorRefreshRequested = true;
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
            ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) {
            commandHistory.redo(*registry);
            descriptorRefreshRequested = true;
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
            duplicateSelectedEntity();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            deleteSelectedEntities();
        }
    }

    ImGui::Begin("Editor");
    ImGui::Text("SuGar Engine Editor");
    ImGui::Text("Docking is enabled.");
    const int renderItems = drawList != nullptr ? static_cast<int>(drawList->items.size()) : 0;
    ImGui::Text("Scene items: %d", renderItems);
    ImGui::Text("Draw calls: %d", renderItems * 2);
    ImGui::End();

    drawPlayControls();
    drawTimelinePanel();
    drawHierarchyPanel();
    drawInspectorPanel();
    drawAssetBrowserPanel();

    ImGui::Begin("Viewport");

    // Gizmo + edit toolbar.
    {
        auto opButton = [&](const char* label, GizmoOp op) {
            const bool active = gizmoOp == op;
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            if (ImGui::Button(label)) {
                gizmoOp = op;
            }
            if (active) {
                ImGui::PopStyleColor();
            }
        };
        opButton("Move", GizmoOp::Translate);
        ImGui::SameLine();
        opButton("Rotate", GizmoOp::Rotate);
        ImGui::SameLine();
        opButton("Scale", GizmoOp::Scale);
        ImGui::SameLine();
        ImGui::Checkbox("World", &gizmoWorldSpace);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        ImGui::BeginDisabled(!commandHistory.canUndo());
        if (ImGui::Button("Undo") && registry != nullptr) {
            commandHistory.undo(*registry);
            descriptorRefreshRequested = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!commandHistory.canRedo());
        if (ImGui::Button("Redo") && registry != nullptr) {
            commandHistory.redo(*registry);
            descriptorRefreshRequested = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(selectedEntities.empty());
        if (ImGui::Button("Duplicate")) {
            duplicateSelectedEntity();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            deleteSelectedEntities();
        }
        ImGui::EndDisabled();
    }

    const ImVec2 size = ImGui::GetContentRegionAvail();
    viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    if (size.x > 1.0f && size.y > 1.0f) {
        const VkExtent2D nextViewportExtent = {
            static_cast<uint32_t>(std::max(size.x, 1.0f)),
            static_cast<uint32_t>(std::max(size.y, 1.0f))
        };

        requestedViewportExtent = nextViewportExtent;
        if (requestedViewportExtent.width != viewportExtent.width ||
            requestedViewportExtent.height != viewportExtent.height) {
            viewportResourcesDirty = true;
        }
    }

    if (viewportTextureDescriptor != VK_NULL_HANDLE) {
        // Tint the viewport border to signal runtime state at a glance:
        // green while playing, amber while paused, none in edit.
        ImVec4 borderColor(0.0f, 0.0f, 0.0f, 0.0f);
        if (app != nullptr) {
            switch (app->getEngineState()) {
                case EngineState::Play:   borderColor = ImVec4(0.20f, 0.90f, 0.30f, 1.0f); break;
                case EngineState::Paused: borderColor = ImVec4(0.95f, 0.85f, 0.20f, 1.0f); break;
                case EngineState::Edit:   break;
            }
        }
        ImGui::Image(reinterpret_cast<ImTextureID>(viewportTextureDescriptor), size,
                     ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                     ImVec4(1.0f, 1.0f, 1.0f, 1.0f), borderColor);

        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const bool imageHovered = ImGui::IsItemHovered();

        // Manipulator over the image (updates ImGuizmo::IsOver/IsUsing for picking).
        drawGizmo(imageMin.x, imageMin.y, size.x, size.y);

        // Click-to-select: ray through the clicked pixel, unless the click landed
        // on the gizmo.
        if (registry != nullptr && imageHovered &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGuizmo::IsOver() && !ImGuizmo::IsUsingAny()) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const Entity picked = pickEntityAt(mouse.x - imageMin.x, mouse.y - imageMin.y, size.x, size.y);
            // Ctrl-click extends the selection; a plain click replaces it.
            if (ImGui::GetIO().KeyCtrl) {
                toggleSelect(picked);
            } else {
                selectSingle(picked);
            }
        }
    } else {
        ImGui::Text("Viewport texture unavailable.");
    }

    ImGui::Text("Render target: %u x %u", viewportExtent.width, viewportExtent.height);
    ImGui::End();
}

namespace {
// Slab-method ray vs axis-aligned box. Returns the near hit distance in tNear.
bool rayIntersectsAabb(const glm::vec3& origin, const glm::vec3& dir,
                       const glm::vec3& lo, const glm::vec3& hi, float& tNear) {
    float tmin = 0.0f;
    float tmax = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(dir[axis]) < 1e-8f) {
            if (origin[axis] < lo[axis] || origin[axis] > hi[axis]) {
                return false; // parallel to slab and outside it
            }
            continue;
        }
        float t1 = (lo[axis] - origin[axis]) / dir[axis];
        float t2 = (hi[axis] - origin[axis]) / dir[axis];
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) {
            return false;
        }
    }
    tNear = tmin;
    return true;
}
} // namespace

Entity Renderer::pickEntityAt(float pixelX, float pixelY, float viewportWidth, float viewportHeight) const {
    if (registry == nullptr || activePass == nullptr || viewportWidth <= 0.0f || viewportHeight <= 0.0f) {
        return INVALID_ENTITY;
    }

    Camera& camera = activePass->getCamera();
    // Camera world basis from the inverse view matrix — mode-agnostic (works for
    // FREE/ORBIT/FOLLOW), unlike reading yaw/pitch directly.
    const glm::mat4 invView = glm::inverse(camera.getViewMatrix());
    const glm::vec3 rayOrigin = glm::vec3(invView[3]);
    const glm::vec3 right = glm::normalize(glm::vec3(invView[0]));
    const glm::vec3 up = glm::normalize(glm::vec3(invView[1]));
    const glm::vec3 forward = glm::normalize(-glm::vec3(invView[2]));

    // Pixel -> normalized device coords in [-1,1] (y up). Build the ray straight
    // from the camera frustum so the Vulkan projection Y-flip doesn't matter.
    const float ndcX = (2.0f * pixelX / viewportWidth) - 1.0f;
    const float ndcY = 1.0f - (2.0f * pixelY / viewportHeight);
    const float aspect = viewportWidth / viewportHeight;
    const float tanHalfFovY = std::tan(glm::radians(camera.fov) * 0.5f);
    const glm::vec3 rayDir = glm::normalize(
        forward + right * (ndcX * aspect * tanHalfFovY) + up * (ndcY * tanHalfFovY));

    Entity hit = INVALID_ENTITY;
    float closest = std::numeric_limits<float>::max();

    for (const auto& [entity, meshComponent] : registry->meshes.getAll()) {
        if (!registry->transforms.has(entity)) {
            continue;
        }
        const auto mesh = ResourceManager::getMesh(meshComponent.mesh);
        if (!mesh || mesh->vertices.empty()) {
            continue;
        }

        // Local-space AABB from the mesh vertices.
        glm::vec3 lo(std::numeric_limits<float>::max());
        glm::vec3 hi(std::numeric_limits<float>::lowest());
        for (const Vertex& vertex : mesh->vertices) {
            const glm::vec3 position(vertex.pos[0], vertex.pos[1], vertex.pos[2]);
            lo = glm::min(lo, position);
            hi = glm::max(hi, position);
        }

        // Transform the 8 corners to world space and rebuild a (loose) world AABB.
        const glm::mat4 model = getWorldMatrix(entity, *registry);
        glm::vec3 worldLo(std::numeric_limits<float>::max());
        glm::vec3 worldHi(std::numeric_limits<float>::lowest());
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 local(
                (corner & 1) ? hi.x : lo.x,
                (corner & 2) ? hi.y : lo.y,
                (corner & 4) ? hi.z : lo.z);
            const glm::vec3 world = glm::vec3(model * glm::vec4(local, 1.0f));
            worldLo = glm::min(worldLo, world);
            worldHi = glm::max(worldHi, world);
        }

        float tNear = 0.0f;
        if (rayIntersectsAabb(rayOrigin, rayDir, worldLo, worldHi, tNear) && tNear < closest) {
            closest = tNear;
            hit = entity;
        }
    }

    return hit;
}

void Renderer::drawGizmo(float imageMinX, float imageMinY, float imageWidth, float imageHeight) {
    if (registry == nullptr || activePass == nullptr || selectedEntity == INVALID_ENTITY ||
        !registry->transforms.has(selectedEntity) || imageWidth <= 0.0f || imageHeight <= 0.0f) {
        return;
    }

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(imageMinX, imageMinY, imageWidth, imageHeight);

    Camera& camera = activePass->getCamera();
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 proj = camera.getProjectionMatrix(imageWidth / imageHeight);
    proj[1][1] *= -1.0f; // undo the Vulkan Y-flip so ImGuizmo matches the rendered image

    glm::mat4 world = getWorldMatrix(selectedEntity, *registry);

    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    if (gizmoOp == GizmoOp::Rotate) {
        operation = ImGuizmo::ROTATE;
    } else if (gizmoOp == GizmoOp::Scale) {
        operation = ImGuizmo::SCALE;
    }
    const ImGuizmo::MODE mode = gizmoWorldSpace ? ImGuizmo::WORLD : ImGuizmo::LOCAL;

    // Capture the pre-edit transform before Manipulate mutates `world`.
    const Transform preEdit = registry->transforms.get(selectedEntity).transform;

    if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), operation, mode, glm::value_ptr(world))) {
        // Convert the manipulated world matrix back into a local transform,
        // accounting for the parent's world matrix.
        glm::mat4 parentWorld(1.0f);
        if (registry->hierarchy.has(selectedEntity)) {
            const Entity parent = registry->hierarchy.get(selectedEntity).parent;
            if (parent != INVALID_ENTITY && registry->transforms.has(parent)) {
                parentWorld = getWorldMatrix(parent, *registry);
            }
        }

        const glm::mat4 local = glm::inverse(parentWorld) * world;
        glm::vec3 translation(0.0f);
        glm::vec3 scale(1.0f);
        glm::quat rotation;
        if (decomposeMatrix(local, translation, rotation, scale)) {
            Transform& transform = registry->transforms.get(selectedEntity).transform;
            transform.position = translation;
            transform.rotation = glm::normalize(rotation);
            transform.scale = scale;
        }
    }

    // Record one undo command for the whole manipulation (first drag .. release).
    if (ImGuizmo::IsUsing()) {
        if (!gizmoEditing) {
            gizmoEditing = true;
            gizmoEditBefore = preEdit;
        }
    } else if (gizmoEditing) {
        gizmoEditing = false;
        commandHistory.push(std::make_unique<TransformCommand>(
            selectedEntity, gizmoEditBefore, registry->transforms.get(selectedEntity).transform));
    }
}

void Renderer::selectSingle(Entity entity) {
    selectedEntities.clear();
    if (entity != INVALID_ENTITY) {
        selectedEntities.push_back(entity);
    }
    selectedEntity = entity;
}

void Renderer::toggleSelect(Entity entity) {
    if (entity == INVALID_ENTITY) {
        return;
    }
    const auto it = std::find(selectedEntities.begin(), selectedEntities.end(), entity);
    if (it != selectedEntities.end()) {
        selectedEntities.erase(it);
        selectedEntity = selectedEntities.empty() ? INVALID_ENTITY : selectedEntities.back();
    } else {
        selectedEntities.push_back(entity);
        selectedEntity = entity;
    }
}

bool Renderer::isSelected(Entity entity) const {
    return std::find(selectedEntities.begin(), selectedEntities.end(), entity) != selectedEntities.end();
}

void Renderer::duplicateSelectedEntity() {
    if (registry == nullptr || selectedEntities.empty()) {
        return;
    }

    // One transaction = one undo step for the whole multi-select duplicate.
    commandHistory.beginTransaction();
    std::vector<Entity> newRoots;
    for (Entity entity : selectedEntities) {
        if (!registry->transforms.has(entity)) {
            continue;
        }
        const std::string data = SceneSerializer::savePrefabToString(*registry, entity);
        if (data.empty()) {
            continue;
        }
        Entity parent = INVALID_ENTITY;
        if (registry->hierarchy.has(entity)) {
            parent = registry->hierarchy.get(entity).parent;
        }
        std::vector<Entity> created;
        const Entity newRoot = SceneSerializer::instantiateFromString(*registry, data, &created);
        if (newRoot == INVALID_ENTITY) {
            continue;
        }
        if (parent != INVALID_ENTITY) {
            try {
                registry->setParent(newRoot, parent);
            } catch (...) {
            }
        }
        commandHistory.push(std::make_unique<CreateSubtreeCommand>(data, parent, created));
        newRoots.push_back(newRoot);
    }
    commandHistory.commitTransaction(*registry);

    if (newRoots.empty()) {
        editorStatusMessage = "Duplicate failed.";
        return;
    }

    selectedEntities = newRoots;
    selectedEntity = newRoots.back();
    descriptorRefreshRequested = true;
    editorStatusMessage = "Duplicated " + std::to_string(newRoots.size()) + " entity(s).";
}

void Renderer::deleteSelectedEntities() {
    if (registry == nullptr || selectedEntities.empty()) {
        return;
    }

    commandHistory.beginTransaction();
    for (Entity entity : selectedEntities) {
        if (!registry->transforms.has(entity)) {
            continue; // already gone (e.g. destroyed as an ancestor's descendant)
        }
        const std::string data = SceneSerializer::savePrefabToString(*registry, entity);
        std::vector<Entity> order;
        SceneSerializer::collectSubtreeEntities(*registry, entity, order); // record id order for remap
        Entity parent = INVALID_ENTITY;
        if (registry->hierarchy.has(entity)) {
            parent = registry->hierarchy.get(entity).parent;
        }
        destroyEntitySubtree(*registry, entity);
        if (!data.empty()) {
            commandHistory.push(std::make_unique<DeleteSubtreeCommand>(data, parent, order));
        }
    }
    commandHistory.commitTransaction(*registry);

    selectedEntities.clear();
    selectedEntity = INVALID_ENTITY;
    descriptorRefreshRequested = true;
    editorStatusMessage = "Deleted selection.";
}

void Renderer::clearEditorState() {
    commandHistory.clear();
    selectedEntity = INVALID_ENTITY;
    selectedEntities.clear();
    gizmoEditing = false;
    inspectorEditing = false;
    transformCache.entity = INVALID_ENTITY;
}

void Renderer::drawPlayControls() {
    ImGui::Begin("Play Controls");

    if (app == nullptr) {
        ImGui::TextUnformatted("No application bound.");
        ImGui::End();
        return;
    }

    const EngineState state = app->getEngineState();
    const bool editing = state == EngineState::Edit;
    const bool playing = state == EngineState::Play;
    const bool paused = state == EngineState::Paused;

    // Play / Stop toggle.
    if (editing) {
        if (ImGui::Button("Play")) {
            app->play();
        }
    } else {
        if (ImGui::Button("Stop")) {
            app->stop();
        }
    }

    ImGui::SameLine();

    // Pause / Resume.
    if (paused) {
        if (ImGui::Button("Resume")) {
            app->resume();
        }
    } else {
        ImGui::BeginDisabled(!playing);
        if (ImGui::Button("Pause")) {
            app->pause();
        }
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    const char* label = editing ? "EDIT" : (paused ? "PAUSED" : "PLAYING");
    ImGui::Text("State: %s", label);
    ImGui::TextDisabled("F6 Play/Stop  |  F7 Pause/Resume");

    ImGui::End();
}

void Renderer::drawTimelinePanel() {
    ImGui::Begin("Timeline");

    if (app == nullptr) {
        ImGui::TextUnformatted("No application bound.");
        ImGui::End();
        return;
    }

    if (app->getEngineState() == EngineState::Edit) {
        ImGui::TextDisabled("Press Play to record a time-travel timeline.");
        ImGui::End();
        return;
    }

    const int count = app->getSnapshotCount();
    if (count <= 0) {
        ImGui::TextUnformatted("No frames recorded yet.");
        ImGui::End();
        return;
    }

    const bool scrubbing = app->isScrubbing();
    int cursor = scrubbing ? app->getScrubCursor() : (count - 1);

    if (scrubbing) {
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.20f, 1.0f), "SCRUBBING (paused)");
    } else {
        ImGui::TextColored(ImVec4(0.20f, 0.90f, 0.30f, 1.0f), "LIVE");
    }
    ImGui::SameLine();
    ImGui::Text("Frame %d / %d", cursor, count - 1);

    // Scrubber over the recorded frames — dragging it pauses and restores a
    // past frame for inspection (basic time-travel debugging).
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderInt("##timeline", &cursor, 0, count - 1)) {
        app->scrubTo(cursor);
    }

    if (ImGui::Button("|< Step")) {
        app->stepFrame(-1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Step >|")) {
        app->stepFrame(+1);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!scrubbing);
    if (ImGui::Button("Resume Live")) {
        app->resumeLive();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Scrub to inspect past frames; edits while scrubbing aren't kept.");

    ImGui::End();
}

void Renderer::drawHierarchyPanel() {
    ImGui::Begin("Hierarchy");

    if (registry == nullptr) {
        ImGui::TextUnformatted("No registry bound.");
        ImGui::End();
        return;
    }

    std::vector<Entity> roots;
    roots.reserve(registry->transforms.getAll().size());
    for (const auto& [entity, transformComponent] : registry->transforms.getAll()) {
        (void)transformComponent;
        if (!registry->hierarchy.has(entity) || registry->hierarchy.get(entity).parent == INVALID_ENTITY) {
            roots.push_back(entity);
        }
    }

    ReparentRequest reparent;

    std::sort(roots.begin(), roots.end());
    for (Entity root : roots) {
        drawEntityNodeRecursive(*registry, root, selectedEntity, selectedEntities, reparent);
    }

    // A drop zone filling the remaining panel space: dropping here unparents an
    // entity (makes it a root).
    const ImVec2 dropZone = ImGui::GetContentRegionAvail();
    ImGui::Dummy(ImVec2(std::max(dropZone.x, 1.0f), std::max(dropZone.y, 16.0f)));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE")) {
            reparent.child = *static_cast<const Entity*>(payload->Data);
            reparent.newParent = INVALID_ENTITY;
            reparent.pending = true;
        }
        ImGui::EndDragDropTarget();
    }

    // Apply any reparent after drawing so the hierarchy isn't mutated mid-walk.
    // setParent rejects cycles (parenting onto a descendant) by throwing.
    if (reparent.pending) {
        Entity oldParent = INVALID_ENTITY;
        if (registry->hierarchy.has(reparent.child)) {
            oldParent = registry->hierarchy.get(reparent.child).parent;
        }
        try {
            registry->setParent(reparent.child, reparent.newParent);
            commandHistory.push(std::make_unique<ReparentCommand>(reparent.child, oldParent, reparent.newParent));
            editorStatusMessage = "Reparented " + getEntityLabel(*registry, reparent.child);
        } catch (const std::exception& exception) {
            editorStatusMessage = std::string("Reparent failed: ") + exception.what();
        }
    }

    ImGui::End();
}

void Renderer::drawInspectorPanel() {
    ImGui::Begin("Inspector");

    if (registry == nullptr || selectedEntity == INVALID_ENTITY || !registry->transforms.has(selectedEntity)) {
        ImGui::TextUnformatted("Select an entity to inspect.");
        ImGui::End();
        return;
    }

    if (selectedEntities.size() > 1) {
        ImGui::Text("%zu entities selected (editing %u)",
                    selectedEntities.size(), static_cast<unsigned>(selectedEntity));
    } else {
        ImGui::Text("Entity: %u", selectedEntity);
    }
    ImGui::Separator();

    if (registry->names.has(selectedEntity)) {
        ImGui::Text("Name: %s", registry->names.get(selectedEntity).name.c_str());
    }

    auto& transform = registry->transforms.get(selectedEntity).transform;
    // Records an undo command spanning one drag of the just-drawn transform widget.
    auto recordTransformEdit = [&]() {
        if (ImGui::IsItemActivated() && !inspectorEditing) {
            inspectorEditing = true;
            inspectorEditBefore = transform; // value is still pre-edit on the activation frame
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && inspectorEditing) {
            inspectorEditing = false;
            commandHistory.push(std::make_unique<TransformCommand>(selectedEntity, inspectorEditBefore, transform));
        }
    };

    ImGui::DragFloat3("Position", &transform.position.x, 0.05f);
    recordTransformEdit();

    // Rotation is a quaternion; expose it as editable Euler degrees via a cache.
    // Re-derive the displayed Euler only when the selection changes or the quat
    // is modified outside the inspector (gizmo/undo); otherwise keep the user's
    // in-progress Euler so dragging near ±90° doesn't flip (see EditorTransformCache).
    const bool quatChangedExternally =
        std::abs(glm::dot(transform.rotation, transformCache.quat)) < 0.99999f;
    if (transformCache.entity != selectedEntity || quatChangedExternally) {
        transformCache.entity = selectedEntity;
        transformCache.quat = transform.rotation;
        transformCache.eulerDegrees = glm::degrees(eulerXyzFromQuat(transform.rotation));
    }
    if (ImGui::DragFloat3("Rotation", &transformCache.eulerDegrees.x, 0.5f)) {
        transform.rotation = quatFromEulerXYZ(glm::radians(transformCache.eulerDegrees));
        transformCache.quat = transform.rotation;
    }
    recordTransformEdit();

    ImGui::DragFloat3("Scale", &transform.scale.x, 0.05f, 0.01f, 100.0f);
    recordTransformEdit();

    if (registry->meshes.has(selectedEntity)) {
        ImGui::Separator();
        ImGui::Text("Mesh Handle: %llu", static_cast<unsigned long long>(registry->meshes.get(selectedEntity).mesh));
    }

    if (registry->materials.has(selectedEntity)) {
        auto& material = registry->materials.get(selectedEntity).material;
        ImGui::Text("Texture Handle: %llu", static_cast<unsigned long long>(material.albedo));
        ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f);
        ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f);
        ImGui::SliderFloat("AO", &material.ao, 0.0f, 1.0f);
    }

    if (registry->scripts.has(selectedEntity)) {
        ImGui::Separator();
        ImGui::TextUnformatted("Script");
        auto& script = registry->scripts.get(selectedEntity);
        char behaviorBuffer[128];
        std::snprintf(behaviorBuffer, sizeof(behaviorBuffer), "%s", script.behavior.c_str());
        if (ImGui::InputText("Behavior", behaviorBuffer, sizeof(behaviorBuffer))) {
            script.behavior = behaviorBuffer;
        }
        if (ImGui::SmallButton("Remove##script")) {
            const Entity e = selectedEntity;
            const ScriptComponent saved = registry->scripts.get(e);
            registry->scripts.remove(e);
            commandHistory.push(std::make_unique<LambdaCommand>(
                [e](Registry& r) { r.scripts.remove(e); },
                [e, saved](Registry& r) { r.scripts.add(e, saved); }));
        }
    }

    if (registry->rigidBodies.has(selectedEntity)) {
        ImGui::Separator();
        ImGui::TextUnformatted("Rigid Body");
        auto& body = registry->rigidBodies.get(selectedEntity);
        ImGui::DragFloat3("Velocity", &body.velocity.x, 0.05f);
        ImGui::DragFloat("Mass", &body.mass, 0.05f, 0.0f, 1000.0f);
        ImGui::SliderFloat("Restitution", &body.restitution, 0.0f, 1.0f);
        ImGui::SliderFloat("Friction", &body.friction, 0.0f, 1.0f);
        ImGui::Checkbox("Use Gravity", &body.useGravity);
        ImGui::SameLine();
        ImGui::Checkbox("Static", &body.isStatic);
        if (ImGui::SmallButton("Remove##rigidbody")) {
            const Entity e = selectedEntity;
            const RigidBodyComponent saved = registry->rigidBodies.get(e);
            registry->rigidBodies.remove(e);
            commandHistory.push(std::make_unique<LambdaCommand>(
                [e](Registry& r) { r.rigidBodies.remove(e); },
                [e, saved](Registry& r) { r.rigidBodies.add(e, saved); }));
        }
    }

    if (registry->colliders.has(selectedEntity)) {
        ImGui::Separator();
        ImGui::TextUnformatted("Collider");
        auto& collider = registry->colliders.get(selectedEntity);
        int colliderType = static_cast<int>(collider.type);
        const char* colliderTypeNames[] = { "Box", "Sphere" };
        if (ImGui::Combo("Shape", &colliderType, colliderTypeNames, 2)) {
            collider.type = static_cast<ColliderType>(colliderType);
        }
        if (collider.type == ColliderType::Box) {
            ImGui::DragFloat3("Half Extents", &collider.halfExtents.x, 0.05f, 0.0f, 100.0f);
        } else {
            ImGui::DragFloat("Radius", &collider.radius, 0.05f, 0.0f, 100.0f);
        }
        if (ImGui::SmallButton("Remove##collider")) {
            const Entity e = selectedEntity;
            const ColliderComponent saved = registry->colliders.get(e);
            registry->colliders.remove(e);
            commandHistory.push(std::make_unique<LambdaCommand>(
                [e](Registry& r) { r.colliders.remove(e); },
                [e, saved](Registry& r) { r.colliders.add(e, saved); }));
        }
    }

    if (registry->audioSources.has(selectedEntity)) {
        ImGui::Separator();
        ImGui::TextUnformatted("Audio Source");
        auto& source = registry->audioSources.get(selectedEntity);
        const auto clip = ResourceManager::getAudioClip(source.clip);
        ImGui::Text("Clip: %s", clip ? clip->getResourceKey().c_str() : "(none)");
        ImGui::TextDisabled("Drop an audio file below to set the clip.");
        ImGui::SliderFloat("Volume", &source.volume, 0.0f, 1.0f);
        ImGui::DragFloat("Pitch", &source.pitch, 0.01f, 0.1f, 4.0f);
        ImGui::Checkbox("Loop", &source.loop);
        ImGui::SameLine();
        ImGui::Checkbox("Play On Start", &source.playOnStart);
        ImGui::Checkbox("Spatial (3D)", &source.spatial);
    } else if (ImGui::Button("Add Audio Source")) {
        registry->audioSources.add(selectedEntity, {});
    }

    if (registry->audioListeners.has(selectedEntity)) {
        ImGui::Separator();
        ImGui::TextUnformatted("Audio Listener");
        auto& listener = registry->audioListeners.get(selectedEntity);
        ImGui::SliderFloat("Master Gain", &listener.gain, 0.0f, 1.0f);
        if (ImGui::SmallButton("Remove##listener")) {
            const Entity e = selectedEntity;
            const AudioListenerComponent saved = registry->audioListeners.get(e);
            registry->audioListeners.remove(e);
            commandHistory.push(std::make_unique<LambdaCommand>(
                [e](Registry& r) { r.audioListeners.remove(e); },
                [e, saved](Registry& r) { r.audioListeners.add(e, saved); }));
        }
    }

    // Add a value-only component (resource-bearing ones — Mesh/Material/AudioSource
    // — are assigned via drag-drop). Each add/remove is a single undo step.
    ImGui::Separator();
    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    if (ImGui::BeginPopup("AddComponentPopup")) {
        const Entity e = selectedEntity;
        if (!registry->rigidBodies.has(e) && ImGui::Selectable("Rigid Body")) {
            registry->rigidBodies.add(e, {});
            commandHistory.push(std::make_unique<LambdaCommand>(
                [e](Registry& r) { if (!r.rigidBodies.has(e)) r.rigidBodies.add(e, {}); },
                [e](Registry& r) { r.rigidBodies.remove(e); }));
        }
        if (!registry->colliders.has(e) && ImGui::Selectable("Collider")) {
            registry->colliders.add(e, {});
            commandHistory.push(std::make_unique<LambdaCommand>(
                [e](Registry& r) { if (!r.colliders.has(e)) r.colliders.add(e, {}); },
                [e](Registry& r) { r.colliders.remove(e); }));
        }
        if (!registry->scripts.has(e) && ImGui::Selectable("Script")) {
            registry->scripts.add(e, {});
            commandHistory.push(std::make_unique<LambdaCommand>(
                [e](Registry& r) { if (!r.scripts.has(e)) r.scripts.add(e, {}); },
                [e](Registry& r) { r.scripts.remove(e); }));
        }
        if (!registry->audioListeners.has(e) && ImGui::Selectable("Audio Listener")) {
            registry->audioListeners.add(e, {});
            commandHistory.push(std::make_unique<LambdaCommand>(
                [e](Registry& r) { if (!r.audioListeners.has(e)) r.audioListeners.add(e, {}); },
                [e](Registry& r) { r.audioListeners.remove(e); }));
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
    if (ImGui::Button("Save as Prefab")) {
        try {
            std::filesystem::create_directories("assets/prefabs");
            const std::string prefabName = registry->names.has(selectedEntity)
                ? registry->names.get(selectedEntity).name
                : ("Entity_" + std::to_string(selectedEntity));
            const std::string prefabPath = "assets/prefabs/" + prefabName + ".prefab";
            if (SceneSerializer::savePrefab(*registry, selectedEntity, prefabPath)) {
                editorStatusMessage = "Saved prefab: " + prefabPath;
                if (assetRegistry != nullptr) {
                    assetRegistry->scan("assets");
                }
            } else {
                editorStatusMessage = "Failed to save prefab.";
            }
        } catch (const std::exception& exception) {
            editorStatusMessage = exception.what();
        }
    }

    // Prefab instance link: show the source and allow reverting overrides by
    // respawning the entity subtree from the prefab.
    if (registry->prefabInstances.has(selectedEntity)) {
        const std::string prefabPath = registry->prefabInstances.get(selectedEntity).prefab;
        ImGui::TextDisabled("Prefab: %s", prefabPath.c_str());

        // Revert: pull the prefab's state back over this instance (respawn).
        if (ImGui::Button("Revert to Prefab")) {
            std::vector<Entity> toDestroy;
            std::vector<Entity> stack{ selectedEntity };
            while (!stack.empty()) {
                const Entity current = stack.back();
                stack.pop_back();
                toDestroy.push_back(current);
                if (registry->hierarchy.has(current)) {
                    for (Entity child : registry->hierarchy.get(current).children) {
                        stack.push_back(child);
                    }
                }
            }
            for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it) {
                registry->destroyEntity(*it);
            }

            const Entity newRoot = SceneSerializer::instantiatePrefab(*registry, prefabPath);
            if (newRoot != INVALID_ENTITY) {
                registry->prefabInstances.add(newRoot, { prefabPath });
                selectSingle(newRoot);
                editorStatusMessage = "Reverted to prefab: " + prefabPath;
            } else {
                selectSingle(INVALID_ENTITY);
                editorStatusMessage = "Revert failed: " + prefabPath;
            }
            commandHistory.clear(); // ids changed; history would dangle
            descriptorRefreshRequested = true;
        }

        // Apply: push this instance's current state (overrides included) back to
        // the prefab file, so it becomes the new template for future instances.
        ImGui::SameLine();
        if (ImGui::Button("Apply to Prefab")) {
            if (SceneSerializer::savePrefab(*registry, selectedEntity, prefabPath)) {
                editorStatusMessage = "Applied overrides to prefab: " + prefabPath;
                if (assetRegistry != nullptr) {
                    assetRegistry->scan("assets");
                }
            } else {
                editorStatusMessage = "Apply failed: " + prefabPath;
            }
        }
        ImGui::TextDisabled("Per-field override tracking is a later refinement.");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Drop .obj/.gltf/.glb, image, or audio assets here.");
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            const std::string assetPath(
                static_cast<const char*>(payload->Data),
                payload->DataSize > 0 ? payload->DataSize - 1 : 0
            );
            const std::string extension = toLower(std::filesystem::path(assetPath).extension().string());

            try {
                if (extension == ".obj" || extension == ".gltf" || extension == ".glb") {
                    const AssetHandle newMeshHandle = ResourceManager::loadMesh(assetPath);
                    AssetHandle oldMeshHandle = INVALID_HANDLE;

                    if (registry->meshes.has(selectedEntity)) {
                        oldMeshHandle = registry->meshes.get(selectedEntity).mesh;
                        registry->meshes.get(selectedEntity).mesh = newMeshHandle;
                    } else {
                        registry->meshes.add(selectedEntity, {newMeshHandle});
                    }

                    if (oldMeshHandle != INVALID_HANDLE) {
                        deferredResourceReleases.push_back(oldMeshHandle);
                    }

                    editorStatusMessage = "Assigned mesh: " + assetPath;
                } else if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") {
                    const AssetHandle newTextureHandle = ResourceManager::loadTexture(assetPath);
                    AssetHandle oldTextureHandle = INVALID_HANDLE;

                    if (registry->materials.has(selectedEntity)) {
                        oldTextureHandle = registry->materials.get(selectedEntity).material.albedo;
                        registry->materials.get(selectedEntity).material.albedo = newTextureHandle;
                    } else {
                        Material material{};
                        material.albedo = newTextureHandle;
                        registry->materials.add(selectedEntity, {material});
                    }

                    if (oldTextureHandle != INVALID_HANDLE) {
                        deferredResourceReleases.push_back(oldTextureHandle);
                    }

                    descriptorRefreshRequested = true;
                    editorStatusMessage = "Assigned texture: " + assetPath;
                } else if (extension == ".wav" || extension == ".mp3" ||
                           extension == ".flac" || extension == ".ogg") {
                    const AssetHandle newClipHandle = ResourceManager::loadAudioClip(assetPath);
                    AssetHandle oldClipHandle = INVALID_HANDLE;
                    if (registry->audioSources.has(selectedEntity)) {
                        oldClipHandle = registry->audioSources.get(selectedEntity).clip;
                        registry->audioSources.get(selectedEntity).clip = newClipHandle;
                    } else {
                        AudioSourceComponent source{};
                        source.clip = newClipHandle;
                        registry->audioSources.add(selectedEntity, source);
                    }

                    if (oldClipHandle != INVALID_HANDLE) {
                        deferredResourceReleases.push_back(oldClipHandle);
                    }

                    editorStatusMessage = "Assigned audio clip: " + assetPath;
                } else {
                    editorStatusMessage = "Unsupported asset drop: " + assetPath;
                }
            } catch (const std::exception& exception) {
                editorStatusMessage = exception.what();
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (!editorStatusMessage.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", editorStatusMessage.c_str());
    }

    ImGui::End();
}

void Renderer::drawAssetBrowserPanel() {
    ImGui::Begin("Assets");

    if (assetRegistry == nullptr) {
        ImGui::TextUnformatted("No asset registry bound.");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Drag a tile onto an entity. Double-click a prefab to "
                        "instantiate, a model to import.");
    ImGui::Separator();

    // Color-coded thumbnail grid. (Live image/mesh render previews are a later
    // graphics-side refinement; the tile color encodes the asset type for now.)
    constexpr float tileSize = 84.0f;
    const float available = ImGui::GetContentRegionAvail().x;
    const int perRow = std::max(1, static_cast<int>(available / (tileSize + 8.0f)));
    int column = 0;

    for (const auto& asset : assetRegistry->getAssets()) {
        const std::string& ext = asset.extension;
        const bool isPrefab = ext == ".prefab";
        const bool isModel = ext == ".gltf" || ext == ".glb" || ext == ".obj";
        const bool isTexture = ext == ".png" || ext == ".jpg" || ext == ".jpeg";
        const bool isAudio = ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".ogg";
        if (!isPrefab && !isModel && !isTexture && !isAudio) {
            continue;
        }

        ImVec4 tint(0.40f, 0.40f, 0.40f, 1.0f);
        const char* typeLabel = "FILE";
        if (isPrefab)       { tint = ImVec4(0.85f, 0.55f, 0.20f, 1.0f); typeLabel = "PREFAB"; }
        else if (isModel)   { tint = ImVec4(0.25f, 0.45f, 0.85f, 1.0f); typeLabel = "MESH"; }
        else if (isTexture) { tint = ImVec4(0.30f, 0.65f, 0.35f, 1.0f); typeLabel = "IMAGE"; }
        else if (isAudio)   { tint = ImVec4(0.55f, 0.35f, 0.75f, 1.0f); typeLabel = "AUDIO"; }

        std::string shortName = asset.name;
        if (shortName.size() > 11) {
            shortName = shortName.substr(0, 10) + "\xE2\x80\xA6"; // ellipsis
        }
        const std::string tileLabel = std::string(typeLabel) + "\n" + shortName + "##" + asset.path;

        ImGui::PushID(asset.path.c_str());
        ImGui::PushStyleColor(ImGuiCol_Button, tint);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(tint.x + 0.1f, tint.y + 0.1f, tint.z + 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(tint.x + 0.15f, tint.y + 0.15f, tint.z + 0.15f, 1.0f));
        ImGui::Button(tileLabel.c_str(), ImVec2(tileSize, tileSize));
        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\n%s", asset.name.c_str(), asset.path.c_str());
        }

        // Drag onto an entity to assign (handled by the inspector drop target).
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("ASSET_PATH", asset.path.c_str(), asset.path.size() + 1);
            ImGui::Text("%s", asset.name.c_str());
            ImGui::EndDragDropSource();
        }

        // Double-click: prefabs instantiate, models import (each generates/links a prefab).
        if (registry != nullptr && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (isPrefab) {
                const Entity instantiated = SceneSerializer::instantiatePrefab(*registry, asset.path);
                if (instantiated != INVALID_ENTITY) {
                    registry->prefabInstances.add(instantiated, { asset.path });
                    descriptorRefreshRequested = true;
                    editorStatusMessage = "Instantiated prefab: " + asset.name;
                } else {
                    editorStatusMessage = "Failed to instantiate prefab: " + asset.name;
                }
            } else if (isModel) {
                const Entity root = ModelImporter::importGltf(*registry, asset.path);
                if (root != INVALID_ENTITY) {
                    try {
                        std::filesystem::create_directories("assets/prefabs");
                        const std::string stem = std::filesystem::path(asset.path).stem().string();
                        const std::string prefabPath = "assets/prefabs/" + (stem.empty() ? "Model" : stem) + ".prefab";
                        if (SceneSerializer::savePrefab(*registry, root, prefabPath)) {
                            registry->prefabInstances.add(root, { prefabPath });
                            if (assetRegistry != nullptr) {
                                assetRegistry->scan("assets");
                            }
                            editorStatusMessage = "Imported " + asset.name + " -> " + prefabPath;
                        } else {
                            editorStatusMessage = "Imported " + asset.name + " (prefab save failed)";
                        }
                    } catch (const std::exception& exception) {
                        editorStatusMessage = exception.what();
                    }
                    descriptorRefreshRequested = true;
                } else {
                    editorStatusMessage = "Failed to import model: " + asset.name;
                }
            }
        }
        ImGui::PopID();

        if (++column % perRow != 0) {
            ImGui::SameLine();
        }
    }

    ImGui::End();
}

void Renderer::renderImGui(VkCommandBuffer cmd) {
    if (!imguiInitialized) {
        return;
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}
