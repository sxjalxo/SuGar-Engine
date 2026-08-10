#include "audio/AudioSystem.h"

#include "audio/AudioClip.h"
#include "audio/AudioEngine.h"
#include "assets/ResourceManager.h"
#include "ecs/Registry.h"

#include <algorithm>
#include <memory>
#include <vector>
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
    // "First listener wins" must be deterministic: the storage is an unordered_map,
    // so iterating it directly makes the winner depend on hash order when a scene has
    // more than one listener. Pick the lowest entity id instead — stable run-to-run
    // and across a snapshot restore, matching the id-order discipline used elsewhere.
    Entity chosen = INVALID_ENTITY;
    for (const auto& [entity, listener] : registry.audioListeners.getAll()) {
        (void)listener;
        if (!registry.transforms.has(entity)) {
            continue;
        }
        if (chosen == INVALID_ENTITY || entity < chosen) {
            chosen = entity;
        }
    }
    if (chosen != INVALID_ENTITY) {
        state.position = getWorldPosition(chosen, registry);
        state.gain = registry.audioListeners.get(chosen).gain;
        state.exists = true;
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

    // Iterate sources in ascending id order, not unordered_map order: play() hands
    // out voice ids in call order and fires one-shots as it goes, so a stable
    // iteration keeps audio behavior identical run-to-run and across a snapshot
    // restore (the same determinism guarantee ScriptSystem/Physics/DrawList hold).
    std::vector<Entity> ordered;
    ordered.reserve(registry.audioSources.getAll().size());
    for (const auto& [entity, source] : registry.audioSources.getAll()) {
        (void)source;
        ordered.push_back(entity);
    }
    std::sort(ordered.begin(), ordered.end());

    for (Entity entity : ordered) {
        auto& source = registry.audioSources.get(entity);
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
