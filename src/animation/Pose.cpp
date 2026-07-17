#include "animation/Pose.h"

#include "ecs/Registry.h"

#include <algorithm>

namespace {

// Blends one channel. Presence wins over value: if only one side animates the
// channel, that side's value is used unblended rather than mixed with a default.
template <typename T, typename Interp>
void blendChannel(bool hasA, const T& a, bool hasB, const T& b, float weight,
                  bool& hasOut, T& out, Interp interpolate) {
    if (hasA && hasB) {
        hasOut = true;
        out = interpolate(a, b, weight);
    } else if (hasA) {
        hasOut = true;
        out = a;
    } else if (hasB) {
        hasOut = true;
        out = b;
    } else {
        hasOut = false;
    }
}

TransformSample blendSamples(const TransformSample& a, const TransformSample& b, float weight) {
    TransformSample result;
    blendChannel(a.hasTranslation, a.translation, b.hasTranslation, b.translation, weight,
                 result.hasTranslation, result.translation,
                 [](const glm::vec3& x, const glm::vec3& y, float t) { return interpolateKey(x, y, t); });
    blendChannel(a.hasRotation, a.rotation, b.hasRotation, b.rotation, weight,
                 result.hasRotation, result.rotation,
                 [](const glm::quat& x, const glm::quat& y, float t) { return interpolateKey(x, y, t); });
    blendChannel(a.hasScale, a.scale, b.hasScale, b.scale, weight,
                 result.hasScale, result.scale,
                 [](const glm::vec3& x, const glm::vec3& y, float t) { return interpolateKey(x, y, t); });
    return result;
}

} // namespace

const PoseEntry* Pose::find(const std::string& target) const {
    for (const PoseEntry& entry : entries) {
        if (entry.target == target) {
            return &entry;
        }
    }
    return nullptr;
}

PoseEntry* Pose::find(const std::string& target) {
    return const_cast<PoseEntry*>(static_cast<const Pose*>(this)->find(target));
}

void samplePose(const AnimationClip& clip, float time, Pose& out) {
    out.entries.clear();
    out.entries.reserve(clip.tracks.size());
    for (const TransformTrack& track : clip.tracks) {
        out.entries.push_back({ track.target, sampleTrack(track, time) });
    }
}

void blendPoses(const Pose& a, const Pose& b, float weight, Pose& out) {
    // Clamped rather than trusted: a transition that overshoots its duration by a
    // fraction of a step would otherwise extrapolate past the target pose.
    const float t = std::min(std::max(weight, 0.0f), 1.0f);

    out.entries.clear();
    out.entries.reserve(a.entries.size() + b.entries.size());

    // `a` order first, so the result is deterministic and stable across steps.
    for (const PoseEntry& entryA : a.entries) {
        if (const PoseEntry* entryB = b.find(entryA.target)) {
            out.entries.push_back({ entryA.target, blendSamples(entryA.sample, entryB->sample, t) });
        } else {
            out.entries.push_back(entryA);
        }
    }
    // Then whatever only `b` animates.
    for (const PoseEntry& entryB : b.entries) {
        if (a.find(entryB.target) == nullptr) {
            out.entries.push_back(entryB);
        }
    }
}

void applyPose(Registry& registry, Entity root, const Pose& pose) {
    const Registry& readOnly = registry;

    for (const PoseEntry& entry : pose.entries) {
        const Entity target = entry.target.empty()
            ? root
            : findDescendantByName(readOnly, root, entry.target);
        if (target == INVALID_ENTITY || !readOnly.transforms.has(target)) {
            continue;
        }

        Transform& transform = registry.transforms.get(target).transform;
        // Only channels the pose actually carries are written — an absent scale
        // channel must leave an authored scale alone, not reset it to 1.
        if (entry.sample.hasTranslation) {
            transform.position = entry.sample.translation;
        }
        if (entry.sample.hasRotation) {
            transform.rotation = entry.sample.rotation;
        }
        if (entry.sample.hasScale) {
            transform.scale = entry.sample.scale;
        }
    }
}
