#include "ecs/ComponentAccess.h"

const char* componentTypeName(ComponentType type) {
    switch (type) {
        case ComponentType::Name:           return "Name";
        case ComponentType::Transform:      return "Transform";
        case ComponentType::Mesh:           return "Mesh";
        case ComponentType::Material:       return "Material";
        case ComponentType::Hierarchy:      return "Hierarchy";
        case ComponentType::Script:         return "Script";
        case ComponentType::RigidBody:      return "RigidBody";
        case ComponentType::Collider:       return "Collider";
        case ComponentType::PrefabInstance: return "PrefabInstance";
        case ComponentType::AudioSource:    return "AudioSource";
        case ComponentType::AudioListener:  return "AudioListener";
        case ComponentType::Count:          break;
    }
    return "<unknown>";
}

std::string describeComponentMask(ComponentMask mask) {
    if (mask == 0) {
        return "<none>";
    }

    std::string result;
    for (uint32_t i = 0; i < static_cast<uint32_t>(ComponentType::Count); ++i) {
        const auto type = static_cast<ComponentType>(i);
        if ((mask & componentBit(type)) == 0) {
            continue;
        }
        if (!result.empty()) {
            result += '|';
        }
        result += componentTypeName(type);
    }
    return result;
}

namespace ComponentAccess {

// A function-local thread_local defined in exactly one translation unit inside
// Core. Header inline variables would give the exe and each DLL their own copy
// under MSVC's COMDAT folding, which would silently break tracking across the
// Engine -> Core -> Game module boundary.
static ComponentAccessTracker*& trackerSlot() {
    static thread_local ComponentAccessTracker* tracker = nullptr;
    return tracker;
}

ComponentAccessTracker* activeTracker() {
    return trackerSlot();
}

void setActiveTracker(ComponentAccessTracker* tracker) {
    trackerSlot() = tracker;
}

} // namespace ComponentAccess
