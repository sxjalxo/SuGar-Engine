#pragma once

// What each RmlUi clip-mask operation does to the coverage mask
// (DevDocs/DESIGN_UI_CLIP_MASK.md).
//
// Split out from the Vulkan code because this is the half that can be silently wrong and
// the half a test can reach: getting `SetInverse` backwards produces a shadow that covers
// everything *except* the element, which still renders, still validates, and is still
// completely wrong. The Vulkan state around it belongs to the device and is verified by
// measuring the actual frame.
//
// RmlUi's own enum is mirrored rather than included so this stays testable without
// pulling RmlUi into the test binary; RmlVulkanRenderer translates at the boundary.

namespace ClipMask {

enum class Op {
    Set,        // coverage becomes exactly the geometry
    SetInverse, // coverage becomes everything *outside* the geometry
    Intersect,  // coverage becomes (old AND geometry)
};

struct Plan {
    // Every operation starts from a known full-target value: the mask is a texture, so
    // "everything not drawn this call" has to be defined explicitly.
    float clearValue = 0.0f;

    // What the geometry writes. Constant for Set/SetInverse; ignored when sampling.
    float writeValue = 1.0f;

    // Intersect must write what the PREVIOUS mask held, so the area inside the geometry
    // keeps its old coverage and everything else is the cleared 0 — exactly "old AND
    // new". Reading and writing one image is a feedback loop, so this is also the flag
    // that makes the two mask targets ping-pong.
    bool samplePrevious = false;
};

// `hasExistingMask` is false for the first mask of a sequence: an Intersect with nothing
// to intersect against is just a Set, and treating it as a sample would read an
// undefined target.
inline Plan plan(Op op, bool hasExistingMask) {
    Plan result;
    switch (op) {
        case Op::Set:
            result.clearValue = 0.0f;
            result.writeValue = 1.0f;
            break;
        case Op::SetInverse:
            result.clearValue = 1.0f;
            result.writeValue = 0.0f;
            break;
        case Op::Intersect:
            result.clearValue = 0.0f;
            result.writeValue = 1.0f;
            result.samplePrevious = hasExistingMask;
            break;
    }
    return result;
}

} // namespace ClipMask
