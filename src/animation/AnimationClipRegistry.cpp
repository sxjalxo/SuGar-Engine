#include "animation/AnimationClipRegistry.h"

#include <utility>

std::unordered_map<std::string, AnimationClip>& AnimationClipRegistry::table() {
    static std::unordered_map<std::string, AnimationClip> instance;
    return instance;
}

void AnimationClipRegistry::registerClip(const std::string& name, AnimationClip clip) {
    table()[name] = std::move(clip);
}

const AnimationClip* AnimationClipRegistry::get(const std::string& name) {
    const auto it = table().find(name);
    return it == table().end() ? nullptr : &it->second;
}

bool AnimationClipRegistry::has(const std::string& name) {
    return table().find(name) != table().end();
}

void AnimationClipRegistry::clear() {
    table().clear();
}
