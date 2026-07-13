#pragma once

#include <algorithm>
#include <functional>
#include <stdexcept>
#include "assets/AssetHandle.h"
#include "audio/AudioComponents.h"
#include "ecs/ComponentAccess.h"
#include "ecs/ComponentStorage.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "physics/PhysicsComponents.h"

// Component type -> ComponentType bit, so ComponentStorage can report access
// against the same identities systems declare (Phase 13B). One entry per storage
// on Registry below; adding a storage without a trait means it silently escapes
// access enforcement, so keep the two lists in step.
#define SUGAR_TRACK_COMPONENT(StructName, EnumName)                       \
    template <>                                                           \
    struct ComponentTraits<StructName> {                                  \
        static constexpr bool tracked = true;                             \
        static constexpr ComponentType type = ComponentType::EnumName;    \
    }

SUGAR_TRACK_COMPONENT(NameComponent, Name);
SUGAR_TRACK_COMPONENT(TransformComponent, Transform);
SUGAR_TRACK_COMPONENT(MeshComponent, Mesh);
SUGAR_TRACK_COMPONENT(MaterialComponent, Material);
SUGAR_TRACK_COMPONENT(HierarchyComponent, Hierarchy);
SUGAR_TRACK_COMPONENT(ScriptComponent, Script);
SUGAR_TRACK_COMPONENT(RigidBodyComponent, RigidBody);
SUGAR_TRACK_COMPONENT(ColliderComponent, Collider);
SUGAR_TRACK_COMPONENT(PrefabInstanceComponent, PrefabInstance);
SUGAR_TRACK_COMPONENT(AudioSourceComponent, AudioSource);
SUGAR_TRACK_COMPONENT(AudioListenerComponent, AudioListener);

#undef SUGAR_TRACK_COMPONENT

class Registry {
public:
    Entity createEntity() {
        return entityManager.createEntity();
    }

    void destroyEntity(Entity entity) {
        detachFromParent(entity);
        releaseResources(entity);

        if (hierarchy.has(entity)) {
            auto children = hierarchy.get(entity).children;
            for (Entity child : children) {
                if (hierarchy.has(child)) {
                    hierarchy.get(child).parent = INVALID_ENTITY;
                }
            }
            hierarchy.remove(entity);
        }

        names.remove(entity);
        transforms.remove(entity);
        meshes.remove(entity);
        materials.remove(entity);
        scripts.remove(entity);
        rigidBodies.remove(entity);
        colliders.remove(entity);
        prefabInstances.remove(entity);
        audioSources.remove(entity);
        audioListeners.remove(entity);
        entityManager.destroyEntity(entity);
    }

    void setParent(Entity child, Entity parent) {
        if (child == INVALID_ENTITY) {
            throw std::invalid_argument("child entity must be valid");
        }

        ensureHierarchy(child);
        if (parent != INVALID_ENTITY) {
            ensureHierarchy(parent);
        }

        if (child == parent) {
            throw std::logic_error("an entity cannot be parented to itself");
        }

        for (Entity ancestor = parent; ancestor != INVALID_ENTITY; ancestor = hierarchy.get(ancestor).parent) {
            if (ancestor == child) {
                throw std::logic_error("entity hierarchy cannot contain cycles");
            }
        }

        detachFromParent(child);
        hierarchy.get(child).parent = parent;

        if (parent != INVALID_ENTITY) {
            auto& children = hierarchy.get(parent).children;
            if (std::find(children.begin(), children.end(), child) == children.end()) {
                children.push_back(child);
            }
        }
    }

    void reset() {
        for (const auto& [entity, transformComponent] : transforms.getAll()) {
            (void)transformComponent;
            releaseResources(entity);
        }

        hierarchy.clear();
        names.clear();
        transforms.clear();
        meshes.clear();
        materials.clear();
        scripts.clear();
        rigidBodies.clear();
        colliders.clear();
        prefabInstances.clear();
        audioSources.clear();
        audioListeners.clear();
        entityManager.reset();
    }

    ComponentStorage<NameComponent> names;
    ComponentStorage<TransformComponent> transforms;
    ComponentStorage<MeshComponent> meshes;
    ComponentStorage<MaterialComponent> materials;
    ComponentStorage<HierarchyComponent> hierarchy;
    ComponentStorage<ScriptComponent> scripts;
    ComponentStorage<RigidBodyComponent> rigidBodies;
    ComponentStorage<ColliderComponent> colliders;
    ComponentStorage<PrefabInstanceComponent> prefabInstances;
    ComponentStorage<AudioSourceComponent> audioSources;
    ComponentStorage<AudioListenerComponent> audioListeners;

    // Injected by the Engine layer to release GPU/asset handles when an entity is
    // destroyed. Keeps the ECS (Core layer) free of any ResourceManager / Vulkan
    // dependency (dependency inversion for the Editor -> Engine -> Core split).
    // Left null in headless contexts (tests), where no resources are loaded.
    std::function<void(AssetHandle)> onReleaseAsset;

private:
    void ensureHierarchy(Entity entity) {
        if (!hierarchy.has(entity)) {
            hierarchy.add(entity, {});
        }
    }

    void detachFromParent(Entity child) {
        if (!hierarchy.has(child)) {
            return;
        }

        Entity parent = hierarchy.get(child).parent;
        if (parent == INVALID_ENTITY || !hierarchy.has(parent)) {
            hierarchy.get(child).parent = INVALID_ENTITY;
            return;
        }

        auto& siblings = hierarchy.get(parent).children;
        siblings.erase(
            std::remove(siblings.begin(), siblings.end(), child),
            siblings.end()
        );
        hierarchy.get(child).parent = INVALID_ENTITY;
    }

    void releaseResources(Entity entity) {
        if (!onReleaseAsset) {
            return; // no resource backend wired (headless/tests)
        }
        if (meshes.has(entity)) {
            onReleaseAsset(meshes.get(entity).mesh);
        }
        if (materials.has(entity)) {
            onReleaseAsset(materials.get(entity).material.albedo);
        }
        if (audioSources.has(entity)) {
            onReleaseAsset(audioSources.get(entity).clip);
        }
    }

    EntityManager entityManager;
};

inline glm::mat4 getWorldMatrix(Entity entity, const Registry& registry) {
    const auto& transform = registry.transforms.get(entity).transform;

    if (!registry.hierarchy.has(entity)) {
        return transform.getLocalMatrix();
    }

    const Entity parent = registry.hierarchy.get(entity).parent;
    if (parent == INVALID_ENTITY || !registry.transforms.has(parent)) {
        return transform.getLocalMatrix();
    }

    return getWorldMatrix(parent, registry) * transform.getLocalMatrix();
}

inline glm::vec3 getWorldPosition(Entity entity, const Registry& registry) {
    return glm::vec3(getWorldMatrix(entity, registry)[3]);
}
