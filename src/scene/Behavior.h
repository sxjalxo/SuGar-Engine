#pragma once

#include "ecs/Entity.h"

class Registry;
struct CollisionEvent;

// A Behavior is *stateless* gameplay logic, looked up by name and shared across
// every entity that references it. All per-entity state must live in components
// (see ScriptComponent + the entity's other components), never in fields on a
// Behavior subclass. That rule is what makes future native code hot-reload
// (recompile + reconnect by name, migrate state via serialization) mechanical
// rather than a rewrite. See ROADMAP.md "design principles".
class Behavior {
public:
    virtual ~Behavior() = default;

    // Called once, the first frame the owning entity is ticked in Play mode.
    virtual void onStart(Registry& registry, Entity self) {
        (void)registry;
        (void)self;
    }

    // Called every fixed gameplay step while in Play mode.
    virtual void onUpdate(Registry& registry, Entity self, float deltaTime) {
        (void)registry;
        (void)self;
        (void)deltaTime;
    }

    // Called for each contact involving `self`, after physics resolves the step.
    // `event.a`/`event.b` identify the pair; `self` is whichever one this behavior
    // is attached to. The single hook behind landing sounds, destruction, etc.
    virtual void onCollision(Registry& registry, Entity self, const CollisionEvent& event) {
        (void)registry;
        (void)self;
        (void)event;
    }
};
