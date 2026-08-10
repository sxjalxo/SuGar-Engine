#include "scene/ScriptSystem.h"

#include <algorithm>
#include <vector>

#include "ecs/Registry.h"
#include "scene/Behavior.h"
#include "scene/BehaviorRegistry.h"

void ScriptSystem::run(Registry& registry, float deltaTime) {
    // Iterate a *sorted snapshot* of the scripted entity ids, never the live storage:
    //  1. Determinism — behaviors interact, so tick order is observable. Sorted id
    //     order is stable run-to-run and across a snapshot restore; unordered_map
    //     order is not. Matches Physics (pair sort) and DrawList.
    //  2. Safety — a behavior may spawn/destroy entities this step, which inserts or
    //     erases into registry.scripts. A private id vector is immune to the rehash /
    //     invalidation that holding a live map iterator across the insert would cause.
    std::vector<Entity> scripted;
    scripted.reserve(registry.scripts.getAll().size());
    for (const auto& [entity, scriptComponent] : registry.scripts.getAll()) {
        (void)scriptComponent;
        scripted.push_back(entity);
    }
    std::sort(scripted.begin(), scripted.end());

    for (Entity entity : scripted) {
        // An earlier behavior this step may have destroyed this entity (or just its
        // ScriptComponent); re-check before every touch.
        if (!registry.scripts.has(entity)) {
            continue;
        }
        Behavior* behavior = BehaviorRegistry::get(registry.scripts.get(entity).behavior);
        if (behavior == nullptr) {
            continue;
        }
        if (!registry.scripts.get(entity).started) {
            behavior->onStart(registry, entity);
            // onStart may have destroyed the entity — re-check before writing.
            if (!registry.scripts.has(entity)) {
                continue;
            }
            registry.scripts.get(entity).started = true;
        }
        behavior->onUpdate(registry, entity, deltaTime);
    }
}
