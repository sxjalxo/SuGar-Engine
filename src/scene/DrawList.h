#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <cstddef>
#include <memory>
#include <vector>
#include "assets/AssetHandle.h"
#include "rendering/Material.h"
#include "scene/Light.h"

class Registry;
class Mesh;

struct RenderItem {
    std::shared_ptr<Mesh> mesh;
    AssetHandle meshHandle = INVALID_HANDLE;
    Material material;
    glm::mat4 model{1.0f};

    // Phase 17C.2 — the pose this item is drawn in, one matrix per joint in the
    // skin's joint order. Empty means "not skinned", which is the common case.
    //
    // Derived, and recomputed every frame from ECS transforms + bind data
    // (Skinning::computeJointMatrices). It rides on the draw list — a per-frame
    // description of what to draw — precisely so the renderer never has to reach
    // into the ECS for a pose, and never becomes the thing that owns one.
    std::vector<glm::mat4> jointMatrices;
};

// DESIGN_RENDER_CPU_TIMING.md §9 -- what the last buildDrawListFromECS() call spent,
// in milliseconds, split so "drawList is 93 % of the render frame" can be attributed
// instead of merely stated (§6.2 already named `record` a suspect on adjacency and
// measured it flat).
//
// RAW per-frame numbers only: no rolling window, no median, no reporting. Those stay in
// SuGarApp, which already owns `drawListTiming` -- so `scene/` never learns what a
// TimingWindow is and this struct stays a plain value the caller may ignore.
//
// `total` is measured around the whole function, independently of the five parts, so
// `total - sum(parts)` is a real residual rather than an identity (same reasoning as
// §3 and DESIGN_SYSTEM_PROFILER.md §2).
//
// `skinnedItems`/`joints` are COUNTS, not a rate. Divide them into `skinning` if you
// like; this struct will not do it for you, because a derived ratio whose halves were
// measured at different moments is how DESIGN_SYSTEM_PROFILER.md §9.2 went wrong. Both
// counts here come from the same frame and the same bracket.
struct DrawListBuildTiming {
    double gatherMs = 0.0;   // collect transform entities + sort into deterministic order
    double itemsMs = 0.0;    // the per-entity loop, MINUS skinning
    double skinningMs = 0.0; // Skinning::computeJointMatrices(), accumulated
    double sortMs = 0.0;     // the render-queue sort over out.items
    double lightsMs = 0.0;   // the second pass building out.lights
    double totalMs = 0.0;    // the whole function, measured independently
    std::size_t skinnedItems = 0;
    std::size_t joints = 0;

    // DESIGN_RENDER_CPU_TIMING.md §11 -- work done inside skinning, counted rather than
    // timed, and attributed to skinning alone by taking RegistryWorkCounters deltas
    // around each computeJointMatrices call. `subtreeSearches` is also the number of
    // heap allocations findDescendantByName made, one `pending` vector per call.
    unsigned long long skinSubtreeSearches = 0;
    unsigned long long skinNodeVisits = 0;
    unsigned long long skinMatrixHops = 0;
};

struct DrawList {
    std::vector<RenderItem> items;
    std::vector<Light> lights;

    // Overwritten by every buildDrawListFromECS() call. Survives between calls because
    // the app owns one DrawList for the process lifetime.
    DrawListBuildTiming timing;
};

// cameraPosition orders the translucent/additive tail back-to-front (painter's order);
// opaque/masked draws are depth-sorted by the GPU and only batched by material here.
void buildDrawListFromECS(const Registry& registry, const std::vector<Light>& lights,
                          const glm::vec3& cameraPosition, DrawList& out);
