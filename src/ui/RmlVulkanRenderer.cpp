#include "ui/RmlVulkanRenderer.h"

#include "rendering/Texture.h"

#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

std::vector<char> readSpirv(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open shader: " + filename);
    }
    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("failed to create RmlUi shader module");
    }
    return module;
}

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type for RmlUi buffer");
}

// Push constants mirror shaders/rml.vert.
struct RmlPushConstants {
    float viewport[2];
    float translation[2];
};

} // namespace

void RmlVulkanRenderer::init(VkDevice deviceIn, VkPhysicalDevice physicalDeviceIn, VkCommandPool commandPoolIn,
                             VkQueue graphicsQueueIn, VkRenderPass renderPass) {
    device = deviceIn;
    physicalDevice = physicalDeviceIn;
    commandPool = commandPoolIn;
    graphicsQueue = graphicsQueueIn;

    createDescriptorResources();
    createPipeline(renderPass);
    createWhiteTexture();
}

void RmlVulkanRenderer::createDescriptorResources() {
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create RmlUi descriptor set layout");
    }

    // One set per texture: fonts atlases + any images referenced from RCSS.
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 128;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 128;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create RmlUi descriptor pool");
    }
}

void RmlVulkanRenderer::createPipeline(VkRenderPass renderPass) {
    auto vertCode = readSpirv("build/shaders/rml.vert.spv");
    auto fragCode = readSpirv("build/shaders/rml.frag.spv");
    VkShaderModule vertModule = createShaderModule(device, vertCode);
    VkShaderModule fragModule = createShaderModule(device, fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // Rml::Vertex { Vector2f position; ColourbPremultiplied colour; Vector2f tex_coord; }
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Rml::Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[3]{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset = offsetof(Rml::Vertex, position);
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R8G8B8A8_UNORM; // premultiplied colour
    attributes[1].offset = offsetof(Rml::Vertex, colour);
    attributes[2].location = 2;
    attributes[2].binding = 0;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = offsetof(Rml::Vertex, tex_coord);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // UI geometry winding is not guaranteed
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Premultiplied alpha.
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    // The scene render pass (which the runtime UI draws into, over the game image)
    // has a depth attachment, so a depth-stencil state is required. UI is a flat
    // overlay: never test or write depth.
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(RmlPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create RmlUi pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create RmlUi graphics pipeline");
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
}

void RmlVulkanRenderer::createWhiteTexture() {
    auto texture = std::make_unique<Texture>();
    const std::vector<uint8_t> white = {255, 255, 255, 255};
    texture->createFromPixels(device, physicalDevice, commandPool, graphicsQueue, white, 1, 1);
    whiteTextureHandle = registerTexture(std::move(texture));
}

VkDescriptorSet RmlVulkanRenderer::createDescriptorSet(const Texture& texture) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device, &allocInfo, &set) != VK_SUCCESS) {
        std::cerr << "[RmlUi] failed to allocate descriptor set\n";
        return VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = texture.getImageView();
    imageInfo.sampler = texture.getSampler();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return set;
}

Rml::TextureHandle RmlVulkanRenderer::registerTexture(std::unique_ptr<Texture> texture) {
    if (!texture || !texture->isReady()) {
        return 0;
    }
    VkDescriptorSet set = createDescriptorSet(*texture);
    if (set == VK_NULL_HANDLE) {
        texture->destroy(device);
        return 0;
    }
    const uintptr_t handle = nextTextureHandle++;
    textures[handle] = TextureEntry{std::move(texture), set};
    return static_cast<Rml::TextureHandle>(handle);
}

bool RmlVulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer,
                                     VkDeviceMemory& memory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, requirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(device, buffer, memory, 0);
    return true;
}

void RmlVulkanRenderer::beginFrame(VkCommandBuffer commandBuffer, VkExtent2D extent) {
    currentCommandBuffer = commandBuffer;
    currentExtent = extent;
    ++frameCounter;
    collectRetiredGeometry(false); // free buffers no longer referenced in flight
}

void RmlVulkanRenderer::destroyGeometry(Geometry& geometry) {
    vkDestroyBuffer(device, geometry.vertexBuffer, nullptr);
    vkFreeMemory(device, geometry.vertexMemory, nullptr);
    vkDestroyBuffer(device, geometry.indexBuffer, nullptr);
    vkFreeMemory(device, geometry.indexMemory, nullptr);
    geometry = {};
}

void RmlVulkanRenderer::collectRetiredGeometry(bool force) {
    for (auto it = retiredGeometries.begin(); it != retiredGeometries.end();) {
        const bool safe = force || (frameCounter - it->retiredAtFrame) > FramesInFlightMargin;
        if (safe) {
            destroyGeometry(it->geometry);
            it = retiredGeometries.erase(it);
        } else {
            ++it;
        }
    }
}

Rml::CompiledGeometryHandle RmlVulkanRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                               Rml::Span<const int> indices) {
    if (vertices.empty() || indices.empty()) {
        return 0;
    }

    Geometry geometry{};
    geometry.indexCount = static_cast<uint32_t>(indices.size());

    const VkDeviceSize vertexSize = sizeof(Rml::Vertex) * vertices.size();
    const VkDeviceSize indexSize = sizeof(int) * indices.size();

    if (!createBuffer(vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, geometry.vertexBuffer, geometry.vertexMemory) ||
        !createBuffer(indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, geometry.indexBuffer, geometry.indexMemory)) {
        std::cerr << "[RmlUi] failed to allocate geometry buffers\n";
        return 0;
    }

    void* mapped = nullptr;
    vkMapMemory(device, geometry.vertexMemory, 0, vertexSize, 0, &mapped);
    std::memcpy(mapped, vertices.data(), static_cast<size_t>(vertexSize));
    vkUnmapMemory(device, geometry.vertexMemory);

    vkMapMemory(device, geometry.indexMemory, 0, indexSize, 0, &mapped);
    std::memcpy(mapped, indices.data(), static_cast<size_t>(indexSize));
    vkUnmapMemory(device, geometry.indexMemory);

    const uintptr_t handle = nextGeometryHandle++;
    geometries[handle] = geometry;
    return static_cast<Rml::CompiledGeometryHandle>(handle);
}

void RmlVulkanRenderer::RenderGeometry(Rml::CompiledGeometryHandle geometryHandle, Rml::Vector2f translation,
                                       Rml::TextureHandle textureHandle) {
    if (currentCommandBuffer == VK_NULL_HANDLE || pipeline == VK_NULL_HANDLE) {
        return;
    }
    const auto geometryIt = geometries.find(static_cast<uintptr_t>(geometryHandle));
    if (geometryIt == geometries.end()) {
        return;
    }

    // Untextured geometry (texture == 0) samples the 1x1 white texture.
    const uintptr_t resolvedTexture =
        textureHandle == 0 ? static_cast<uintptr_t>(whiteTextureHandle) : static_cast<uintptr_t>(textureHandle);
    const auto textureIt = textures.find(resolvedTexture);
    if (textureIt == textures.end() || textureIt->second.descriptorSet == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(currentExtent.width);
    viewport.height = static_cast<float>(currentExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(currentCommandBuffer, 0, 1, &viewport);

    RmlPushConstants push{};
    push.viewport[0] = static_cast<float>(currentExtent.width);
    push.viewport[1] = static_cast<float>(currentExtent.height);
    push.translation[0] = translation.x;
    push.translation[1] = translation.y;
    vkCmdPushConstants(currentCommandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

    vkCmdBindDescriptorSets(currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                            &textureIt->second.descriptorSet, 0, nullptr);

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(currentCommandBuffer, 0, 1, &geometryIt->second.vertexBuffer, &offset);
    vkCmdBindIndexBuffer(currentCommandBuffer, geometryIt->second.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(currentCommandBuffer, geometryIt->second.indexCount, 1, 0, 0, 0);
}

void RmlVulkanRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometryHandle) {
    const auto it = geometries.find(static_cast<uintptr_t>(geometryHandle));
    if (it == geometries.end()) {
        return;
    }
    // Do NOT destroy now: RmlUi drops geometry during a re-layout, and these buffers
    // may still be referenced by command buffers in flight. Retire and free later.
    retiredGeometries.push_back(RetiredGeometry{it->second, frameCounter});
    geometries.erase(it);
}

Rml::TextureHandle RmlVulkanRenderer::LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(source.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr) {
        std::cerr << "[RmlUi] failed to load texture: " << source << "\n";
        return 0;
    }

    const std::vector<uint8_t> data(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);

    auto texture = std::make_unique<Texture>();
    texture->createFromPixels(device, physicalDevice, commandPool, graphicsQueue, data,
                              static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    textureDimensions = Rml::Vector2i(width, height);
    return registerTexture(std::move(texture));
}

Rml::TextureHandle RmlVulkanRenderer::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                      Rml::Vector2i sourceDimensions) {
    // This is how font atlases arrive from the FreeType engine: raw RGBA8 bytes.
    if (source.empty() || sourceDimensions.x <= 0 || sourceDimensions.y <= 0) {
        return 0;
    }
    const std::vector<uint8_t> data(source.data(), source.data() + source.size());
    auto texture = std::make_unique<Texture>();
    texture->createFromPixels(device, physicalDevice, commandPool, graphicsQueue, data,
                              static_cast<uint32_t>(sourceDimensions.x), static_cast<uint32_t>(sourceDimensions.y));
    return registerTexture(std::move(texture));
}

void RmlVulkanRenderer::ReleaseTexture(Rml::TextureHandle textureHandle) {
    const auto it = textures.find(static_cast<uintptr_t>(textureHandle));
    if (it == textures.end()) {
        return;
    }
    if (it->second.descriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device, descriptorPool, 1, &it->second.descriptorSet);
    }
    if (it->second.texture) {
        it->second.texture->destroy(device);
    }
    textures.erase(it);
}

void RmlVulkanRenderer::EnableScissorRegion(bool enable) {
    if (currentCommandBuffer == VK_NULL_HANDLE) {
        return;
    }
    if (!enable) {
        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = currentExtent;
        vkCmdSetScissor(currentCommandBuffer, 0, 1, &scissor);
    }
    // When enabling, RmlUi always follows with SetScissorRegion.
}

void RmlVulkanRenderer::SetScissorRegion(Rml::Rectanglei region) {
    if (currentCommandBuffer == VK_NULL_HANDLE) {
        return;
    }
    // Clamp to the framebuffer: Vulkan rejects scissors outside the render area.
    const int left = std::max(0, region.Left());
    const int top = std::max(0, region.Top());
    const int right = std::min(static_cast<int>(currentExtent.width), region.Left() + region.Width());
    const int bottom = std::min(static_cast<int>(currentExtent.height), region.Top() + region.Height());

    VkRect2D scissor{};
    scissor.offset = {left, top};
    scissor.extent = {static_cast<uint32_t>(std::max(0, right - left)),
                      static_cast<uint32_t>(std::max(0, bottom - top))};
    vkCmdSetScissor(currentCommandBuffer, 0, 1, &scissor);
}

void RmlVulkanRenderer::shutdown() {
    if (device == VK_NULL_HANDLE) {
        return;
    }

    // Callers idle the device before shutdown, so everything is safe to free now.
    collectRetiredGeometry(true);
    for (auto& [handle, geometry] : geometries) {
        (void)handle;
        destroyGeometry(geometry);
    }
    geometries.clear();

    for (auto& [handle, entry] : textures) {
        (void)handle;
        if (entry.texture) {
            entry.texture->destroy(device);
        }
    }
    textures.clear();

    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr); // frees its sets
        descriptorPool = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    device = VK_NULL_HANDLE;
}
