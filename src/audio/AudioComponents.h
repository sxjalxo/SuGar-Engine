#pragma once

#include <cstdint>

#include "assets/AssetHandle.h"

// Audio components follow the same "all state in serializable components" rule as
// the rest of the engine. Authored fields (clip/volume/pitch/loop/...) round-trip
// through the serializer; the runtime fields are transient mixer bookkeeping and
// are intentionally NOT serialized (they reset to their defaults on load, exactly
// like ScriptComponent::started). The clip is an AssetHandle into ResourceManager,
// just like MeshComponent/MaterialComponent — so it gets caching, ref counting,
// and hot reload for free; the serializer persists the clip's resource key.

struct AudioSourceComponent {
    AssetHandle clip = INVALID_HANDLE; // ResourceManager audio-clip handle
    float volume = 1.0f;       // linear gain [0..1+]
    float pitch = 1.0f;        // playback rate multiplier (1 = original pitch)
    bool loop = false;
    bool playOnStart = true;   // begin playing when Play mode starts
    bool spatial = false;      // attenuate by distance to the active AudioListener

    // --- runtime only (not serialized) ---
    bool started = false;      // Play-mode lifecycle latch (mirrors ScriptComponent)
    uint32_t voice = 0;        // active looping/playOnStart voice id, 0 = none
    bool oneShotPending = false; // set by gameplay (e.g. onCollision) to fire a
                                 // one-off play of this source's clip next step
};

// Marks the entity whose world position is the "ears" for spatial sources. The
// first entity carrying this component is used as the active listener.
struct AudioListenerComponent {
    float gain = 1.0f;         // master gain applied to everything this listener hears
};
