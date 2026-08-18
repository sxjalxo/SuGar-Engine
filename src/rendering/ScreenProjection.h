#pragma once

#include <glm/glm.hpp>

// World point -> viewport pixel. The one piece of real math behind world-space labels
// (DevDocs/DESIGN_RUNTIME_UI.md addendum), kept in Core and pure so it is testable without a
// device — the renderer supplies the matrices it already has and nothing else.
namespace ScreenProjection {

struct Result {
    float x = 0.0f;      // viewport pixels, origin top-left (the UI context's space)
    float y = 0.0f;
    float distance = 0.0f; // eye-space depth, for distance fade and nearest-N selection
};

// Projects `world` through `viewProjection` into a `width` x `height` viewport.
//
// Returns false when the point is behind the camera or exactly on the near plane — the
// case that, unguarded, divides by ~0 and puts a label from behind you at a plausible
// on-screen position (the classic mirrored-marker bug). Points off the sides still return
// true with coordinates outside the viewport; whether to clamp or cull is the caller's
// policy, not this function's.
inline bool project(const glm::mat4& viewProjection, const glm::vec3& world, float width,
                    float height, Result& out) {
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
    if (!(clip.w > 1e-6f)) {
        return false; // behind the camera (or a degenerate matrix: NaN fails this too)
    }

    const glm::vec3 ndc(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
    // Vulkan NDC: x right, y DOWN, so y maps straight through without a flip.
    out.x = (ndc.x * 0.5f + 0.5f) * width;
    out.y = (ndc.y * 0.5f + 0.5f) * height;
    out.distance = clip.w;
    return true;
}

} // namespace ScreenProjection
