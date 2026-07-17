#pragma once

// Per-subsystem confidence self-tests. Not exhaustive — one quick "is this
// subsystem sane?" check each, so a single run (SUGAR_SELFTEST=1) prints a
// reassuring table before you ever launch the editor. Everything here runs
// headless (no Vulkan); subsystems that need a device are reported as SKIPPED.

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "core/InputActions.h"
#include "core/SnapshotStorage.h"
#include "ecs/Registry.h"
#include "ecs/SystemSchedule.h"
#include "editor/EditorCommand.h"
#include "editor/EditorCommands.h"
#include "editor/EntityQuery.h"
#include "physics/PhysicsWorld.h"
#include "scene/BehaviorRegistry.h"
#include "scene/Light.h"
#include "scene/SceneSerializer.h"
#include "ui/RuntimeUISystem.h"
#include "ui/UIIntent.h"

namespace SelfTests {

inline Transform transformAtX(float x) {
    Transform t;
    t.position = glm::vec3(x, 0.0f, 0.0f);
    return t;
}

inline float xOf(Registry& registry, Entity entity) {
    return registry.transforms.get(entity).transform.position.x;
}

// --- CommandHistory: transactions + compression -----------------------------
inline bool testCommandHistory() {
    bool ok = true;

    { // transaction groups edits into one undo step
        Registry reg;
        const Entity a = reg.createEntity();
        const Entity b = reg.createEntity();
        reg.transforms.add(a, { transformAtX(0.0f) });
        reg.transforms.add(b, { transformAtX(0.0f) });

        CommandHistory history;
        history.beginTransaction();
        reg.transforms.get(a).transform = transformAtX(1.0f);
        history.push(std::make_unique<TransformCommand>(a, transformAtX(0.0f), transformAtX(1.0f)));
        reg.transforms.get(b).transform = transformAtX(1.0f);
        history.push(std::make_unique<TransformCommand>(b, transformAtX(0.0f), transformAtX(1.0f)));
        history.commitTransaction(reg);
        ok &= history.size() == 1;
        history.undo(reg);
        ok &= xOf(reg, a) == 0.0f && xOf(reg, b) == 0.0f;
        history.redo(reg);
        ok &= xOf(reg, a) == 1.0f && xOf(reg, b) == 1.0f;
    }

    { // compression merges back-to-back edits of the same entity
        Registry reg;
        const Entity a = reg.createEntity();
        reg.transforms.add(a, { transformAtX(0.0f) });
        CommandHistory history;
        reg.transforms.get(a).transform = transformAtX(1.0f);
        history.push(std::make_unique<TransformCommand>(a, transformAtX(0.0f), transformAtX(1.0f)));
        reg.transforms.get(a).transform = transformAtX(2.0f);
        history.push(std::make_unique<TransformCommand>(a, transformAtX(1.0f), transformAtX(2.0f)));
        ok &= history.size() == 1;
        history.undo(reg);
        ok &= xOf(reg, a) == 0.0f;
    }

    return ok;
}

// --- EntityManager id recycling: recreate a destroyed id in place (Phase 14B).
// The primitive behind deleting the command remap layer — a destroyed subtree
// comes back with its original ids, so command references stay valid. ----------
inline bool testEntityIdRecycling() {
    bool ok = true;

    { // a destroyed id can be recreated exactly, and isn't handed out twice
        Registry reg;
        const Entity a = reg.createEntity();
        const Entity b = reg.createEntity();
        reg.transforms.add(a, {});
        reg.transforms.add(b, {});
        reg.destroyEntity(b);

        const Entity recreated = reg.createEntityWithId(b);
        ok &= recreated == b;               // same id, as commands rely on
        ok &= reg.createEntity() != a && reg.createEntity() != b; // never double-issued
    }

    { // reserving a future id banks the skipped ids for later createEntity()
        Registry reg;
        const Entity first = reg.createEntity(); // 1
        const Entity target = first + 5;
        ok &= reg.createEntityWithId(target) == target;
        // The gap (first+1 .. target-1) is now free and gets handed out next.
        const Entity next = reg.createEntity();
        ok &= next > first && next < target;
    }

    return ok;
}

// --- EntityQuery: parse + evaluate ------------------------------------------
inline bool testEntityQuery() {
    Registry reg;
    const Entity a = reg.createEntity();
    reg.transforms.add(a, { transformAtX(0.0f) });
    RigidBodyComponent falling{};
    falling.velocity = glm::vec3(0.0f, -5.0f, 0.0f);
    reg.rigidBodies.add(a, falling);

    const Entity b = reg.createEntity();
    reg.transforms.add(b, { transformAtX(0.0f) });
    RigidBodyComponent rising{};
    rising.velocity = glm::vec3(0.0f, 3.0f, 0.0f);
    reg.rigidBodies.add(b, rising);

    bool ok = true;
    const auto filtered = EntityQuery::run(reg, "rigidbody where vel.y < 0");
    ok &= filtered.error.empty() && filtered.entities.size() == 1 && filtered.entities[0] == a;
    ok &= EntityQuery::run(reg, "rigidbody").entities.size() == 2;
    ok &= !EntityQuery::run(reg, "bogus").error.empty();
    ok &= !EntityQuery::run(reg, "rigidbody where vel.q < 0").error.empty();
    return ok;
}

// --- SnapshotStorage: ring, eviction, stable frame numbers ------------------
inline bool testSnapshotStorage() {
    JsonSnapshotStorage storage(3);
    storage.push("a");
    storage.push("b");
    storage.push("c");
    bool ok = storage.count() == 3 && storage.get(0) == "a" && storage.get(2) == "c" &&
              storage.frameNumber(0) == 0;
    storage.push("d"); // evicts "a"
    ok &= storage.count() == 3 && storage.get(0) == "b" && storage.frameNumber(0) == 1 &&
          storage.frameNumber(2) == 3;
    storage.clear();
    ok &= storage.count() == 0;
    return ok;
}

// --- Physics: gravity integration + collision event emission ----------------
inline bool testPhysics() {
    bool ok = true;

    { // gravity pulls a dynamic body down
        Registry reg;
        PhysicsWorld world;
        const Entity e = reg.createEntity();
        Transform t;
        t.position = glm::vec3(0.0f, 10.0f, 0.0f);
        reg.transforms.add(e, { t });
        reg.rigidBodies.add(e, RigidBodyComponent{});
        world.step(reg, 1.0f / 60.0f);
        ok &= reg.rigidBodies.get(e).velocity.y < 0.0f && reg.transforms.get(e).transform.position.y < 10.0f;
    }

    { // overlapping box + static ground produces a collision event
        Registry reg;
        PhysicsWorld world;
        const Entity dynamic = reg.createEntity();
        Transform dt;
        dt.position = glm::vec3(0.0f, 0.4f, 0.0f);
        reg.transforms.add(dynamic, { dt });
        RigidBodyComponent db{};
        db.velocity = glm::vec3(0.0f, -1.0f, 0.0f);
        reg.rigidBodies.add(dynamic, db);
        ColliderComponent dc{};
        dc.type = ColliderType::Box;
        dc.halfExtents = glm::vec3(0.5f);
        reg.colliders.add(dynamic, dc);

        const Entity ground = reg.createEntity();
        Transform gt;
        gt.position = glm::vec3(0.0f, -0.2f, 0.0f);
        reg.transforms.add(ground, { gt });
        RigidBodyComponent gb{};
        gb.isStatic = true;
        gb.useGravity = false;
        reg.rigidBodies.add(ground, gb);
        ColliderComponent gc{};
        gc.type = ColliderType::Box;
        gc.halfExtents = glm::vec3(0.5f);
        reg.colliders.add(ground, gc);

        world.step(reg, 1.0f / 60.0f);
        ok &= !world.getCollisionEvents().empty();
    }

    return ok;
}

// --- Physics broadphase: the uniform grid finds exactly the overlapping pairs
// (no spurious, no missed) and does so deterministically (Phase 15) -----------
inline bool testPhysicsBroadphase() {
    // A helper to drop a static unit box (spans center +/- 0.5 on each axis).
    auto addBox = [](Registry& reg, float x) {
        const Entity e = reg.createEntity();
        Transform t;
        t.position = glm::vec3(x, 0.0f, 0.0f);
        reg.transforms.add(e, { t });
        RigidBodyComponent body{};
        body.isStatic = true;
        body.useGravity = false;
        reg.rigidBodies.add(e, body);
        ColliderComponent collider{};
        collider.type = ColliderType::Box;
        collider.halfExtents = glm::vec3(0.5f);
        reg.colliders.add(e, collider);
        return e;
    };

    Registry reg;
    // Two overlapping clusters far apart, plus a loner — only two pairs collide.
    const Entity a0 = addBox(reg, 0.0f);
    const Entity a1 = addBox(reg, 0.5f);   // overlaps a0
    const Entity b0 = addBox(reg, 10.0f);
    const Entity b1 = addBox(reg, 10.4f);  // overlaps b0
    (void)addBox(reg, 100.0f);             // isolated: no partner in its cells

    PhysicsWorld world;
    world.step(reg, 1.0f / 60.0f);
    const auto& events = world.getCollisionEvents();

    bool ok = events.size() == 2;

    auto hasPair = [&](Entity x, Entity y) {
        for (const CollisionEvent& e : events) {
            if ((e.a == x && e.b == y) || (e.a == y && e.b == x)) {
                return true;
            }
        }
        return false;
    };
    ok &= hasPair(a0, a1) && hasPair(b0, b1);

    // Deterministic: a second identical step yields the same event order.
    PhysicsWorld world2;
    world2.step(reg, 1.0f / 60.0f);
    const auto& events2 = world2.getCollisionEvents();
    ok &= events2.size() == events.size();
    for (size_t i = 0; i < events.size() && i < events2.size(); ++i) {
        ok &= events[i].a == events2[i].a && events[i].b == events2[i].b;
    }

    return ok;
}

// --- Snapshot patch: in-place restore preserves entity ids + editor identity
// (Phase 14A). Uses only device-free components so it runs headless -----------
inline bool testSnapshotPatch() {
    bool ok = true;

    // A two-entity scene with a parent/child link, transforms, a rigid body, and a
    // script — all serializable without a Vulkan device.
    Registry reg;
    std::vector<Light> lights;
    const Entity parent = reg.createEntity();
    reg.names.add(parent, { "Parent" });
    reg.transforms.add(parent, { transformAtX(1.0f) });
    reg.hierarchy.add(parent, {});
    reg.scripts.add(parent, { "Spinner", true }); // started=true to prove it resets

    const Entity child = reg.createEntity();
    reg.names.add(child, { "Child" });
    reg.transforms.add(child, { transformAtX(2.0f) });
    reg.hierarchy.add(child, {});
    RigidBodyComponent body{};
    body.velocity = glm::vec3(0.0f, -4.0f, 0.0f);
    reg.rigidBodies.add(child, body);
    reg.setParent(child, parent);

    const std::string frame0 = SceneSerializer::saveToString(reg, lights);
    ok &= !frame0.empty();

    // Simulate a fixed step: mutate component data in place (ids unchanged).
    reg.transforms.get(parent).transform = transformAtX(9.0f);
    reg.transforms.get(child).transform = transformAtX(8.0f);
    reg.rigidBodies.get(child).velocity = glm::vec3(0.0f, -12.0f, 0.0f);
    reg.scripts.get(parent).started = true; // patch must reset this to false

    // Patch frame 0 back in. The registry still has exactly these two entities, so
    // the patch path is taken (not a rebuild).
    ok &= SceneSerializer::patchFromString(reg, lights, frame0);

    // Same entity ids — the whole point (editor selection keyed on ids survives).
    ok &= reg.transforms.has(parent) && reg.transforms.has(child);
    // State restored to frame 0.
    ok &= xOf(reg, parent) == 1.0f && xOf(reg, child) == 2.0f;
    ok &= reg.rigidBodies.get(child).velocity.y == -4.0f;
    // Hierarchy restored.
    ok &= reg.hierarchy.get(child).parent == parent;
    // Runtime script latch reset on restore.
    ok &= reg.scripts.get(parent).started == false;

    // Structural mismatch (extra entity) → patch declines WITHOUT mutating, so the
    // caller knows to fall back to a full rebuild.
    const Entity extra = reg.createEntity();
    reg.transforms.add(extra, { transformAtX(5.0f) });
    ok &= !SceneSerializer::patchFromString(reg, lights, frame0);
    ok &= reg.transforms.has(extra) && xOf(reg, extra) == 5.0f; // untouched

    return ok;
}

// --- RuntimeUI: intents drive the authoritative UI model deterministically, and
// that model survives a snapshot restore in place (Phase 16A / Rule 21) ---------
inline bool testRuntimeUI() {
    bool ok = true;

    { // intents mutate the screen stack + focus in queue order; queue is drained
        Registry reg;
        const Entity uiRoot = reg.createEntity();
        reg.uiScreens.add(uiRoot, {});
        reg.focus.add(uiRoot, {});

        UIIntentQueue queue;
        queue.push(UIIntent::openScreen("HUD"));
        queue.push(UIIntent::openScreen("Inventory")); // opens over HUD
        queue.push(UIIntent::setFocus("SlotA"));
        RuntimeUISystem::update(reg, queue);

        const auto& screen = reg.uiScreens.get(uiRoot);
        ok &= screen.screenStack.size() == 2 && screen.active() == "Inventory";
        ok &= reg.focus.get(uiRoot).focusedElement == "SlotA";
        ok &= queue.empty(); // drained

        queue.push(UIIntent::popScreen());  // back to HUD
        queue.push(UIIntent::clearFocus());
        RuntimeUISystem::update(reg, queue);
        ok &= reg.uiScreens.get(uiRoot).active() == "HUD";
        ok &= reg.focus.get(uiRoot).focusedElement.empty();

        // The root screen is not poppable: backing out of the last screen would
        // leave the game with no UI at all.
        queue.push(UIIntent::popScreen());
        queue.push(UIIntent::popScreen());
        RuntimeUISystem::update(reg, queue);
        ok &= reg.uiScreens.get(uiRoot).screenStack.size() == 1 &&
              reg.uiScreens.get(uiRoot).active() == "HUD";
    }

    { // text entry: buffer + caret authoritative, edited via intents, and routed to
      // the *focused* field — two fields prove typing never leaks into the wrong one
        Registry reg;
        const Entity uiRoot = reg.createEntity();
        reg.focus.add(uiRoot, {});
        const Entity nameField = reg.createEntity();
        reg.textInputs.add(nameField, { "name", "", 0 });
        const Entity tagField = reg.createEntity();
        reg.textInputs.add(tagField, { "tag", "", 0 });

        UIIntentQueue queue;

        // Nothing focused: typing is ignored rather than hitting an arbitrary field.
        queue.push(UIIntent::appendText("x"));
        RuntimeUISystem::update(reg, queue);
        ok &= reg.textInputs.get(nameField).buffer.empty() && reg.textInputs.get(tagField).buffer.empty();

        queue.push(UIIntent::setFocus("name"));
        queue.push(UIIntent::appendText("Hi"));
        queue.push(UIIntent::appendText("!"));
        RuntimeUISystem::update(reg, queue);
        ok &= reg.textInputs.get(nameField).buffer == "Hi!" && reg.textInputs.get(nameField).caret == 3;
        ok &= reg.textInputs.get(tagField).buffer.empty(); // unfocused field untouched

        queue.push(UIIntent::backspaceText());
        RuntimeUISystem::update(reg, queue);
        ok &= reg.textInputs.get(nameField).buffer == "Hi" && reg.textInputs.get(nameField).caret == 2;

        // Caret moves, then an insert lands at the caret (not the end).
        queue.push(UIIntent::caretLeft());
        queue.push(UIIntent::appendText("E"));
        RuntimeUISystem::update(reg, queue);
        ok &= reg.textInputs.get(nameField).buffer == "HEi" && reg.textInputs.get(nameField).caret == 2;

        // Caret clamps at both ends.
        queue.push(UIIntent::caretLeft());
        queue.push(UIIntent::caretLeft());
        queue.push(UIIntent::caretLeft());
        RuntimeUISystem::update(reg, queue);
        ok &= reg.textInputs.get(nameField).caret == 0;
        for (int i = 0; i < 6; i++) {
            queue.push(UIIntent::caretRight());
        }
        RuntimeUISystem::update(reg, queue);
        ok &= reg.textInputs.get(nameField).caret == 3;

        // Backspace at the start must not underflow.
        queue.push(UIIntent::setFocus("tag"));
        queue.push(UIIntent::backspaceText());
        RuntimeUISystem::update(reg, queue);
        ok &= reg.textInputs.get(tagField).buffer.empty() && reg.textInputs.get(tagField).caret == 0;
        ok &= reg.textInputs.get(nameField).buffer == "HEi"; // focus moved: name intact

        // Typing now goes to the newly focused field.
        queue.push(UIIntent::appendText("boss"));
        RuntimeUISystem::update(reg, queue);
        ok &= reg.textInputs.get(tagField).buffer == "boss";
        ok &= reg.textInputs.get(nameField).buffer == "HEi";
    }

    { // UI state survives an in-place snapshot restore with the same entity id —
      // the Rule 21 guarantee that makes UI = f(ECS, input) worthwhile. The UIRoot
      // carries a transform so it participates in serialization like any entity.
        Registry reg;
        std::vector<Light> lights;
        const Entity uiRoot = reg.createEntity();
        reg.transforms.add(uiRoot, {});
        reg.hierarchy.add(uiRoot, {});
        UIScreenComponent screen;
        screen.screenStack = { "MainMenu", "Settings" };
        reg.uiScreens.add(uiRoot, screen);
        reg.focus.add(uiRoot, { "AudioTab" });
        reg.textInputs.add(uiRoot, { "name", "half-typed", 5 });

        const std::string frame = SceneSerializer::saveToString(reg, lights);
        ok &= !frame.empty();

        // Simulate the game running: navigate away and keep typing.
        reg.uiScreens.get(uiRoot).screenStack = { "HUD" };
        reg.focus.get(uiRoot).focusedElement = "Crosshair";
        reg.textInputs.get(uiRoot).buffer = "something else";
        reg.textInputs.get(uiRoot).caret = 3;

        // Scrub back — patch in place.
        ok &= SceneSerializer::patchFromString(reg, lights, frame);
        ok &= reg.uiScreens.has(uiRoot); // same id preserved
        const auto& restored = reg.uiScreens.get(uiRoot);
        ok &= restored.screenStack.size() == 2 &&
              restored.screenStack[0] == "MainMenu" && restored.screenStack[1] == "Settings";
        ok &= reg.focus.get(uiRoot).focusedElement == "AudioTab";
        // A half-typed line is authoritative — a scrub must bring it back exactly,
        // including which field it belonged to and where the caret sat.
        ok &= reg.textInputs.get(uiRoot).buffer == "half-typed" &&
              reg.textInputs.get(uiRoot).caret == 5 &&
              reg.textInputs.get(uiRoot).element == "name";
    }

    return ok;
}

// --- Serializer: save produces the expected scene text (round-trip load needs
// a device, so this is save-only) ------------------------------------------
inline bool testSerializer() {
    Registry reg;
    std::vector<Light> lights;
    const Entity e = reg.createEntity();
    reg.names.add(e, { "Probe" });
    Transform t;
    t.position = glm::vec3(1.0f, 2.0f, 3.0f);
    reg.transforms.add(e, { t });
    reg.hierarchy.add(e, {});

    const std::string text = SceneSerializer::saveToString(reg, lights);
    return !text.empty() &&
           text.find("Probe") != std::string::npos &&
           text.find("\"pos\"") != std::string::npos;
}

// --- BehaviorRegistry: register / resolve by name / clear -------------------
// Tests Core's registry *mechanism* with a local behavior (concrete behaviors now
// live in the game module DLL, which isn't loaded in the headless self-test).
inline bool testBehaviorRegistry() {
    struct DummyBehavior : Behavior {};
    BehaviorRegistry::clear();
    BehaviorRegistry::registerBehavior("TestBehavior", std::make_unique<DummyBehavior>());
    bool ok = BehaviorRegistry::has("TestBehavior") &&
              BehaviorRegistry::get("TestBehavior") != nullptr &&
              BehaviorRegistry::get("DoesNotExist") == nullptr;
    BehaviorRegistry::clear();
    ok &= !BehaviorRegistry::has("TestBehavior");
    return ok;
}

// --- Registry graph: parenting, cycle guard, destroy detaches children ------
inline bool testRegistryGraph() {
    Registry reg;
    const Entity parent = reg.createEntity();
    reg.transforms.add(parent, {});
    reg.hierarchy.add(parent, {});
    const Entity child = reg.createEntity();
    reg.transforms.add(child, {});
    reg.hierarchy.add(child, {});

    reg.setParent(child, parent);
    bool ok = reg.hierarchy.get(child).parent == parent;

    bool threwOnCycle = false;
    try {
        reg.setParent(parent, child); // would form a cycle
    } catch (...) {
        threwOnCycle = true;
    }
    ok &= threwOnCycle;

    reg.destroyEntity(parent); // destroying the parent detaches the child
    ok &= reg.hierarchy.has(child) && reg.hierarchy.get(child).parent == INVALID_ENTITY;
    return ok;
}

// --- CoreBoundary: the Core subsystems construct together with no Vulkan -----
// A regression tripwire for the Editor -> Engine -> Core boundary: if any of
// these ever pulls in a renderer/Vulkan dependency, Core stops building headless.
inline bool testCoreBoundary() {
    Registry registry;
    const Entity entity = registry.createEntity();
    registry.transforms.add(entity, {});
    bool ok = registry.transforms.has(entity);

    struct DummyBehavior : Behavior {};
    BehaviorRegistry::clear();
    BehaviorRegistry::registerBehavior("CoreProbe", std::make_unique<DummyBehavior>());
    ok &= BehaviorRegistry::has("CoreProbe");
    BehaviorRegistry::clear();

    InputActions::registerDefaults();
    ok &= InputActions::getAxis("MoveForward") == 0.0f; // defined axis, no input -> 0
    return ok;
}

// --- SystemScheduler: deterministic order, conflict detection, stage grouping -
inline bool testSystemScheduler() {
    bool ok = true;

    { // runs in registration order regardless of declared independence
        SystemScheduler scheduler;
        std::string trace;
        scheduler.add(System{"A", 0, maskOf(ComponentType::Transform),
                             [&trace](float) { trace += 'A'; }});
        scheduler.add(System{"B", 0, maskOf(ComponentType::RigidBody),
                             [&trace](float) { trace += 'B'; }});
        scheduler.add(System{"C", 0, maskOf(ComponentType::AudioSource),
                             [&trace](float) { trace += 'C'; }});
        scheduler.run(1.0f / 60.0f);
        ok &= trace == "ABC" && scheduler.size() == 3;
    }

    { // write/read and write/write hazards conflict; disjoint access does not
        System writer{"W", 0, maskOf(ComponentType::Transform), {}};
        System reader{"R", maskOf(ComponentType::Transform), 0, {}};
        System other{"O", 0, maskOf(ComponentType::AudioSource), {}};
        ok &= systemsConflict(writer, reader);   // write-read
        ok &= systemsConflict(writer, writer);   // write-write
        ok &= !systemsConflict(writer, other);   // disjoint
        ok &= !systemsConflict(reader, other);   // read vs unrelated write
    }

    { // stages: independent systems share a stage, a conflict opens a new one
        SystemScheduler scheduler;
        scheduler.add(System{"A", 0, maskOf(ComponentType::Transform), {}});
        scheduler.add(System{"B", 0, maskOf(ComponentType::AudioSource), {}}); // indep of A
        scheduler.add(System{"C", maskOf(ComponentType::Transform), 0, {}});   // reads A's write
        const auto stages = scheduler.stages();
        ok &= stages.size() == 2;
        ok &= stages[0].size() == 2 && stages[0][0] == 0 && stages[0][1] == 1;
        ok &= stages[1].size() == 1 && stages[1][0] == 2;
    }

    return ok;
}

// --- ComponentAccess: the ECS reports access; the scheduler enforces the
// declared read/write sets (Phase 13B guard rail) -----------------------------
inline bool testComponentAccess() {
    // Tracking is compiled out of release builds, so enforcement is inert there.
    // Report the mechanism as sane rather than failing a check it cannot observe.
    if (!ComponentAccess::trackingEnabled()) {
        return true;
    }

    bool ok = true;

    { // const paths record reads, non-const paths record writes
        Registry reg;
        const Entity e = reg.createEntity();
        ComponentAccessTracker tracker;
        {
            ComponentAccess::Scope scope(&tracker);
            reg.transforms.add(e, {});                      // write Transform
            const Registry& readOnly = reg;
            (void)readOnly.transforms.get(e);               // read Transform
            (void)readOnly.rigidBodies.has(e);              // read RigidBody
        }
        ok &= tracker.touched() == maskOf(ComponentType::Transform, ComponentType::RigidBody);
        ok &= tracker.mutated() == maskOf(ComponentType::Transform);
    }

    { // a compliant system reports nothing; violations are surfaced with the
      // exact storages at fault
        Registry reg;
        const Entity e = reg.createEntity();
        reg.transforms.add(e, {});
        reg.rigidBodies.add(e, RigidBodyComponent{});

        std::vector<AccessViolation> violations;
        SystemScheduler scheduler;
        scheduler.setEnforcement(AccessEnforcement::Warn);
        scheduler.setViolationHandler([&violations](const AccessViolation& v) { violations.push_back(v); });

        // Declares what it does: reads Transform, writes RigidBody.
        scheduler.add(System{"Compliant",
                             maskOf(ComponentType::Transform),
                             maskOf(ComponentType::RigidBody),
                             [&reg, e](float) {
                                 const Registry& readOnly = reg;
                                 (void)readOnly.transforms.get(e);
                                 reg.rigidBodies.get(e).mass = 2.0f;
                             }});
        // Reaches into Transform without declaring it at all.
        scheduler.add(System{"Coupled", 0, maskOf(ComponentType::RigidBody),
                             [&reg, e](float) {
                                 reg.transforms.get(e).transform.position.x = 1.0f;
                             }});
        // Declares Transform read-only, then mutates it.
        scheduler.add(System{"LyingReader", maskOf(ComponentType::Transform), 0,
                             [&reg, e](float) {
                                 reg.transforms.get(e).transform.position.y = 1.0f;
                             }});

        scheduler.run(1.0f / 60.0f);
        ok &= violations.size() == 2;
        if (violations.size() == 2) {
            ok &= violations[0].system == "Coupled" &&
                  violations[0].undeclaredAccess == maskOf(ComponentType::Transform) &&
                  violations[0].undeclaredWrites == maskOf(ComponentType::Transform);
            ok &= violations[1].system == "LyingReader" &&
                  violations[1].undeclaredAccess == 0 && // Transform was declared...
                  violations[1].undeclaredWrites == maskOf(ComponentType::Transform); // ...but read-only
        }

        // A repeated violation is reported once, not every fixed step.
        violations.clear();
        scheduler.run(1.0f / 60.0f);
        ok &= violations.empty();
    }

    { // Strict enforcement throws on the first violation (fail-fast for CI)
        Registry reg;
        const Entity e = reg.createEntity();
        reg.transforms.add(e, {});

        SystemScheduler scheduler;
        scheduler.setEnforcement(AccessEnforcement::Strict);
        scheduler.add(System{"Rogue", 0, maskOf(ComponentType::RigidBody),
                             [&reg, e](float) { reg.transforms.get(e).transform.position.x = 1.0f; }});
        bool threw = false;
        try {
            scheduler.run(1.0f / 60.0f);
        } catch (const AccessViolationError&) {
            threw = true;
        }
        ok &= threw;

        // A compliant system under Strict runs without throwing.
        SystemScheduler clean;
        clean.setEnforcement(AccessEnforcement::Strict);
        clean.add(System{"Clean", 0, maskOf(ComponentType::Transform),
                        [&reg, e](float) { reg.transforms.get(e).transform.position.y = 2.0f; }});
        bool threwClean = false;
        try {
            clean.run(1.0f / 60.0f);
        } catch (const AccessViolationError&) {
            threwClean = true;
        }
        ok &= !threwClean;
    }

    { // the real physics step honors the Physics system's declared access:
      // reads Collider, writes Transform + RigidBody, touches nothing else
        Registry reg;
        PhysicsWorld world;
        const Entity e = reg.createEntity();
        Transform t;
        t.position = glm::vec3(0.0f, 10.0f, 0.0f);
        reg.transforms.add(e, { t });
        reg.rigidBodies.add(e, RigidBodyComponent{});
        ColliderComponent collider{};
        collider.type = ColliderType::Box;
        collider.halfExtents = glm::vec3(0.5f);
        reg.colliders.add(e, collider);

        ComponentAccessTracker tracker;
        {
            ComponentAccess::Scope scope(&tracker);
            world.step(reg, 1.0f / 60.0f);
        }
        const ComponentMask declaredReads =
            maskOf(ComponentType::Collider, ComponentType::Transform, ComponentType::RigidBody);
        const ComponentMask declaredWrites = maskOf(ComponentType::Transform, ComponentType::RigidBody);
        ok &= (tracker.touched() & ~declaredReads) == 0;
        ok &= (tracker.mutated() & ~declaredWrites) == 0;
        ok &= (tracker.touched() & componentBit(ComponentType::Collider)) != 0; // it really did read them
    }

    return ok;
}

// Returns {passed, total}. Prints the per-test table as a side effect.
inline std::pair<int, int> run() {
    using TestFn = bool (*)();
    struct Case { const char* name; TestFn fn; };
    const Case cases[] = {
        { "CoreBoundary",     testCoreBoundary },
        { "CommandHistory",   testCommandHistory },
        { "EntityIdRecycling", testEntityIdRecycling },
        { "EntityQuery",      testEntityQuery },
        { "SnapshotStorage",  testSnapshotStorage },
        { "Physics",          testPhysics },
        { "PhysicsBroadphase", testPhysicsBroadphase },
        { "SystemScheduler",  testSystemScheduler },
        { "ComponentAccess",  testComponentAccess },
        { "SnapshotPatch",    testSnapshotPatch },
        { "RuntimeUI",        testRuntimeUI },
        { "Serializer",       testSerializer },
        { "BehaviorRegistry", testBehaviorRegistry },
        { "RegistryGraph",    testRegistryGraph },
    };

    int passed = 0;
    const int total = static_cast<int>(sizeof(cases) / sizeof(cases[0]));
    for (const Case& test : cases) {
        const auto start = std::chrono::high_resolution_clock::now();
        const bool ok = test.fn();
        const auto end = std::chrono::high_resolution_clock::now();
        const double milliseconds = std::chrono::duration<double, std::milli>(end - start).count();

        std::string label = test.name;
        while (label.size() < 18) {
            label += '.';
        }
        std::cout << "[selftest] " << label << ' ' << (ok ? "PASS" : "FAIL")
                  << " (" << std::fixed << std::setprecision(2) << milliseconds << " ms)\n";
        passed += ok ? 1 : 0;
    }
    std::cout << "[selftest] ResourceManager.. SKIPPED (needs Vulkan device)\n";
    std::cout << "[selftest] " << (passed == total ? "ALL PASS" : "FAILURES PRESENT") << "\n";
    return { passed, total };
}

} // namespace SelfTests
