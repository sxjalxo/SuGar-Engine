#pragma once

#include <string>
#include <unordered_map>

#include "animation/Skin.h"

// Maps skin names -> immutable bind data, mirroring AnimationClipRegistry (and,
// behind it, BehaviorRegistry). SkinnedMeshComponent stores only the name, so the
// indirection survives serialization and a re-import can replace the bind data
// underneath a rendering character without invalidating any component.
class SkinRegistry {
public:
    // Registering an existing name replaces the skin (that is what asset hot reload
    // is). Any pointer previously handed out by get() is invalidated, which is why
    // callers resolve by name per frame and never cache the pointer.
    static void registerSkin(const std::string& name, Skin skin);

    // Returns nullptr if no skin is registered under that name.
    static const Skin* get(const std::string& name);

    static bool has(const std::string& name);

    static void clear();

private:
    // In a .cpp, never a header inline: the engine exe, Core, and the game DLL must
    // share exactly one table, for the same reason BehaviorRegistry's does.
    static std::unordered_map<std::string, Skin>& table();
};
