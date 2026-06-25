#include "scene/BehaviorRegistry.h"
#include "core/InputActions.h"
#include "ecs/Registry.h"
#include "physics/CollisionEvent.h"

namespace {

constexpr float PlayerMoveSpeed = 3.0f;

// Impacts softer than this don't make a sound — keeps resting/sliding contacts
// (impulse ~ 0) from machine-gunning the clip every fixed step.
constexpr float CollisionSfxMinImpulse = 0.3f;

// Built-in: spins the entity about its local Y axis. State-free — reads/writes
// only the entity's TransformComponent, so it is safe to share across entities
// and trivial to hot-reload.
class SpinnerBehavior : public Behavior {
public:
    void onUpdate(Registry& registry, Entity self, float deltaTime) override {
        if (registry.transforms.has(self)) {
            auto& rotation = registry.transforms.get(self).transform.rotation;
            // Post-multiply spins about the entity's local Y axis.
            rotation = glm::normalize(rotation * glm::angleAxis(deltaTime * 0.85f, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
    }
};

// Built-in: moves the entity on the XZ plane from the "MoveForward"/"MoveRight"
// input axes (arrow keys by default). State-free; demonstrates the full
// Play -> input -> gameplay loop. Only ticks in Play mode (updateSystems gating).
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

// Built-in: plays the entity's AudioSource as a one-shot on a hard enough impact.
// State-free (the trigger lives in the component); the canonical example of the
// CollisionEvent -> Behavior -> Audio chain. Footsteps/destruction follow the
// same shape.
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

void BehaviorRegistry::registerBuiltins() {
    if (!has("Spinner")) {
        registerBehavior("Spinner", std::make_unique<SpinnerBehavior>());
    }
    if (!has("PlayerController")) {
        registerBehavior("PlayerController", std::make_unique<PlayerControllerBehavior>());
    }
    if (!has("CollisionSfx")) {
        registerBehavior("CollisionSfx", std::make_unique<CollisionSfxBehavior>());
    }
}

void BehaviorRegistry::clear() {
    table().clear();
}
