#include "scene/BehaviorRegistry.h"

// Core owns only the registry *mechanism* (name -> instance). The concrete
// behaviors live in the game module DLL, which registers them here by name.

std::unordered_map<std::string, std::unique_ptr<Behavior>>& BehaviorRegistry::table() {
    static std::unordered_map<std::string, std::unique_ptr<Behavior>> instance;
    return instance;
}

void BehaviorRegistry::registerBehavior(const std::string& name, std::unique_ptr<Behavior> behavior) {
    table()[name] = std::move(behavior);
}

Behavior* BehaviorRegistry::get(const std::string& name) {
    const auto it = table().find(name);
    return it == table().end() ? nullptr : it->second.get();
}

bool BehaviorRegistry::has(const std::string& name) {
    return table().find(name) != table().end();
}

void BehaviorRegistry::clear() {
    table().clear();
}
