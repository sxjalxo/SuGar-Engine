#pragma once

#include <unordered_map>
#include "ecs/Entity.h"

template<typename T>
class ComponentStorage {
public:
    void add(Entity entity, const T& component) {
        components[entity] = component;
    }

    T& get(Entity entity) {
        return components.at(entity);
    }

    const T& get(Entity entity) const {
        return components.at(entity);
    }

    bool has(Entity entity) const {
        return components.find(entity) != components.end();
    }

    void remove(Entity entity) {
        components.erase(entity);
    }

    void clear() {
        components.clear();
    }

    std::unordered_map<Entity, T>& getAll() {
        return components;
    }

    const std::unordered_map<Entity, T>& getAll() const {
        return components;
    }

private:
    std::unordered_map<Entity, T> components;
};
