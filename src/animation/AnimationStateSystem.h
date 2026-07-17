#pragma once

class Registry;

// Phase 17D — evaluates animation state machines on the fixed step: advance the
// active state's time, test transitions against gameplay parameters, cross-fade, and
// write the resulting pose into transforms.
//
// It owns nothing. The active state, its time, and the transition's progress are
// authoritative ECS state (AnimationStateComponent); the graph is an immutable asset;
// blend weights, sampled poses and the blend result are derived and recomputed every
// step. The pattern is the same one 17A established, with a graph on top:
//
//     Pose = f(graph, playback state, parameters)
//
// so snapshot restore / time travel / hot reload bring a mid-transition character
// back exactly, with no animation-specific restore code. See docs/DESIGN_ANIMATION.md.
namespace AnimationStateSystem {

// Advances and applies every AnimationStateComponent. `dt` is the fixed step.
void update(Registry& registry, float dt);

} // namespace AnimationStateSystem
