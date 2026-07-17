#pragma once

#include <string>
#include <unordered_map>

#include "animation/AnimationClip.h"

// Maps clip names -> immutable clip data, deliberately mirroring BehaviorRegistry.
// AnimationPlayerComponent stores only the name, so the indirection survives
// serialization and lets the clip table be repopulated (asset hot reload, a
// re-imported glTF) underneath a running animation without invalidating any
// component.
//
// Clips are assets, not state: nothing here is snapshotted. Core owns only the
// registry *mechanism*; the Engine layer fills it from glTF imports (Phase 17B),
// and headless tests fill it with synthetic clips — which is what keeps the whole
// playback path testable without a GPU.
class AnimationClipRegistry {
public:
    // Registering an existing name replaces the clip (that is what asset hot
    // reload is). Any pointer previously handed out by get() is invalidated, which
    // is why callers must resolve by name per step and never cache the pointer.
    static void registerClip(const std::string& name, AnimationClip clip);

    // Returns nullptr if no clip is registered under that name. Borrowed for the
    // duration of the call site only — see registerClip.
    static const AnimationClip* get(const std::string& name);

    static bool has(const std::string& name);

    static void clear();

private:
    // In a .cpp, never a header inline: the engine exe, Core, and the game DLL must
    // share exactly one table, for the same reason BehaviorRegistry's does.
    static std::unordered_map<std::string, AnimationClip>& table();
};
