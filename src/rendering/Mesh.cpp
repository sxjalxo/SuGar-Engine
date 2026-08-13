#include "rendering/Mesh.h"

#include "rendering/DeviceMemoryPool.h"
#include "rendering/MeshUploadProfile.h"

#include <chrono>
#include <stdexcept>
#include <cstring>

Mesh Mesh::makeUnitCube() {
    Mesh mesh;
    // Six faces, four unique vertices each (per-face normals, so no sharing), two
    // triangles per face. Half-extent 0.5 → a unit cube centered on the origin.
    struct Face {
        float normal[3];
        float corners[4][3]; // CCW when viewed from outside
    };
    const float h = 0.5f;
    const Face faces[6] = {
        {{ 0, 0, 1}, {{-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}}}, // +Z
        {{ 0, 0,-1}, {{ h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h}}}, // -Z
        {{ 1, 0, 0}, {{ h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h}}}, // +X
        {{-1, 0, 0}, {{-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h}}}, // -X
        {{ 0, 1, 0}, {{-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h}}}, // +Y
        {{ 0,-1, 0}, {{-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h}}}, // -Y
    };
    const float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    for (const Face& face : faces) {
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (int i = 0; i < 4; ++i) {
            Vertex v{};
            v.pos[0] = face.corners[i][0];
            v.pos[1] = face.corners[i][1];
            v.pos[2] = face.corners[i][2];
            v.normal[0] = face.normal[0];
            v.normal[1] = face.normal[1];
            v.normal[2] = face.normal[2];
            v.uv[0] = uvs[i][0];
            v.uv[1] = uvs[i][1];
            mesh.vertices.push_back(v);
        }
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }
    return mesh;
}

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


namespace {
// Adds its own lifetime to one of the upload counters. Scoped rather than manual so an
// early return or a thrown exception cannot silently drop a phase from the total.
class PhaseTimer {
public:
    explicit PhaseTimer(double& sink) : sink_(sink), start_(std::chrono::steady_clock::now()) {}
    ~PhaseTimer() {
        sink_ += std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - start_).count();
    }
    PhaseTimer(const PhaseTimer&) = delete;
    PhaseTimer& operator=(const PhaseTimer&) = delete;

private:
    double& sink_;
    std::chrono::steady_clock::time_point start_;
};
} // namespace

// Pooled variant: same create/bind, but the memory is a placement inside a shared block
// instead of an allocation of its own. Used for the device-local vertex/index buffers,
// which are many, long-lived and all about the same size.
void createPooledBuffer(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    DeviceMemoryPool::Allocation& allocation
) {
    PhaseTimer timer(MeshUploadProfile::counters().bufferCreateMs);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mesh buffer!");
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);

    try {
        allocation = DeviceMemoryPool::allocate(device, physicalDevice, requirements, properties);
    } catch (...) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        throw;
    }

    if (vkBindBufferMemory(device, buffer, allocation.memory, allocation.offset) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        DeviceMemoryPool::free(allocation);
        throw std::runtime_error("failed to bind mesh buffer memory!");
    }
}

void destroyPooledBuffer(VkDevice device, VkBuffer& buffer,
                         DeviceMemoryPool::Allocation& allocation) {
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    DeviceMemoryPool::free(allocation);
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
    PhaseTimer timer(MeshUploadProfile::counters().bufferCreateMs);

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

    MeshUploadProfile::counters().deviceAllocations++;
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
    auto& profile = MeshUploadProfile::counters();
    auto commandStart = std::chrono::steady_clock::now();

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

    profile.commandMs += std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - commandStart).count();
    const auto submitStart = std::chrono::steady_clock::now();

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw std::runtime_error("failed to submit mesh copy command buffer!");
    }

    vkQueueWaitIdle(graphicsQueue);
    profile.submitWaitMs += std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - submitStart).count();

    commandStart = std::chrono::steady_clock::now();
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    profile.commandMs += std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - commandStart).count();
}


// --- Reused staging buffer --------------------------------------------------------------
// Every mesh upload used to create a host-visible staging buffer, copy through it, and
// destroy it again. Measured over a streaming run: creating those buffers was 28% of upload
// time and destroying them 23%, while the memcpy they exist for was 1.6%.
//
// One staging buffer is kept and reused, grown when a mesh needs more than it has. That is
// safe *because the copy is synchronous*: copyBuffer below ends in vkQueueWaitIdle, so the
// GPU is provably finished reading the staging buffer before this function returns and the
// next upload can refill it. If the submit is ever batched or deferred (the fence-contract
// change deliberately not made), this reuse becomes a use-after-free and must grow a ring
// plus per-slot fences. The invariant is stated here because it is the whole argument.
struct StagingBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize capacity = 0;
};
StagingBuffer g_staging;

// Grows geometrically so a run of slowly increasing chunk meshes does not reallocate on
// every upload; never shrinks, because the peak is what the next chunk will want too.
VkBuffer acquireStaging(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size,
                        VkDeviceMemory& outMemory) {
    if (g_staging.capacity >= size && g_staging.buffer != VK_NULL_HANDLE) {
        outMemory = g_staging.memory;
        return g_staging.buffer;
    }

    VkDeviceSize capacity = g_staging.capacity > 0 ? g_staging.capacity : (256u * 1024u);
    while (capacity < size) {
        capacity *= 2;
    }

    {
        PhaseTimer destroyTimer(MeshUploadProfile::counters().destroyMs);
        destroyBuffer(device, g_staging.buffer, g_staging.memory);
    }
    g_staging.capacity = 0;

    createBuffer(device, physicalDevice, capacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 g_staging.buffer, g_staging.memory);
    g_staging.capacity = capacity;
    outMemory = g_staging.memory;
    return g_staging.buffer;
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
    DeviceMemoryPool::Allocation& memory
) {
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    const VkBuffer stagingBuffer = acquireStaging(device, physicalDevice, bufferSize, stagingMemory);

    try {
        {
            PhaseTimer mapTimer(MeshUploadProfile::counters().mapCopyMs);
            void* mappedData = nullptr;
            if (vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &mappedData) != VK_SUCCESS) {
                throw std::runtime_error("failed to map mesh staging buffer memory!");
            }

            std::memcpy(mappedData, sourceData, static_cast<size_t>(bufferSize));
            vkUnmapMemory(device, stagingMemory);
        }

        createPooledBuffer(
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
        // The staging buffer is shared and outlives this call; only the destination is
        // this upload's to clean up.
        destroyPooledBuffer(device, buffer, memory);
        throw;
    }

    MeshUploadProfile::counters().buffers++;
    MeshUploadProfile::counters().bytes += static_cast<uint64_t>(bufferSize);
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

void Mesh::shutdownUploadResources(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    destroyBuffer(device, g_staging.buffer, g_staging.memory);
    g_staging.capacity = 0;
    DeviceMemoryPool::shutdown(device);
}

void Mesh::destroy(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return;
    }

    destroyPooledBuffer(device, indexBuffer, indexMemory);
    destroyPooledBuffer(device, vertexBuffer, vertexMemory);
    uploaded = false;
}
