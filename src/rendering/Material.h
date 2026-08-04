#pragma once

#include <glm/glm.hpp>

#include "assets/AssetHandle.h"

// How a material composites against the framebuffer. The seam every mature engine uses
// (Unreal EBlendMode; Unity URP Surface Type + Alpha Clip + blend mode): a per-material
// mode the renderer buckets and sorts on, so transparency is not a shader hack but a
// first-class property. See RULES.md Rule 22.
//   Opaque      - write depth, no blend. The default; every existing material.
//   Masked      - opaque queue, but discard texels with alpha < cutoff (crisp cutout;
//                 pixel-art sprites). Lit, depth-writing, order-independent.
//   Translucent - src.rgb*a + dst*(1-a). Glass, smoke, ghosts, UI fades. Drawn after
//                 opaque, back-to-front, no depth write.
//   Additive    - src + dst. Glows, fire, energy, many particles. Same ordering.
// Order matters: keep values contiguous with the two blended modes last so a
// "transparent" test is `mode >= Translucent` (see DrawList sorting).
enum class BlendMode : uint32_t {
    Opaque = 0,
    Masked = 1,
    Translucent = 2,
    Additive = 3,
};

struct Material {
    AssetHandle albedo = INVALID_HANDLE;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    // Flat colour tint multiplied into the sampled albedo. Default white = the
    // texture shows unchanged; pair with builtin://white for a solid flat colour.
    glm::vec3 baseColor = glm::vec3(1.0f);
    BlendMode blendMode = BlendMode::Opaque;
};

inline bool isBlended(BlendMode mode) {
    return mode == BlendMode::Translucent || mode == BlendMode::Additive;
}
