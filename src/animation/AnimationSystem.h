#pragma once

class Registry;

// Phase 17A — the ECS-facing animation system. Each fixed step it advances every
// player's authoritative time by `dt * speed`, then samples its clip and writes
// the resulting TRS into the target entities' transforms.
//
// It owns no state of its own: playback state lives in components, clip data in
// AnimationClipRegistry, and the pose is recomputed rather than kept — so
// `Pose = f(clip data, playback state)` holds, and snapshot restore / time travel
// / hot reload bring animation back for free, with no animation-specific code.
//
// The fixed step is the only clock. Advancing on the render delta would make the
// pose frame-rate dependent and replay non-deterministic, which is exactly the
// class of bug RULES.md Rule 21 exists to prevent. See docs/DESIGN_ANIMATION.md.
namespace AnimationSystem {

// Advances and applies every AnimationPlayerComponent. `dt` is the fixed step.
void update(Registry& registry, float dt);

} // namespace AnimationSystem
