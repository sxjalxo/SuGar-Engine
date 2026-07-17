#pragma once

#include <vector>

#include <glm/mat4x4.hpp>

#include "ecs/Entity.h"

class Registry;
struct Skin;

// Phase 17C — joint matrices. This is the whole of skinning's CPU side, and it is
// deliberately **not** a system: it writes no components and owns no state.
//
//     Skinning = f(mesh, skeleton pose)
//
// Joint matrices are *derived* — recomputed from ECS transforms plus the skin's
// bind data whenever the renderer wants them, and never stored, never serialized.
// The renderer is a **consumer**: GPU skinning is an implementation detail of
// drawing, not a new home for animation state. Nothing here would need to change
// to skin on the CPU instead.
//
// It lives in Core because it is pure math over the registry — no Vulkan — which is
// what keeps it headless-testable (Rule 9, Rule 15). See docs/DESIGN_ANIMATION.md.
namespace Skinning {

// Fills `out` with one matrix per joint, in the skin's joint-index order — the
// order JOINTS_0 vertex indices refer to. Returns false (leaving `out` empty) if
// the skin is malformed or no joint resolved, so a caller can fall back to
// unskinned drawing rather than render a collapsed mesh.
//
// Each joint matrix is:
//
//     inverse(world(skinnedEntity)) * world(joint[i]) * inverseBind[i]
//
// The leading inverse cancels the skinned entity's own world transform. glTF says
// a skinned mesh's node transform must be ignored (skinned vertices are already in
// scene space); cancelling it here means the renderer can keep applying its
// ordinary per-entity model matrix and get the same result — so moving the
// character entity moves the character, and skinned meshes need no special case in
// the draw path.
//
// A joint whose name resolves to nothing gets identity: one missing bone leaves the
// rest of the character posed correctly rather than collapsing it to the origin.
bool computeJointMatrices(const Registry& registry,
                          Entity skinnedEntity,
                          const Skin& skin,
                          std::vector<glm::mat4>& out);

} // namespace Skinning
