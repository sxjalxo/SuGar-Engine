#pragma once

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <unordered_set>
#include "animation/AnimationComponents.h"
#include "assets/AssetHandle.h"
#include "audio/AudioComponents.h"
#include "ecs/ComponentAccess.h"
#include "ecs/ComponentStorage.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameData.h"
#include "navigation/NavComponents.h"
#include "physics/PhysicsComponents.h"
#include "rendering/CameraComponent.h"
#include "rendering/LightComponent.h"
#include "ui/UIComponents.h"

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
SUGAR_TRACK_COMPONENT(UIScreenComponent, UIScreen);
SUGAR_TRACK_COMPONENT(FocusComponent, Focus);
SUGAR_TRACK_COMPONENT(TextInputComponent, TextInput);
SUGAR_TRACK_COMPONENT(UILabelComponent, UILabel);
SUGAR_TRACK_COMPONENT(AnimationPlayerComponent, Animation);
SUGAR_TRACK_COMPONENT(SkinnedMeshComponent, SkinnedMesh);
SUGAR_TRACK_COMPONENT(AnimationStateComponent, AnimationState);
SUGAR_TRACK_COMPONENT(AnimationParametersComponent, AnimationParameters);
SUGAR_TRACK_COMPONENT(NavAgentComponent, NavAgent);
SUGAR_TRACK_COMPONENT(NavMeshSourceComponent, NavMeshSource);
SUGAR_TRACK_COMPONENT(NavObstacleComponent, NavObstacle);
SUGAR_TRACK_COMPONENT(CameraComponent, Camera);
SUGAR_TRACK_COMPONENT(GameDataComponent, GameData);
SUGAR_TRACK_COMPONENT(LightComponent, Light);
SUGAR_TRACK_COMPONENT(UIElementStateComponent, UIElementState);
SUGAR_TRACK_COMPONENT(WorldLabelComponent, WorldLabel);

#undef SUGAR_TRACK_COMPONENT

class Registry {
public:
    Entity createEntity() {
        return entityManager.createEntity();
    }

    // Recreates an entity with a specific id (Phase 14B) — used to restore a
    // destroyed subtree into its original ids so editor command references stay
    // valid without a remap layer. See EntityManager::createEntityWithId.
    Entity createEntityWithId(Entity id) {
        return entityManager.createEntityWithId(id);
    }

    // True if `entity` still names the entity it was created for — false if it was
    // destroyed, whether or not its slot has since been reused
    // (DevDocs/DESIGN_GENERATIONAL_IDS.md).
    //
    // This is not the same question as `transforms.has(entity)`. A component query
    // answers "does this handle address something now", which a recycled handle
    // answers *yes* to while addressing a different entity. Gameplay that keeps a
    // handle across frames — a target, an owner, a projectile's shooter — wants this
    // one.
    bool isAlive(Entity entity) const {
        return entityManager.isAlive(entity);
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
        uiScreens.remove(entity);
        focus.remove(entity);
        textInputs.remove(entity);
        uiLabels.remove(entity);
        animations.remove(entity);
        skinnedMeshes.remove(entity);
        animationStates.remove(entity);
        animationParameters.remove(entity);
        navAgents.remove(entity);
        navMeshSources.remove(entity);
        navObstacles.remove(entity);
        cameras.remove(entity);
        gameData.remove(entity);
        lights.remove(entity);
        uiElementStates.remove(entity);
        worldLabels.remove(entity);
        entityManager.destroyEntity(entity);
    }

    // Destroys `root` AND its whole subtree. Plain destroyEntity() re-parents a
    // destroyed entity's children to the world (they survive as orphan roots), which
    // is the right primitive for the editor's delete-then-undo (it restores each node
    // by id). Gameplay that wants "kill this enemy and everything attached to it"
    // wants the cascade instead — a bullet's muzzle flash / an enemy's health bar
    // should not outlive it. Iterative (leaves first) so a deep chain can't overflow
    // the stack, and so each destroyEntity sees its children already gone.
    void destroyEntityTree(Entity root) {
        if (root == INVALID_ENTITY) {
            return;
        }
        std::vector<Entity> stack{root};
        std::vector<Entity> order; // root-first; destroyed in reverse (leaves first)
        while (!stack.empty()) {
            const Entity entity = stack.back();
            stack.pop_back();
            order.push_back(entity);
            if (hierarchy.has(entity)) {
                for (Entity child : hierarchy.get(entity).children) {
                    stack.push_back(child);
                }
            }
        }
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            destroyEntity(*it);
        }
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
        // Release GPU/asset handles for every entity that *owns* one, keyed off the
        // resource storages themselves — not off Transform. A mesh/material/audio
        // entity with no TransformComponent would otherwise leak its handles here.
        // A set dedups entities that own several (releaseResources frees all three).
        std::unordered_set<Entity> resourceOwners;
        for (const auto& [entity, component] : meshes.getAll()) { (void)component; resourceOwners.insert(entity); }
        for (const auto& [entity, component] : materials.getAll()) { (void)component; resourceOwners.insert(entity); }
        for (const auto& [entity, component] : audioSources.getAll()) { (void)component; resourceOwners.insert(entity); }
        for (Entity entity : resourceOwners) {
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
        uiScreens.clear();
        focus.clear();
        textInputs.clear();
        uiLabels.clear();
        animations.clear();
        skinnedMeshes.clear();
        animationStates.clear();
        animationParameters.clear();
        navAgents.clear();
        navMeshSources.clear();
        navObstacles.clear();
        cameras.clear();
        gameData.clear();
        lights.clear();
        uiElementStates.clear();
        worldLabels.clear();
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
    ComponentStorage<UIScreenComponent> uiScreens;
    ComponentStorage<FocusComponent> focus;
    ComponentStorage<TextInputComponent> textInputs;
    ComponentStorage<UILabelComponent> uiLabels;
    ComponentStorage<AnimationPlayerComponent> animations;
    ComponentStorage<SkinnedMeshComponent> skinnedMeshes;
    ComponentStorage<AnimationStateComponent> animationStates;
    ComponentStorage<AnimationParametersComponent> animationParameters;
    ComponentStorage<NavAgentComponent> navAgents;
    ComponentStorage<NavMeshSourceComponent> navMeshSources;
    ComponentStorage<NavObstacleComponent> navObstacles;
    ComponentStorage<CameraComponent> cameras;
    // Game-defined per-entity state. The engine stores/serializes it and never reads a
    // value — see DevDocs/DESIGN_GAME_DATA.md.
    ComponentStorage<GameDataComponent> gameData;
    // Lights on entities; pose derived from the transform (DevDocs/DESIGN_LIGHTING.md).
    ComponentStorage<LightComponent> lights;
    // Per-element UI presentation state (classes / inline style).
    ComponentStorage<UIElementStateComponent> uiElementStates;
    // Text anchored to this entity's world position (nameplates, hints).
    ComponentStorage<WorldLabelComponent> worldLabels;

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

// Game data for `entity`, adding an empty component the first time. Gameplay code writes
// a handful of keys on entities it spawned; making every call site write the
// has()/add()/get() dance would guarantee someone forgets one and silently loses state.
inline GameDataComponent& ensureGameData(Registry& registry, Entity entity) {
    if (!registry.gameData.has(entity)) {
        registry.gameData.add(entity, {});
    }
    return registry.gameData.get(entity);
}

// DESIGN_RENDER_CPU_TIMING.md §11 -- work counters for the two helpers pose resolution
// leans on. Counts, not timings: §10.3 measured a clock read at ~100 ns and splitting
// `skinning` by time would need ~77 000 of them a frame, which would cost more than it
// measures. An increment is ~1 ns, exact rather than statistical, and answers "why" where
// a millisecond only answers "how bad".
//
// Read as DELTAS around a call (see scene/DrawList.cpp), never as absolutes.
//
// Incremented ONLY by animation/Skinning.cpp's call-local joint walk and world-matrix
// memo -- deliberately NOT by getWorldMatrix or findDescendantByName below. They were,
// briefly, and it cost ~0.4 ms per fixed step across every other caller (the Animation
// system's own per-track name lookups above all) while contributing nothing to the
// numbers actually reported: after §13's fix, skinning no longer calls either helper.
// An always-on instrument in the engine's hottest inline helpers has to earn it, and
// this one had stopped earning it the moment the code it was measuring moved (§15).
//
// Not atomic, deliberately: both helpers are gameplay-thread-only, and an atomic here
// would make the instrument the thing being measured.
struct RegistryWorkCounters {
    unsigned long long subtreeSearches = 0; // findDescendantByName calls (= one heap alloc each)
    unsigned long long nodeVisits = 0;      // nodes popped inside those searches
    unsigned long long matrixHops = 0;      // ancestor levels walked by getWorldMatrix
};

// DECLARED here, DEFINED once in ecs/RegistryWork.cpp -- deliberately not inline. An
// inline accessor with a function-local static gives every module its own copy, which
// made every counter read zero on the first run (§12).
RegistryWorkCounters& registryWorkCounters();

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

// Resolve MANY names against one subtree in a SINGLE depth-first walk, instead of one
// findDescendantByName call (and one heap allocation) per name.
//
// Rule 22, fixing the category rather than the case: two hot loops in this engine
// resolved N names against the same subtree N times — joint resolution in
// animation/Skinning.cpp and pose application in animation/Pose.cpp. Measured, each was
// the dominant cost of its region (DevDocs/DESIGN_RENDER_CPU_TIMING.md §12, §16).
//
// Semantics are findDescendantByName's, name for name:
//   * visit order is identical — children pushed reversed so they pop in declared order;
//   * FIRST MATCH WINS, because a slot is filled only while still empty. That is load
//     bearing: picking a different match for an ambiguous name poses a character from the
//     wrong bone with nothing visibly broken. SelfTests.h covers it and it was break-tested
//     by reversing this very order;
//   * a name appearing twice in the input fills both slots from the same first match,
//     which is what two separate searches did;
//   * a name that matches nothing leaves INVALID_ENTITY.
//
// `nameAt(i)` returns the i-th name as a `const std::string&`, so callers whose names live
// inside some other struct need not materialise a vector of copies to call this.
//
// Returns the number of nodes visited. It deliberately keeps NO counters of its own:
// §15 of the design record records what unconditional instrumentation in a hot shared
// helper costs every caller that never asked for it, so a caller that wants the figure
// adds the return value to its own counter and everyone else pays nothing.
template <typename NameAt>
inline std::size_t findDescendantsByName(const Registry& registry, Entity root,
                                         std::size_t count, NameAt nameAt,
                                         std::vector<Entity>& out) {
    out.assign(count, INVALID_ENTITY);
    if (root == INVALID_ENTITY || count == 0) {
        return 0;
    }

    std::size_t visited = 0;
    std::size_t unresolved = count;
    std::vector<Entity> pending{ root };
    while (!pending.empty() && unresolved > 0) {
        const Entity entity = pending.back();
        pending.pop_back();
        visited++;

        if (registry.names.has(entity)) {
            const std::string& name = registry.names.get(entity).name;
            for (std::size_t i = 0; i < count; i++) {
                if (out[i] == INVALID_ENTITY && nameAt(i) == name) {
                    out[i] = entity;
                    unresolved--;
                }
            }
        }
        if (registry.hierarchy.has(entity)) {
            const auto& children = registry.hierarchy.get(entity).children;
            pending.insert(pending.end(), children.rbegin(), children.rend());
        }
    }
    return visited;
}

// The topmost ancestor of `entity` (itself, if it has no parent). Cycle-free by
// construction: setParent rejects cycles.
inline Entity getRootAncestor(Entity entity, const Registry& registry) {
    Entity current = entity;
    while (registry.hierarchy.has(current)) {
        const Entity parent = registry.hierarchy.get(current).parent;
        if (parent == INVALID_ENTITY) {
            break;
        }
        current = parent;
    }
    return current;
}

// Depth-first search of `root`'s subtree (root included) for an entity named
// `name`; INVALID_ENTITY if there is none. Children are visited in declared order,
// so "first match wins" is stable across runs when names are ambiguous.
//
// Name-based lookup is the engine's idiom for cross-entity references that must
// survive serialization (animation track targets, skin joints): a name round-trips
// as a plain string, where an index or pointer would not.
inline Entity findDescendantByName(const Registry& registry, Entity root, const std::string& name) {
    if (root == INVALID_ENTITY) {
        return INVALID_ENTITY;
    }

    // Iterative rather than recursive: a deep skeleton shouldn't risk the stack.
    std::vector<Entity> pending{root};
    while (!pending.empty()) {
        const Entity entity = pending.back();
        pending.pop_back();

        if (registry.names.has(entity) && registry.names.get(entity).name == name) {
            return entity;
        }
        if (registry.hierarchy.has(entity)) {
            const auto& children = registry.hierarchy.get(entity).children;
            // Reversed, so popping the stack visits children in declared order.
            pending.insert(pending.end(), children.rbegin(), children.rend());
        }
    }
    return INVALID_ENTITY;
}
