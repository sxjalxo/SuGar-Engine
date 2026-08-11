#include "ui/RmlVulkanRenderer.h"

#include "rendering/Texture.h"

#include <RmlUi/Core/Variant.h>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
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
    createOffscreenResources();
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

namespace {
// Fragment push constants for rml_composite.frag.
struct CompositePush {
    float texelSize[2];
    float sigma;
    int radius;
};
} // namespace

void RmlVulkanRenderer::createOffscreenResources() {
    colorFormat = VK_FORMAT_R8G8B8A8_UNORM; // premultiplied UI colour; no sRGB double-convert

    // --- offscreen render pass: colour-only, LOAD/STORE, stays COLOR_ATTACHMENT --------
    VkAttachmentDescription color{};
    color.format = colorFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &color;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
    rpInfo.pDependencies = deps.data();
    if (vkCreateRenderPass(device, &rpInfo, nullptr, &offscreenPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create RmlUi offscreen render pass");
    }

    // --- sampler for sampling layers / saved textures -----------------------------------
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &layerSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create RmlUi layer sampler");
    }

    // --- offscreen UI pipeline: same as the layer-0 pipeline but into offscreenPass and
    //     with no depth attachment (offscreen layers are colour-only) ---------------------
    {
        auto vertCode = readSpirv("build/shaders/rml.vert.spv");
        auto fragCode = readSpirv("build/shaders/rml.frag.spv");
        VkShaderModule vertModule = createShaderModule(device, vertCode);
        VkShaderModule fragModule = createShaderModule(device, fragCode);
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule; stages[1].pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0; binding.stride = sizeof(Rml::Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attributes[3]{};
        attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Rml::Vertex, position)};
        attributes[1] = {1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Rml::Vertex, colour)};
        attributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Rml::Vertex, tex_coord)};
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 3;
        vertexInput.pVertexAttributeDescriptions = attributes;
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &blend;
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vertexInput; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rs; pi.pMultisampleState = &ms;
        pi.pColorBlendState = &cb; pi.pDynamicState = &ds;
        pi.layout = pipelineLayout;      // shares the layer-0 UI layout (viewport+translation)
        pi.renderPass = offscreenPass; pi.subpass = 0;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &offscreenUiPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create RmlUi offscreen UI pipeline");
        }
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
    }

    // --- composite/blur pipeline: fullscreen triangle sampling one layer onto another ---
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(CompositePush);
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &compositePipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create RmlUi composite pipeline layout");
        }

        auto vertCode = readSpirv("build/shaders/rml_fullscreen.vert.spv");
        auto fragCode = readSpirv("build/shaders/rml_composite.frag.spv");
        VkShaderModule vertModule = createShaderModule(device, vertCode);
        VkShaderModule fragModule = createShaderModule(device, fragCode);
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertModule; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragModule; stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo vertexInput{}; // no vertex buffer
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};
        blend.blendEnable = VK_TRUE; // premultiplied "over": composite source onto dest
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &blend;
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dstate{};
        dstate.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dstate.dynamicStateCount = 2; dstate.pDynamicStates = dyn;
        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vertexInput; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rs; pi.pMultisampleState = &ms;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dstate;
        pi.layout = compositePipelineLayout; pi.renderPass = offscreenPass; pi.subpass = 0;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &compositePipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create RmlUi composite pipeline");
        }
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
    }
}

bool RmlVulkanRenderer::createColorTarget(VkExtent2D extent, VkImage& image, VkDeviceMemory& memory,
                                          VkImageView& view) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = colorFormat;
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = 1; imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        return false;
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device, image, &req);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = findMemoryType(physicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &alloc, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyImage(device, image, nullptr); image = VK_NULL_HANDLE; return false;
    }
    vkBindImageMemory(device, image, memory, 0);
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = colorFormat;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        vkFreeMemory(device, memory, nullptr); vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE; memory = VK_NULL_HANDLE; return false;
    }
    return true;
}

VkDescriptorSet RmlVulkanRenderer::createDescriptorSetForView(VkImageView view, VkSampler sampler) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device, &allocInfo, &set) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = view;
    imageInfo.sampler = sampler;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set; write.dstBinding = 0; write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return set;
}

int RmlVulkanRenderer::acquireLayer() {
    for (size_t i = 0; i < layerPool.size(); ++i) {
        LayerTarget& t = layerPool[i];
        if (!t.inUse && t.extent.width == currentExtent.width && t.extent.height == currentExtent.height) {
            t.inUse = true;
            t.layout = VK_IMAGE_LAYOUT_UNDEFINED; // treated as fresh (cleared on first pass)
            return static_cast<int>(i);
        }
    }
    // None free at the right size: create a new target.
    LayerTarget t{};
    if (!createColorTarget(currentExtent, t.image, t.memory, t.view)) {
        return -1;
    }
    VkFramebufferCreateInfo fb{};
    fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb.renderPass = offscreenPass;
    fb.attachmentCount = 1; fb.pAttachments = &t.view;
    fb.width = currentExtent.width; fb.height = currentExtent.height; fb.layers = 1;
    if (vkCreateFramebuffer(device, &fb, nullptr, &t.framebuffer) != VK_SUCCESS) {
        return -1;
    }
    t.sampleSet = createDescriptorSetForView(t.view, layerSampler);
    t.extent = currentExtent;
    t.inUse = true;
    t.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    layerPool.push_back(t);
    return static_cast<int>(layerPool.size() - 1);
}

void RmlVulkanRenderer::beginLayerPass(int layerIndex, bool clear) {
    LayerTarget& t = layerPool[layerIndex];

    // Move the target into COLOR_ATTACHMENT from wherever it was (fresh, or sampled).
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = t.layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = t.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    vkCmdPipelineBarrier(currentCommandBuffer,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    t.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = offscreenPass;
    begin.framebuffer = t.framebuffer;
    begin.renderArea.offset = {0, 0};
    begin.renderArea.extent = t.extent;
    begin.clearValueCount = 0;
    vkCmdBeginRenderPass(currentCommandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
    renderPassActive = true;
    activePassLayer = layerIndex;

    VkViewport viewport{};
    viewport.x = 0.0f; viewport.y = 0.0f;
    viewport.width = static_cast<float>(t.extent.width);
    viewport.height = static_cast<float>(t.extent.height);
    viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
    vkCmdSetViewport(currentCommandBuffer, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.offset = {0, 0}; scissor.extent = t.extent;
    vkCmdSetScissor(currentCommandBuffer, 0, 1, &scissor);

    if (clear) {
        VkClearAttachment clearAtt{};
        clearAtt.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clearAtt.colorAttachment = 0;
        clearAtt.clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkClearRect clearRect{};
        clearRect.rect.offset = {0, 0}; clearRect.rect.extent = t.extent;
        clearRect.baseArrayLayer = 0; clearRect.layerCount = 1;
        vkCmdClearAttachments(currentCommandBuffer, 1, &clearAtt, 1, &clearRect);
    }
}

void RmlVulkanRenderer::transitionLayerToSampled(int layerIndex) {
    LayerTarget& t = layerPool[layerIndex];
    if (t.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        return;
    }
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = t.layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = t.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(currentCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    t.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void RmlVulkanRenderer::destroyLayerTargets() {
    for (LayerTarget& t : layerPool) {
        if (t.sampleSet != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(device, descriptorPool, 1, &t.sampleSet);
        }
        if (t.framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, t.framebuffer, nullptr);
        if (t.view != VK_NULL_HANDLE) vkDestroyImageView(device, t.view, nullptr);
        if (t.image != VK_NULL_HANDLE) vkDestroyImage(device, t.image, nullptr);
        if (t.memory != VK_NULL_HANDLE) vkFreeMemory(device, t.memory, nullptr);
    }
    layerPool.clear();
}

// --- RmlUi effect interface ------------------------------------------------------------

Rml::LayerHandle RmlVulkanRenderer::PushLayer() {
    endActivePass();
    const int idx = acquireLayer();
    if (idx < 0) {
        return Rml::LayerHandle{}; // out of memory; RmlUi degrades to no effect
    }
    beginLayerPass(idx, /*clear=*/true);
    layerStack.push_back(idx);
    return static_cast<Rml::LayerHandle>(idx + 1); // +1 so handle is never 0
}

void RmlVulkanRenderer::PopLayer() {
    endActivePass();
    if (!layerStack.empty()) {
        layerStack.pop_back();
    }
}

void RmlVulkanRenderer::CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination,
                                        Rml::BlendMode /*blendMode*/,
                                        Rml::Span<const Rml::CompiledFilterHandle> filters) {
    const int srcIdx = static_cast<int>(source) - 1;
    const int dstIdx = static_cast<int>(destination) - 1;
    if (srcIdx < 0 || dstIdx < 0 || srcIdx >= (int)layerPool.size() || dstIdx >= (int)layerPool.size()) {
        return;
    }
    endActivePass();
    transitionLayerToSampled(srcIdx);
    beginLayerPass(dstIdx, /*clear=*/false); // composite onto dest, preserving it

    CompositePush push{};
    push.texelSize[0] = 1.0f / static_cast<float>(currentExtent.width);
    push.texelSize[1] = 1.0f / static_cast<float>(currentExtent.height);
    push.sigma = 0.0f;
    push.radius = 0;
    for (const Rml::CompiledFilterHandle f : filters) {
        const auto it = filters_lookup(f);
        if (it >= 0.0f) {
            push.sigma = it;
            push.radius = std::min(8, std::max(1, static_cast<int>(std::ceil(2.0f * it))));
            break; // one blur filter is all box-shadow uses
        }
    }

    vkCmdBindPipeline(currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipeline);
    vkCmdBindDescriptorSets(currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipelineLayout,
                            0, 1, &layerPool[srcIdx].sampleSet, 0, nullptr);
    vkCmdPushConstants(currentCommandBuffer, compositePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(push), &push);
    vkCmdDraw(currentCommandBuffer, 3, 1, 0, 0);
    endActivePass();
}

Rml::TextureHandle RmlVulkanRenderer::SaveLayerAsTexture() {
    endActivePass();
    if (layerStack.empty()) {
        return 0;
    }
    const int idx = layerStack.back();
    // The saved texture is exactly the current scissor region of the layer, copied into a
    // persistent image (RmlUi caches box-shadow textures across frames; the layer pool is
    // transient). Fall back to the full layer if no scissor was set.
    VkOffset2D srcOffset = lastScissorOffset;
    VkExtent2D size = lastScissorExtent;
    if (size.width == 0 || size.height == 0) {
        srcOffset = {0, 0};
        size = currentExtent;
    }

    VkImage dstImage = VK_NULL_HANDLE;
    VkDeviceMemory dstMemory = VK_NULL_HANDLE;
    VkImageView dstView = VK_NULL_HANDLE;
    if (!createColorTarget(size, dstImage, dstMemory, dstView)) {
        return 0;
    }

    transitionLayerToSampled(idx); // ensures a defined layout; move src to TRANSFER_SRC next
    LayerTarget& src = layerPool[idx];
    // src: SHADER_READ -> TRANSFER_SRC.
    VkImageMemoryBarrier b0{};
    b0.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b0.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b0.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b0.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b0.image = src.image; b0.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b0.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; b0.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    // dst: UNDEFINED -> TRANSFER_DST.
    VkImageMemoryBarrier b1{};
    b1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b1.image = dstImage; b1.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b1.srcAccessMask = 0; b1.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    VkImageMemoryBarrier pre[2] = {b0, b1};
    vkCmdPipelineBarrier(currentCommandBuffer,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, pre);
    src.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkImageCopy copy{};
    copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.srcOffset = {srcOffset.x, srcOffset.y, 0};
    copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.dstOffset = {0, 0, 0};
    copy.extent = {size.width, size.height, 1};
    vkCmdCopyImage(currentCommandBuffer, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    // dst: TRANSFER_DST -> SHADER_READ for sampling as the box-shadow texture.
    VkImageMemoryBarrier b2{};
    b2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b2.image = dstImage; b2.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(currentCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b2);

    VkDescriptorSet set = createDescriptorSetForView(dstView, layerSampler);
    if (set == VK_NULL_HANDLE) {
        vkDestroyImageView(device, dstView, nullptr);
        vkFreeMemory(device, dstMemory, nullptr);
        vkDestroyImage(device, dstImage, nullptr);
        return 0;
    }
    const uintptr_t handle = nextTextureHandle++;
    TextureEntry entry{};
    entry.rawImage = dstImage; entry.rawMemory = dstMemory; entry.rawView = dstView;
    entry.descriptorSet = set;
    textures[handle] = std::move(entry);
    return static_cast<Rml::TextureHandle>(handle);
}

Rml::CompiledFilterHandle RmlVulkanRenderer::CompileFilter(const Rml::String& name,
                                                           const Rml::Dictionary& parameters) {
    // Only "blur" is honoured (box-shadow's soft edge). Other filters compile to a no-op
    // handle so the layer simply composites unfiltered rather than failing.
    BlurFilter filter{};
    if (name == "blur") {
        const auto it = parameters.find("sigma");
        if (it != parameters.end()) {
            filter.sigma = it->second.Get<float>(0.0f);
        }
    }
    const uintptr_t handle = nextFilterHandle++;
    filters[handle] = filter;
    return static_cast<Rml::CompiledFilterHandle>(handle);
}

void RmlVulkanRenderer::ReleaseFilter(Rml::CompiledFilterHandle filter) {
    filters.erase(static_cast<uintptr_t>(filter));
}

// Returns the sigma of a compiled blur filter, or -1 if the handle is not a blur.
float RmlVulkanRenderer::filters_lookup(uintptr_t handle) const {
    const auto it = filters.find(handle);
    if (it == filters.end() || it->second.sigma <= 0.0f) {
        return -1.0f;
    }
    return it->second.sigma;
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
    TextureEntry entry{};
    entry.texture = std::move(texture);
    entry.descriptorSet = set;
    textures[handle] = std::move(entry);
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

void RmlVulkanRenderer::beginFrame(VkCommandBuffer commandBuffer, VkExtent2D extent,
                                   VkFramebuffer framebuffer, VkRenderPass layerPass,
                                   VkImage sceneImageIn) {
    currentCommandBuffer = commandBuffer;
    currentExtent = extent;
    layer0Pass = layerPass;
    layer0Framebuffer = framebuffer;
    sceneImage = sceneImageIn;
    ++frameCounter;
    collectRetiredGeometry(false); // free buffers no longer referenced in flight
    collectRetiredTextures(false); // and the textures/descriptors released with them

    // Compositor state resets each frame. No pass is opened yet: the first RenderGeometry
    // (via ensureActivePass) opens layer 0; effect methods open/close offscreen passes.
    layerStack.clear();
    activePassLayer = -2;
    renderPassActive = false;
    for (LayerTarget& t : layerPool) {
        t.inUse = false;
    }
    // A defined scissor before the first draw; RmlUi resets it via EnableScissorRegion.
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = currentExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void RmlVulkanRenderer::endFrame() {
    endActivePass();

    // The scene image is left in COLOR_ATTACHMENT by the UI passes (see uiLayerPass);
    // publish the UI's writes and hand it to the ImGui sampler as SHADER_READ_ONLY.
    if (currentCommandBuffer != VK_NULL_HANDLE && sceneImage != VK_NULL_HANDLE) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = sceneImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(currentCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    currentCommandBuffer = VK_NULL_HANDLE;
}

void RmlVulkanRenderer::endActivePass() {
    if (renderPassActive && currentCommandBuffer != VK_NULL_HANDLE) {
        vkCmdEndRenderPass(currentCommandBuffer);
        renderPassActive = false;
    }
    activePassLayer = -2;
}

void RmlVulkanRenderer::ensureActivePass() {
    if (renderPassActive) {
        return;
    }
    const int target = layerStack.empty() ? -1 : layerStack.back();
    if (target < 0) {
        // Layer 0: the scene image, LOAD (preserve scene + prior UI), no clear.
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = layer0Pass;
        begin.framebuffer = layer0Framebuffer;
        begin.renderArea.offset = {0, 0};
        begin.renderArea.extent = currentExtent;
        begin.clearValueCount = 0;
        vkCmdBeginRenderPass(currentCommandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
        renderPassActive = true;
        activePassLayer = -1;
    } else {
        beginLayerPass(target, false);
    }
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

    ensureActivePass(); // opens layer 0 (or the current offscreen layer) lazily
    // The offscreen render pass differs from layer 0's (colour-only, no depth), so it
    // needs the pipeline built against it; both share the layout, shaders and blend.
    const VkPipeline activeUiPipeline = (activePassLayer >= 0) ? offscreenUiPipeline : pipeline;
    vkCmdBindPipeline(currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, activeUiPipeline);

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

void RmlVulkanRenderer::destroyTexture(TextureEntry& entry) {
    if (entry.descriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device, descriptorPool, 1, &entry.descriptorSet);
        entry.descriptorSet = VK_NULL_HANDLE;
    }
    if (entry.texture) {
        entry.texture->destroy(device);
        entry.texture.reset();
    }
    // Compositor-saved layer copies own their image/view/memory directly.
    if (entry.rawView != VK_NULL_HANDLE) { vkDestroyImageView(device, entry.rawView, nullptr); entry.rawView = VK_NULL_HANDLE; }
    if (entry.rawImage != VK_NULL_HANDLE) { vkDestroyImage(device, entry.rawImage, nullptr); entry.rawImage = VK_NULL_HANDLE; }
    if (entry.rawMemory != VK_NULL_HANDLE) { vkFreeMemory(device, entry.rawMemory, nullptr); entry.rawMemory = VK_NULL_HANDLE; }
}

void RmlVulkanRenderer::collectRetiredTextures(bool force) {
    for (auto it = retiredTextures.begin(); it != retiredTextures.end();) {
        const bool safe = force || (frameCounter - it->retiredAtFrame) > FramesInFlightMargin;
        if (safe) {
            destroyTexture(it->entry);
            it = retiredTextures.erase(it);
        } else {
            ++it;
        }
    }
}

void RmlVulkanRenderer::ReleaseTexture(Rml::TextureHandle textureHandle) {
    const auto it = textures.find(static_cast<uintptr_t>(textureHandle));
    if (it == textures.end()) {
        return;
    }
    // Retire rather than destroy: the descriptor set and image may still be referenced by
    // a command buffer in flight (see RetiredTexture in the header).
    retiredTextures.push_back(RetiredTexture{ std::move(it->second), frameCounter });
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
        lastScissorOffset = scissor.offset;
        lastScissorExtent = scissor.extent;
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
    lastScissorOffset = scissor.offset;
    lastScissorExtent = scissor.extent;
}

void RmlVulkanRenderer::shutdown() {
    if (device == VK_NULL_HANDLE) {
        return;
    }

    // Callers idle the device before shutdown, so everything is safe to free now.
    collectRetiredGeometry(true);
    collectRetiredTextures(true);
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
        if (entry.rawView != VK_NULL_HANDLE) vkDestroyImageView(device, entry.rawView, nullptr);
        if (entry.rawImage != VK_NULL_HANDLE) vkDestroyImage(device, entry.rawImage, nullptr);
        if (entry.rawMemory != VK_NULL_HANDLE) vkFreeMemory(device, entry.rawMemory, nullptr);
    }
    textures.clear();

    destroyLayerTargets();
    if (compositePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, compositePipeline, nullptr);
        compositePipeline = VK_NULL_HANDLE;
    }
    if (compositePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, compositePipelineLayout, nullptr);
        compositePipelineLayout = VK_NULL_HANDLE;
    }
    if (offscreenUiPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, offscreenUiPipeline, nullptr);
        offscreenUiPipeline = VK_NULL_HANDLE;
    }
    if (offscreenPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, offscreenPass, nullptr);
        offscreenPass = VK_NULL_HANDLE;
    }
    if (layerSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, layerSampler, nullptr);
        layerSampler = VK_NULL_HANDLE;
    }

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
