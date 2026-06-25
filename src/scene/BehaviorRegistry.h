#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include "scene/Behavior.h"

// Maps behavior names -> stateless Behavior instances. ScriptComponent stores
// only the name, so the indirection survives serialization and (later) a
// hot-reloaded game module that re-registers behaviors under the same names.
class BehaviorRegistry {
public:
    static void registerBehavior(const std::string& name, std::unique_ptr<Behavior> behavior);

    // Returns nullptr if no behavior is registered under that name.
    static Behavior* get(const std::string& name);

    static bool has(const std::string& name);

    // Registers the engine's built-in behaviors (e.g. "Spinner"). Idempotent.
    static void registerBuiltins();

    static void clear();

private:
    static std::unordered_map<std::string, std::unique_ptr<Behavior>>& table();
};
