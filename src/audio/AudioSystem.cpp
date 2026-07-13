#include "audio/AudioSystem.h"

#include "audio/AudioClip.h"
#include "audio/AudioEngine.h"
#include "assets/ResourceManager.h"
#include "ecs/Registry.h"

#include <algorithm>
#include <memory>
#include <glm/glm.hpp>

namespace {

// Simple linear distance rolloff for spatial sources. Hardcoded for now; these
// become per-source fields in a later sub-phase.
constexpr float MinDistance = 1.0f;   // full volume within this radius
constexpr float MaxDistance = 25.0f;  // silent beyond this radius

// Returns the active listener's world position and gain. Falls back to the
// origin at unit gain when no AudioListener exists (sources still play in 2D).
struct ListenerState {
    glm::vec3 position{0.0f};
    float gain = 1.0f;
    bool exists = false;
};

// const Registry: the listener lookup only reads, and the ECS records const
// access as a read — which is what lets the Audio system declare AudioListener,
// Transform, and Hierarchy read-only and have that verified (Phase 13B).
ListenerState findListener(const Registry& registry) {
    ListenerState state;
    for (const auto& [entity, listener] : registry.audioListeners.getAll()) {
        if (!registry.transforms.has(entity)) {
            continue;
        }
        state.position = getWorldPosition(entity, registry);
        state.gain = listener.gain;
        state.exists = true;
        break; // first listener wins
    }
    return state;
}

float spatialAttenuation(const glm::vec3& source, const glm::vec3& listener) {
    const float distance = glm::length(source - listener);
    if (distance <= MinDistance) {
        return 1.0f;
    }
    if (distance >= MaxDistance) {
        return 0.0f;
    }
    return 1.0f - (distance - MinDistance) / (MaxDistance - MinDistance);
}

} // namespace

namespace AudioSystem {

void update(Registry& registry, AudioEngine& engine) {
    const ListenerState listener = findListener(registry);

    for (auto& [entity, source] : registry.audioSources.getAll()) {
        if (source.clip == INVALID_HANDLE) {
            source.oneShotPending = false;
            continue;
        }

        // Effective gain = authored volume × listener gain × spatial falloff.
        float gain = source.volume * listener.gain;
        if (source.spatial && registry.transforms.has(entity)) {
            gain *= spatialAttenuation(getWorldPosition(entity, registry), listener.position);
        }

        std::shared_ptr<AudioClip> clip = ResourceManager::getAudioClip(source.clip);

        // One-shot: fire-and-forget play requested by gameplay this step (e.g. a
        // CollisionSfx behavior on impact). Independent of playOnStart/looping.
        if (source.oneShotPending) {
            source.oneShotPending = false;
            engine.play(clip, gain, source.pitch, false);
        }

        if (!source.started) {
            source.started = true;
            if (source.playOnStart) {
                source.voice = engine.play(clip, gain, source.pitch, source.loop);
            }
            continue;
        }

        // Keep a still-playing voice synced with live inspector edits. A finished
        // one-shot simply stays "started" so it doesn't retrigger.
        if (engine.isActive(source.voice)) {
            engine.setVoiceParams(source.voice, gain, source.pitch);
        }
    }
}

void stopAll(Registry& registry, AudioEngine& engine) {
    engine.stopAll();
    for (auto& [entity, source] : registry.audioSources.getAll()) {
        (void)entity;
        source.started = false;
        source.voice = 0;
        source.oneShotPending = false;
    }
}

} // namespace AudioSystem
