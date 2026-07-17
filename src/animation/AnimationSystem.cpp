#include "animation/AnimationSystem.h"

#include "animation/AnimationClip.h"
#include "animation/AnimationClipRegistry.h"
#include "animation/AnimationComponents.h"
#include "animation/Pose.h"
#include "ecs/Registry.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

// Wrap into [0, duration) with a modulo — never by subtracting one duration.
// `time -= duration` is the tempting version and it breaks as soon as one step
// overshoots the clip (a 0.1 s clip at speed 100 crosses several loops in a single
// step), leaving time out of range. fmod keeps the sign of the numerator, so a
// rewind (speed < 0) lands negative and needs folding back up.
float wrapTime(float time, float duration) {
    if (!(duration > 0.0f)) {
        return 0.0f;
    }
    float wrapped = std::fmod(time, duration);
    if (wrapped < 0.0f) {
        wrapped += duration;
    }
    return wrapped;
}

float clampTime(float time, float duration) {
    if (!(duration > 0.0f)) {
        return 0.0f;
    }
    return time < 0.0f ? 0.0f : (time > duration ? duration : time);
}

} // namespace

namespace AnimationSystem {

void update(Registry& registry, float dt) {
    // Reused across entities so a scene full of animated characters doesn't
    // reallocate a pose per character per step. Derived either way — this is a
    // scratch buffer, not state.
    Pose pose;

    for (auto& [entity, player] : registry.animations.getAll()) {
        const AnimationClip* clip = AnimationClipRegistry::get(player.clip);
        if (clip == nullptr) {
            // Unknown clip name — nothing to sample and nothing to advance against.
            // Left running rather than stopped: the clip may simply not be imported
            // yet, and a hot reload that registers it should just pick up.
            continue;
        }

        if (player.playing) {
            player.time += dt * player.speed;

            if (player.loop) {
                player.time = wrapTime(player.time, clip->duration);
            } else {
                const float clamped = clampTime(player.time, clip->duration);
                // A one-shot that ran off either end stops *authoritatively*, so
                // gameplay reads "finished" from a component rather than inferring
                // it from a derived pose — and a restore replays the same decision.
                if (player.time != clamped) {
                    player.playing = false;
                }
                player.time = clamped;
            }
        }

        // Sample and apply in the same step as the advance: a clip-driven transform
        // is then a physics input this step rather than one step stale (which is
        // why this system is scheduled ahead of Physics).
        samplePose(*clip, player.time, pose);
        applyPose(registry, entity, pose);
    }
}

} // namespace AnimationSystem
