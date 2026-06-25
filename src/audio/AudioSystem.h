#pragma once

class Registry;
class AudioEngine;

// The ECS-facing audio system: a pure-ish function over (World, AudioEngine).
// It owns no state of its own — all source state lives in AudioSourceComponent —
// so it round-trips and hot-reloads like every other system.
namespace AudioSystem {

// Advances audio for one gameplay step: starts playOnStart sources, keeps each
// voice's gain/pitch in sync with its component, and applies distance
// attenuation for spatial sources relative to the active AudioListener.
void update(Registry& registry, AudioEngine& engine);

// Stops every voice and clears per-source runtime latches. Call on Stop / scene
// replacement so the next Play starts from a clean slate.
void stopAll(Registry& registry, AudioEngine& engine);

} // namespace AudioSystem
