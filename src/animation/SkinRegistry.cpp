#include "animation/SkinRegistry.h"

#include <utility>

std::unordered_map<std::string, Skin>& SkinRegistry::table() {
    static std::unordered_map<std::string, Skin> instance;
    return instance;
}

void SkinRegistry::registerSkin(const std::string& name, Skin skin) {
    table()[name] = std::move(skin);
}

const Skin* SkinRegistry::get(const std::string& name) {
    const auto it = table().find(name);
    return it == table().end() ? nullptr : &it->second;
}

bool SkinRegistry::has(const std::string& name) {
    return table().find(name) != table().end();
}

void SkinRegistry::clear() {
    table().clear();
}
