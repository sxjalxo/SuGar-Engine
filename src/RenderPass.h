#pragma once

#include <vulkan/vulkan.h>

class RenderPass {
public:
    virtual ~RenderPass() = default;
    
    virtual void setup() = 0;
    virtual void execute(VkCommandBuffer cmd, uint32_t imageIndex) = 0;
};