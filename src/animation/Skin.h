#pragma once

#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

// Phase 17C — skin (bind-pose) data. Like AnimationClip, this is an **immutable
// asset, not state**: bind matrices never change at runtime, so nothing here is
// snapshotted.
//
// Note what is *absent*: there is no joint hierarchy, no per-joint transform, no
// current pose. Joints are **entities** — they were imported as entities in 17B and
// the AnimationSystem already poses them by writing TransformComponent. Storing a
// parallel joint array here would be a second representation of the same thing,
// able to disagree with the first after a snapshot restore: precisely the second
// owner RULES.md Rule 21 forbids. So a Skin carries only what the ECS *cannot*
// know — which entities are joints, in which order, and their inverse bind
// matrices.
//
//     Skinning = f(mesh, skeleton pose)
//
// where the pose lives in ECS transforms and the joint matrices are derived
// (Skinning.h). See docs/DESIGN_ANIMATION.md.
struct Skin {
    std::string name;

    // Joint entity names, in **joint-index order** — the order JOINTS_0 vertex
    // indices refer to. Names, not entity ids or glTF node indices: a name
    // round-trips through serialization and survives a re-export that reorders
    // nodes. Resolved against the skinned entity's root subtree.
    std::vector<std::string> joints;

    // Parallel to `joints`: the matrix taking a vertex from model space into each
    // joint's local space at bind time. Supplied by the asset — it is not
    // derivable from the current pose, which is exactly why it is stored.
    std::vector<glm::mat4> inverseBindMatrices;

    bool valid() const {
        return !joints.empty() && joints.size() == inverseBindMatrices.size();
    }
};
