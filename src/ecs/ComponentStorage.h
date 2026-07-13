#pragma once

#include <unordered_map>
#include "ecs/ComponentAccess.h"
#include "ecs/Entity.h"

// Every op reports itself to the thread's active ComponentAccessTracker (Phase
// 13B) so the scheduler can verify a system only touched the storages it
// declared. Const paths record a read; non-const paths hand out a mutable
// reference and so must record a write. This is why reading through a
// `const Registry&` matters: it's what lets a read-only system prove it is one.
// The recording compiles to nothing unless SUGAR_ACCESS_TRACKING is defined.
template<typename T>
class ComponentStorage {
public:
    void add(Entity entity, const T& component) {
        ComponentAccess::recordWrite<T>();
        components[entity] = component;
    }

    T& get(Entity entity) {
        ComponentAccess::recordWrite<T>();
        return components.at(entity);
    }

    const T& get(Entity entity) const {
        ComponentAccess::recordRead<T>();
        return components.at(entity);
    }

    bool has(Entity entity) const {
        ComponentAccess::recordRead<T>();
        return components.find(entity) != components.end();
    }

    void remove(Entity entity) {
        ComponentAccess::recordWrite<T>();
        components.erase(entity);
    }

    void clear() {
        ComponentAccess::recordWrite<T>();
        components.clear();
    }

    std::unordered_map<Entity, T>& getAll() {
        ComponentAccess::recordWrite<T>();
        return components;
    }

    const std::unordered_map<Entity, T>& getAll() const {
        ComponentAccess::recordRead<T>();
        return components;
    }

private:
    std::unordered_map<Entity, T> components;
};
