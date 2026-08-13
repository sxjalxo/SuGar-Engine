#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

// Suballocates device-local buffer memory out of a few large blocks.
//
// Every runtime mesh used to own two whole `vkAllocateMemory` allocations (vertices and
// indices). A streaming voxel world measured 10 371 of them over one run, and buffer
// creation — the create/allocate/bind triple — was 27% of upload time after the staging
// buffer was reused. Vulkan documents per-resource allocation as the thing not to do:
// `maxMemoryAllocationCount` is 4096 on some drivers, and allocation is a heavyweight
// driver call everywhere.
//
// **Scope, deliberately narrow.** This handles device-local *buffer* memory only. Staging
// memory has a different lifetime (one reused buffer, host-visible) and images have
// different placement rules (`bufferImageGranularity`), so neither goes through here. One
// clever abstraction over three lifetimes would be harder to reason about than three plain
// ones, and only this one has a measurement behind it.
//
// **Ownership is unchanged.** The pool owns the `VkDeviceMemory` blocks and the bookkeeping,
// nothing else. A Mesh still owns its `VkBuffer`s, and hands its Allocation back to the pool
// when it destroys them. ResourceManager remains the owner of the resources themselves.
namespace DeviceMemoryPool {

// A placed piece of a block. `memory` + `offset` is what vkBindBufferMemory wants.
struct Allocation {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    int block = -1;          // index into the pool's block list; -1 when unallocated
    uint32_t memoryType = 0;

    bool valid() const { return memory != VK_NULL_HANDLE && block >= 0; }
};

// Places `requirements` in a block of a compatible memory type, creating one if needed.
// Requests larger than half a block get their own block, so one big mesh cannot strand
// most of a shared block behind it. Throws on failure, like the rest of the upload path.
Allocation allocate(VkDevice device, VkPhysicalDevice physicalDevice,
                    const VkMemoryRequirements& requirements, VkMemoryPropertyFlags properties);

// Returns the space; adjacent free space is merged so a stream of same-sized chunk meshes
// reuses the same holes instead of walking off the end of the block.
void free(Allocation& allocation);

// Frees every block. Call after the last buffer bound to this memory is destroyed.
void shutdown(VkDevice device);

// Live blocks and how many bytes of them are in use — the numbers that say whether the
// pool is doing its job or quietly fragmenting.
struct Stats {
    uint64_t blocks = 0;
    uint64_t blockBytes = 0;
    uint64_t usedBytes = 0;
    uint64_t liveAllocations = 0;
};
Stats stats();

// The placement bookkeeping, separated from Vulkan so it can be tested without a device.
// This is the half that can be silently wrong — an overlap, a lost hole, an alignment that
// is not honoured — and none of it needs a GPU to check.
namespace detail {

struct FreeRun {
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
};

// First fit honouring `alignment`. Returns false when the runs cannot hold `size`.
// Alignment padding is left as a free run of its own rather than absorbed, so repeated
// aligned placements cannot leak the gaps between them.
bool place(std::vector<FreeRun>& runs, VkDeviceSize size, VkDeviceSize alignment,
           VkDeviceSize& outOffset);

// Returns [offset, offset+size) and merges it with any run it now touches. Runs stay
// sorted by offset, which is what makes merging a neighbour check instead of a search.
void release(std::vector<FreeRun>& runs, VkDeviceSize offset, VkDeviceSize size);

} // namespace detail

} // namespace DeviceMemoryPool
