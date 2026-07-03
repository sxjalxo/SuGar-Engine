// The game module: concrete gameplay behaviors, compiled into a hot-swappable
// DLL that links ONLY against Core (no engine/renderer/Vulkan). The engine loads
// this DLL and calls registerGameBehaviors() to register them into Core's
// BehaviorRegistry by name; ScriptComponents reference behaviors by that name, so
// a recompiled+reloaded module reconnects with no state loss (state lives in
// components).

#include "game/GameModule.h"

#include <memory>

#include "core/InputActions.h"
#include "ecs/Registry.h"
#include "physics/CollisionEvent.h"
#include "scene/Behavior.h"
#include "scene/BehaviorRegistry.h"

namespace {

constexpr float PlayerMoveSpeed = 3.0f;

// Impacts softer than this don't make a sound — keeps resting/sliding contacts
// (impulse ~ 0) from machine-gunning the clip every fixed step.
constexpr float CollisionSfxMinImpulse = 0.3f;

// Spins the entity about its local Y axis. State-free.
class SpinnerBehavior : public Behavior {
public:
    void onUpdate(Registry& registry, Entity self, float deltaTime) override {
        if (registry.transforms.has(self)) {
            auto& rotation = registry.transforms.get(self).transform.rotation;
            rotation = glm::normalize(rotation * glm::angleAxis(deltaTime * 0.85f, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
    }
};

// Moves the entity on the XZ plane from the "MoveForward"/"MoveRight" input axes.
class PlayerControllerBehavior : public Behavior {
public:
    void onUpdate(Registry& registry, Entity self, float deltaTime) override {
        if (!registry.transforms.has(self)) {
            return;
        }
        const float forward = InputActions::getAxis("MoveForward");
        const float right = InputActions::getAxis("MoveRight");
        auto& transform = registry.transforms.get(self).transform;
        transform.position.x += right * PlayerMoveSpeed * deltaTime;
        transform.position.z -= forward * PlayerMoveSpeed * deltaTime; // -Z is forward
    }
};

// Plays the entity's AudioSource as a one-shot on a hard enough impact.
class CollisionSfxBehavior : public Behavior {
public:
    void onCollision(Registry& registry, Entity self, const CollisionEvent& event) override {
        if (event.impulse < CollisionSfxMinImpulse) {
            return;
        }
        if (registry.audioSources.has(self)) {
            registry.audioSources.get(self).oneShotPending = true;
        }
    }
};

} // namespace

// Entry point the engine resolves and calls after loading the DLL. Idempotent, so
// reloading (register again after clear) is safe.
SUGAR_GAME_EXPORT void registerGameBehaviors() {
    if (!BehaviorRegistry::has("Spinner")) {
        BehaviorRegistry::registerBehavior("Spinner", std::make_unique<SpinnerBehavior>());
    }
    if (!BehaviorRegistry::has("PlayerController")) {
        BehaviorRegistry::registerBehavior("PlayerController", std::make_unique<PlayerControllerBehavior>());
    }
    if (!BehaviorRegistry::has("CollisionSfx")) {
        BehaviorRegistry::registerBehavior("CollisionSfx", std::make_unique<CollisionSfxBehavior>());
    }
}
