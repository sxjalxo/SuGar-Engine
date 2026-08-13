#pragma once

#include <cstdint>
#include <string>

// Where a runtime-mesh upload's time goes.
//
// A streamed voxel world creates ~14 GPU meshes on every chunk boundary, and that upload
// measured 16.9 ms of a 35.7 ms crossing — the largest single phase. "Uploading is slow"
// is not something anyone can act on: the fix for a stall is different from the fix for an
// allocation storm, which is different again from the fix for a memcpy. So the path is
// split at the boundaries where those fixes would land.
//
// Always accumulating: a handful of clock reads against a millisecond-scale operation is
// not a measurable cost, and a counter that has to be switched on is a counter nobody has
// when they need it. Printing is opt-in (SUGAR_UPLOADLOG=1).
namespace MeshUploadProfile {

struct Counters {
    // Engine-side, before Vulkan is involved at all.
    double validateMs = 0.0;   // index-range + length checks on the caller's data
    double translateMs = 0.0;  // copy into the engine's Vertex layout

    // Vulkan.
    double bufferCreateMs = 0.0; // vkCreateBuffer + vkAllocateMemory + vkBindBufferMemory
    double mapCopyMs = 0.0;      // vkMapMemory + memcpy + vkUnmapMemory
    double commandMs = 0.0;      // allocate/begin/record/end/free the copy command buffer
    double submitWaitMs = 0.0;   // vkQueueSubmit + vkQueueWaitIdle
    double destroyMs = 0.0;      // tearing the staging buffer down again

    uint64_t meshes = 0;         // createRuntimeMesh calls that reached the upload
    uint64_t buffers = 0;        // device-local buffers filled (2 per mesh: vertex + index)
    uint64_t bytes = 0;          // payload bytes copied

    // vkAllocateMemory calls, and the driver's ceiling on *live* allocations. Every
    // buffer here owns a whole allocation, which is the pattern Vulkan explicitly warns
    // against: maxMemoryAllocationCount is as low as 4096 on some drivers, so this is a
    // scaling limit and not only a speed one.
    uint64_t deviceAllocations = 0;
    uint32_t maxAllocationsAllowed = 0;

    double totalMs() const {
        return validateMs + translateMs + bufferCreateMs + mapCopyMs + commandMs +
               submitWaitMs + destroyMs;
    }

    std::string describe() const;
};

// The process-wide accumulator. Not thread-safe on purpose: uploads happen on the thread
// that owns the device, and pretending otherwise would imply a guarantee that does not
// exist yet.
Counters& counters();
void reset();

} // namespace MeshUploadProfile
