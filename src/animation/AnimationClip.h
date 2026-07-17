#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Phase 17A — animation clip data + sampling. Clips are **immutable assets, not
// state**: keyframes never change at runtime, so they are never snapshotted. The
// authoritative playback state (which clip, what time) lives in ECS
// (AnimationPlayerComponent) and a pose is recomputed from the two:
//
//     Pose = f(clip data, playback state)
//
// This header is pure data + pure math, which is exactly why it lives in Core: no
// Vulkan, no ResourceManager, and therefore testable headlessly (RULES.md Rule 9,
// Rule 15). See docs/DESIGN_ANIMATION.md.

// How a channel interpolates between two keys. glTF also defines CUBICSPLINE; it
// is deliberately absent until a real asset needs it, rather than speculatively
// implemented (docs/DESIGN_ANIMATION.md, open question 4).
enum class Interpolation {
    Linear,
    Step
};

// A keyframe channel over T: `times` (seconds, strictly increasing) paired with
// `values` of equal length. Split into parallel arrays rather than a vector of
// {time, value} pairs so a sample only touches the times while searching.
template <typename T>
struct Channel {
    std::vector<float> times;
    std::vector<T> values;
    Interpolation interpolation = Interpolation::Linear;

    bool empty() const { return times.empty(); }
};

using Vec3Channel = Channel<glm::vec3>;
using QuatChannel = Channel<glm::quat>;

// Everything a clip animates on one target. Each channel is independent and may
// be empty — a clip that only rotates a wheel carries a rotation channel and
// nothing else, and sampling leaves the other components of the transform alone.
//
// `target` names an entity, resolved against the subtree of the entity carrying
// the AnimationPlayerComponent; empty means that entity itself. Name-based
// because it serializes to a string and matches the engine's existing indirection
// idiom (behaviors, screens, focus elements) — see docs/DESIGN_ANIMATION.md for
// the trade-off (duplicate names under one root are ambiguous; first match wins).
struct TransformTrack {
    std::string target;
    Vec3Channel translation;
    QuatChannel rotation;
    Vec3Channel scale;
};

// A sampled TRS for one target. The `has*` flags distinguish "this clip animates
// scale to 1.0" from "this clip does not animate scale at all" — without them,
// an absent channel would silently stomp an authored value with a default.
struct TransformSample {
    bool hasTranslation = false;
    glm::vec3 translation{0.0f};
    bool hasRotation = false;
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool hasScale = false;
    glm::vec3 scale{1.0f};
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f; // seconds; the last keyframe time across all tracks
    std::vector<TransformTrack> tracks;
};

// Interpolation between two keys. Named distinctly (rather than overloading
// `mix`) so the template below resolves it by ordinary overload lookup without
// colliding with glm's own generic overloads.
inline glm::vec3 interpolateKey(const glm::vec3& a, const glm::vec3& b, float alpha) {
    return glm::mix(a, b, alpha);
}

// glm::slerp already negates one input when the dot product is negative, so this
// takes the short way around the sphere. Renormalized because repeated slerps of
// slightly non-unit keys drift.
inline glm::quat interpolateKey(const glm::quat& a, const glm::quat& b, float alpha) {
    return glm::normalize(glm::slerp(a, b, alpha));
}

// Samples one channel at `time` (seconds). Returns false — leaving `out`
// untouched — when the channel is empty or malformed, which is how "this clip
// doesn't animate that component" is reported. Times outside the channel's range
// clamp to the first/last key; wrapping for looping clips is the caller's job
// (AnimationSystem), because it depends on playback state, not on the data.
template <typename T>
bool sampleChannel(const Channel<T>& channel, float time, T& out) {
    if (channel.times.empty() || channel.times.size() != channel.values.size()) {
        return false;
    }

    if (channel.times.size() == 1 || time <= channel.times.front()) {
        out = channel.values.front();
        return true;
    }
    if (time >= channel.times.back()) {
        out = channel.values.back();
        return true;
    }

    // First key strictly after `time`, so the containing interval is [hi-1, hi].
    const auto next = std::upper_bound(channel.times.begin(), channel.times.end(), time);
    const std::size_t hi = static_cast<std::size_t>(next - channel.times.begin());
    const std::size_t lo = hi - 1;

    if (channel.interpolation == Interpolation::Step) {
        out = channel.values[lo];
        return true;
    }

    const float span = channel.times[hi] - channel.times[lo];
    // Coincident keys have no "between" to interpolate across, and dividing by the
    // zero span would produce a NaN that propagates into the transform.
    const float alpha = span > 0.0f ? (time - channel.times[lo]) / span : 0.0f;
    out = interpolateKey(channel.values[lo], channel.values[hi], alpha);
    return true;
}

// Samples every channel of one track at `time`.
TransformSample sampleTrack(const TransformTrack& track, float time);

// The last keyframe time across every channel of every track — a clip's duration
// is a property of its data, which is why it is computed here and never stored in
// the component (that would be a second source of truth able to disagree).
float computeDuration(const std::vector<TransformTrack>& tracks);
