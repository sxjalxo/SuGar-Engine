#include "rendering/Mesh.h"
#include <stdexcept>
#include <cstring>

namespace {
uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type for mesh buffer!");
}

void destroyBuffer(VkDevice device, VkBuffer& buffer, VkDeviceMemory& memory) {
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }

    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

void createBuffer(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& memory
) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mesh buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        physicalDevice,
        memRequirements.memoryTypeBits,
        properties
    );

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        throw std::runtime_error("failed to allocate mesh buffer memory!");
    }

    if (vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS) {
        destroyBuffer(device, buffer, memory);
        throw std::runtime_error("failed to bind mesh buffer memory!");
    }
}

void copyBuffer(
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkBuffer srcBuffer,
    VkBuffer dstBuffer,
    VkDeviceSize size
) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate mesh copy command buffer!");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw std::runtime_error("failed to begin mesh copy command buffer!");
    }

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw std::runtime_error("failed to record mesh copy command buffer!");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw std::runtime_error("failed to submit mesh copy command buffer!");
    }

    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void uploadToDeviceLocalBuffer(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    const void* sourceData,
    VkDeviceSize bufferSize,
    VkBufferUsageFlags finalUsage,
    VkBuffer& buffer,
    VkDeviceMemory& memory
) {
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    try {
        createBuffer(
            device,
            physicalDevice,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingMemory
        );

        void* mappedData = nullptr;
        if (vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &mappedData) != VK_SUCCESS) {
            throw std::runtime_error("failed to map mesh staging buffer memory!");
        }

        std::memcpy(mappedData, sourceData, static_cast<size_t>(bufferSize));
        vkUnmapMemory(device, stagingMemory);

        createBuffer(
            device,
            physicalDevice,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | finalUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            buffer,
            memory
        );

        copyBuffer(device, commandPool, graphicsQueue, stagingBuffer, buffer, bufferSize);
    } catch (...) {
        destroyBuffer(device, stagingBuffer, stagingMemory);
        destroyBuffer(device, buffer, memory);
        throw;
    }

    destroyBuffer(device, stagingBuffer, stagingMemory);
}
} // namespace

void Mesh::upload(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue
) {
    if (vertices.empty() || indices.empty()) {
        throw std::runtime_error("mesh upload requires both vertex and index data!");
    }

    if (uploaded) {
        return;
    }

    destroy(device);

    try {
        const VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();
        uploadToDeviceLocalBuffer(
            device,
            physicalDevice,
            commandPool,
            graphicsQueue,
            vertices.data(),
            vertexBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            vertexBuffer,
            vertexMemory
        );

        const VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();
        uploadToDeviceLocalBuffer(
            device,
            physicalDevice,
            commandPool,
            graphicsQueue,
            indices.data(),
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            indexBuffer,
            indexMemory
        );

        uploaded = true;
    } catch (...) {
        destroy(device);
        throw;
    }
}

void Mesh::destroy(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return;
    }

    destroyBuffer(device, indexBuffer, indexMemory);
    destroyBuffer(device, vertexBuffer, vertexMemory);
    uploaded = false;
}
