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

#include "core/SnapshotStorage.h"
#include "ecs/Registry.h"
#include "editor/EditorCommand.h"
#include "editor/EditorCommands.h"
#include "editor/EntityQuery.h"
#include "physics/PhysicsWorld.h"
#include "scene/BehaviorRegistry.h"
#include "scene/Light.h"
#include "scene/SceneSerializer.h"

namespace SelfTests {

inline Transform transformAtX(float x) {
    Transform t;
    t.position = glm::vec3(x, 0.0f, 0.0f);
    return t;
}

inline float xOf(Registry& registry, Entity entity) {
    return registry.transforms.get(entity).transform.position.x;
}

// Test-only command that emits a fixed remap from redo, to exercise the history's
// remap propagation without the serializer/ResourceManager.
class RemapEmitter : public EditorCommand {
public:
    explicit RemapEmitter(EntityRemap mapping) : mapping(std::move(mapping)) {}
    EntityRemap undo(Registry&) override { return {}; }
    EntityRemap redo(Registry&) override { return mapping; }

private:
    EntityRemap mapping;
};

// --- CommandHistory: transactions, compression, entity remapping ------------
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

    { // remap repoints an older command after a recreate reassigns ids
        Registry reg;
        const Entity a = reg.createEntity();
        const Entity b = reg.createEntity();
        reg.transforms.add(a, { transformAtX(0.0f) });
        reg.transforms.add(b, { transformAtX(0.0f) });
        CommandHistory history;
        reg.transforms.get(a).transform = transformAtX(1.0f);
        history.push(std::make_unique<TransformCommand>(a, transformAtX(0.0f), transformAtX(1.0f)));
        history.push(std::make_unique<RemapEmitter>(EntityRemap{ { a, b } }));
        history.undo(reg); // emitter
        history.undo(reg); // transform (a -> 0)
        history.redo(reg); // transform (a -> 1)
        history.redo(reg); // emitter remaps transform a -> b
        history.undo(reg); // emitter
        history.undo(reg); // transform now targets b -> 0
        ok &= xOf(reg, b) == 0.0f && xOf(reg, a) == 1.0f;
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
inline bool testBehaviorRegistry() {
    BehaviorRegistry::clear();
    BehaviorRegistry::registerBuiltins();
    bool ok = BehaviorRegistry::has("Spinner") &&
              BehaviorRegistry::has("PlayerController") &&
              BehaviorRegistry::has("CollisionSfx") &&
              BehaviorRegistry::get("Spinner") != nullptr &&
              BehaviorRegistry::get("DoesNotExist") == nullptr;
    BehaviorRegistry::clear();
    ok &= !BehaviorRegistry::has("Spinner");
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

inline bool run() {
    using TestFn = bool (*)();
    struct Case { const char* name; TestFn fn; };
    const Case cases[] = {
        { "CommandHistory",   testCommandHistory },
        { "EntityQuery",      testEntityQuery },
        { "SnapshotStorage",  testSnapshotStorage },
        { "Physics",          testPhysics },
        { "Serializer",       testSerializer },
        { "BehaviorRegistry", testBehaviorRegistry },
        { "RegistryGraph",    testRegistryGraph },
    };

    bool allOk = true;
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
        allOk &= ok;
    }
    std::cout << "[selftest] ResourceManager.. SKIPPED (needs Vulkan device)\n";
    std::cout << "[selftest] " << (allOk ? "ALL PASS" : "FAILURES PRESENT") << "\n";
    return allOk;
}

} // namespace SelfTests
