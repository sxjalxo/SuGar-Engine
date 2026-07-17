#pragma once

#include <string>
#include <vector>

#include "animation/AnimationClip.h"
#include "ecs/Entity.h"

class Registry;

// Phase 17D — the intermediate representation blending needs.
//
// 17A sampled a clip straight into `TransformComponent`, which is fine for one clip
// and impossible for two: once a pose has been written into the transforms, the
// information needed to blend it with another is gone. A Pose is that value *before*
// it lands — so `blend(a, b, w)` is an ordinary function over data, and applying is a
// separate step.
//
// A Pose is **derived**, always: it is recomputed from (clip, time) every step and
// never stored, never serialized. It has no ECS presence. See
// docs/DESIGN_ANIMATION.md.
struct PoseEntry {
    std::string target; // entity name, resolved against the player's subtree
    TransformSample sample;
};

struct Pose {
    std::vector<PoseEntry> entries;

    bool empty() const { return entries.empty(); }
    void clear() { entries.clear(); }

    // The entry for `target`, or nullptr. Linear: a pose holds one entry per animated
    // bone, and skeletons are small. No index is built because a Pose is rebuilt from
    // scratch every step — an index would cost more to construct than it saves.
    const PoseEntry* find(const std::string& target) const;
    PoseEntry* find(const std::string& target);
};

// Samples every track of `clip` at `time` into `out`. `time` must already be wrapped
// or clamped into range: how time advances is playback state's business, not the
// clip's (AnimationSystem owns that).
void samplePose(const AnimationClip& clip, float time, Pose& out);

// Blends `b` into `a` by `weight` (0 = all `a`, 1 = all `b`), writing to `out`.
//
// Targets present in both are interpolated per channel. A target present in only one
// pose is taken **as-is** rather than faded: with nothing to fade toward, the honest
// options are "hold it" or "guess a rest value", and holding is the one that cannot
// silently invent motion. Same for a channel present in one sample and not the other
// — an absent channel means "this clip doesn't animate that", not "animate it to
// zero".
//
// `out` may alias neither `a` nor `b`.
void blendPoses(const Pose& a, const Pose& b, float weight, Pose& out);

// Writes `pose` into the transforms of `root`'s subtree, resolving each entry's
// target by name (an empty target means `root` itself). Entries that resolve to
// nothing, or to an entity without a transform, are skipped — one missing bone
// shouldn't stop the rest of the character from posing.
//
// This is where a derived pose stops being a value and becomes ECS state; both the
// single-clip player and the state machine funnel through it, so there is one place
// that writes a pose and one definition of how targets bind.
void applyPose(Registry& registry, Entity root, const Pose& pose);
