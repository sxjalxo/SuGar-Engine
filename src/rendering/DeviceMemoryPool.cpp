#include "rendering/DeviceMemoryPool.h"

#include "rendering/MeshUploadProfile.h"

#include <algorithm>
#include <stdexcept>

namespace DeviceMemoryPool {
namespace {

constexpr VkDeviceSize kBlockSize = 32u * 1024u * 1024u;

using detail::FreeRun;

struct Block {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    uint32_t memoryType = 0;
    bool dedicated = false;      // one oversized allocation owns the whole block
    std::vector<FreeRun> freeRuns;
    uint64_t liveAllocations = 0;
};

std::vector<Block> g_blocks;

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                        VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type for pooled buffer memory!");
}

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) / alignment * alignment;
}

int createBlock(VkDevice device, uint32_t memoryType, VkDeviceSize size, bool dedicated) {
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = size;
    allocateInfo.memoryTypeIndex = memoryType;

    Block block;
    MeshUploadProfile::counters().deviceAllocations++;
    if (vkAllocateMemory(device, &allocateInfo, nullptr, &block.memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate a device memory block!");
    }
    block.size = size;
    block.memoryType = memoryType;
    block.dedicated = dedicated;
    block.freeRuns.push_back(FreeRun{0, size});
    g_blocks.push_back(std::move(block));
    return static_cast<int>(g_blocks.size()) - 1;
}

} // namespace


namespace detail {

bool place(std::vector<FreeRun>& runs, VkDeviceSize size, VkDeviceSize alignment,
           VkDeviceSize& outOffset) {
    // First fit rather than best fit: the workload is a stream of near-identical chunk
    // meshes, so the hole a departing chunk leaves is the size the next one wants.
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const VkDeviceSize start = alignUp(runs[i].offset, alignment);
        const VkDeviceSize padding = start - runs[i].offset;
        if (runs[i].size < padding || runs[i].size - padding < size) {
            continue;
        }

        const VkDeviceSize runEnd = runs[i].offset + runs[i].size;
        if (padding > 0) {
            runs[i].size = padding;
            if (start + size < runEnd) {
                runs.insert(runs.begin() + static_cast<long>(i) + 1,
                            FreeRun{start + size, runEnd - (start + size)});
            }
        } else if (start + size < runEnd) {
            runs[i].offset = start + size;
            runs[i].size = runEnd - (start + size);
        } else {
            runs.erase(runs.begin() + static_cast<long>(i));
        }

        outOffset = start;
        return true;
    }
    return false;
}

void release(std::vector<FreeRun>& runs, VkDeviceSize offset, VkDeviceSize size) {
    if (size == 0) return;
    const FreeRun run{offset, size};
    auto position = std::lower_bound(runs.begin(), runs.end(), run,
                                     [](const FreeRun& a, const FreeRun& b) {
                                         return a.offset < b.offset;
                                     });
    position = runs.insert(position, run);

    if (position + 1 != runs.end() && position->offset + position->size == (position + 1)->offset) {
        position->size += (position + 1)->size;
        runs.erase(position + 1);
    }
    if (position != runs.begin()) {
        auto previous = position - 1;
        if (previous->offset + previous->size == position->offset) {
            previous->size += position->size;
            runs.erase(position);
        }
    }
}

} // namespace detail

Allocation allocate(VkDevice device, VkPhysicalDevice physicalDevice,
                    const VkMemoryRequirements& requirements, VkMemoryPropertyFlags properties) {
    const uint32_t memoryType =
        findMemoryType(physicalDevice, requirements.memoryTypeBits, properties);

    Allocation allocation;
    allocation.size = requirements.size;
    allocation.memoryType = memoryType;

    // Big requests get a block to themselves rather than stranding the tail of a shared one.
    if (requirements.size > kBlockSize / 2) {
        const int index = createBlock(device, memoryType, requirements.size, true);
        g_blocks[static_cast<std::size_t>(index)].freeRuns.clear();
        g_blocks[static_cast<std::size_t>(index)].liveAllocations = 1;
        allocation.memory = g_blocks[static_cast<std::size_t>(index)].memory;
        allocation.offset = 0;
        allocation.block = index;
        return allocation;
    }

    for (std::size_t i = 0; i < g_blocks.size(); ++i) {
        Block& block = g_blocks[i];
        if (block.dedicated || block.memoryType != memoryType) continue;
        VkDeviceSize offset = 0;
        if (!detail::place(block.freeRuns, requirements.size, requirements.alignment, offset)) continue;
        block.liveAllocations++;
        allocation.memory = block.memory;
        allocation.offset = offset;
        allocation.block = static_cast<int>(i);
        return allocation;
    }

    const int index = createBlock(device, memoryType, kBlockSize, false);
    VkDeviceSize offset = 0;
    Block& block = g_blocks[static_cast<std::size_t>(index)];
    if (!detail::place(block.freeRuns, requirements.size, requirements.alignment, offset)) {
        throw std::runtime_error("a fresh memory block could not fit one allocation!");
    }
    block.liveAllocations++;
    allocation.memory = block.memory;
    allocation.offset = offset;
    allocation.block = index;
    return allocation;
}

void free(Allocation& allocation) {
    if (!allocation.valid() ||
        static_cast<std::size_t>(allocation.block) >= g_blocks.size()) {
        allocation = Allocation{};
        return;
    }

    Block& block = g_blocks[static_cast<std::size_t>(allocation.block)];
    if (block.liveAllocations > 0) block.liveAllocations--;

    if (!block.dedicated) {
        detail::release(block.freeRuns, allocation.offset, allocation.size);
    }

    // Blocks are kept, not returned to the driver: this is a streaming workload, and the
    // block that just emptied is the one the next chunk will fill.
    allocation = Allocation{};
}

void shutdown(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        g_blocks.clear();
        return;
    }
    for (Block& block : g_blocks) {
        if (block.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, block.memory, nullptr);
            block.memory = VK_NULL_HANDLE;
        }
    }
    g_blocks.clear();
}

Stats stats() {
    Stats result;
    for (const Block& block : g_blocks) {
        result.blocks++;
        result.blockBytes += block.size;
        result.liveAllocations += block.liveAllocations;
        VkDeviceSize freeBytes = 0;
        for (const FreeRun& run : block.freeRuns) freeBytes += run.size;
        result.usedBytes += block.size - freeBytes;
    }
    return result;
}

} // namespace DeviceMemoryPool
