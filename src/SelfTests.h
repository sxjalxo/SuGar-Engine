#pragma once

// Per-subsystem confidence self-tests. Not exhaustive — one quick "is this
// subsystem sane?" check each, so a single run (SUGAR_SELFTEST=1) prints a
// reassuring table before you ever launch the editor. Everything here runs
// headless (no Vulkan); subsystems that need a device are reported as SKIPPED.

#include <chrono>
#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "animation/AnimationClip.h"
#include "animation/AnimationClipRegistry.h"
#include "animation/AnimationComponents.h"
#include "animation/AnimationGraph.h"
#include "animation/AnimationGraphRegistry.h"
#include "animation/AnimationStateSystem.h"
#include "animation/AnimationSystem.h"
#include "animation/Pose.h"
#include "animation/Skin.h"
#include "animation/SkinRegistry.h"
#include "animation/Skinning.h"
#include "assets/GltfLoader.h"
#include "assets/GltfModel.h"
#include "assets/ModelImporter.h"
#include "core/InputActions.h"
#include "core/SnapshotStorage.h"
#include "ecs/Registry.h"
#include "ecs/SystemSchedule.h"
#include "editor/EditorCommand.h"
#include "editor/EditorCommands.h"
#include "editor/EntityQuery.h"
#include "physics/PhysicsWorld.h"
#include "rendering/Mesh.h"
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

// --- Animation: playback state is authoritative, the pose is derived --------
// The whole subsystem is testable headlessly because sampling is pure math over
// plain data in Core (docs/DESIGN_ANIMATION.md) — no GPU, no skinning, no assets.
inline bool testAnimation() {
    bool ok = true;
    constexpr float step = 1.0f / 60.0f;

    // A 1-second clip that slides "Arm" from x=0 to x=10 and scales it 1 -> 2.
    // Rotation is deliberately left empty: an absent channel must not stomp an
    // authored rotation.
    const auto makeSlideClip = []() {
        TransformTrack track;
        track.target = "Arm";
        track.translation.times = { 0.0f, 1.0f };
        track.translation.values = { glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f) };
        track.scale.times = { 0.0f, 1.0f };
        track.scale.values = { glm::vec3(1.0f), glm::vec3(2.0f) };

        AnimationClip clip;
        clip.name = "Slide";
        clip.tracks = { track };
        clip.duration = computeDuration(clip.tracks);
        return clip;
    };

    const auto nearly = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };

    { // sampling: endpoints, midpoint, out-of-range clamp, and Step vs Linear
        const AnimationClip clip = makeSlideClip();
        ok &= nearly(clip.duration, 1.0f); // derived from the data, never stored

        const TransformSample start = sampleTrack(clip.tracks[0], 0.0f);
        ok &= start.hasTranslation && nearly(start.translation.x, 0.0f);
        ok &= start.hasScale && nearly(start.scale.x, 1.0f);
        ok &= !start.hasRotation; // no channel -> reported absent, not defaulted

        const TransformSample mid = sampleTrack(clip.tracks[0], 0.5f);
        ok &= nearly(mid.translation.x, 5.0f) && nearly(mid.scale.x, 1.5f);

        // Times outside the range clamp to the end keys; wrapping is playback's job.
        ok &= nearly(sampleTrack(clip.tracks[0], -3.0f).translation.x, 0.0f);
        ok &= nearly(sampleTrack(clip.tracks[0], 9.0f).translation.x, 10.0f);

        // Step holds the previous key until the next one is reached.
        TransformTrack stepped = clip.tracks[0];
        stepped.translation.interpolation = Interpolation::Step;
        ok &= nearly(sampleTrack(stepped, 0.99f).translation.x, 0.0f);
        ok &= nearly(sampleTrack(stepped, 1.0f).translation.x, 10.0f);

        // Coincident keys have a zero-length span: must not divide by zero into NaN.
        TransformTrack degenerate;
        degenerate.translation.times = { 0.5f, 0.5f };
        degenerate.translation.values = { glm::vec3(1.0f), glm::vec3(2.0f) };
        const TransformSample sample = sampleTrack(degenerate, 0.5f);
        ok &= sample.hasTranslation && !std::isnan(sample.translation.x);
    }

    { // the system advances time on the fixed step and applies the pose to the
      // target named by the track, resolved through the hierarchy
        AnimationClipRegistry::clear();
        AnimationClipRegistry::registerClip("Slide", makeSlideClip());

        Registry reg;
        const Entity root = reg.createEntity();
        reg.names.add(root, { "Character" });
        reg.transforms.add(root, {});
        const Entity arm = reg.createEntity();
        reg.names.add(arm, { "Arm" });
        reg.transforms.add(arm, {});
        // An authored rotation the clip does not animate — it must survive.
        reg.transforms.get(arm).transform.rotation = glm::angleAxis(1.0f, glm::vec3(0.0f, 1.0f, 0.0f));
        reg.setParent(arm, root);

        AnimationPlayerComponent player;
        player.clip = "Slide";
        reg.animations.add(root, player);

        // 30 steps = 0.5 s = halfway.
        for (int i = 0; i < 30; i++) {
            AnimationSystem::update(reg, step);
        }
        ok &= nearly(reg.animations.get(root).time, 0.5f);
        ok &= nearly(xOf(reg, arm), 5.0f);                                  // pose applied
        ok &= nearly(reg.transforms.get(arm).transform.scale.x, 1.5f);
        ok &= nearly(reg.transforms.get(arm).transform.rotation.w,          // untouched
                     glm::angleAxis(1.0f, glm::vec3(0.0f, 1.0f, 0.0f)).w);
        ok &= nearly(xOf(reg, root), 0.0f); // only the named target moved

        // An unknown clip name is inert rather than fatal (it may not be imported
        // yet) — time must not advance against data that isn't there.
        reg.animations.get(root).clip = "Missing";
        const float frozen = reg.animations.get(root).time;
        AnimationSystem::update(reg, step);
        ok &= reg.animations.get(root).time == frozen;
    }

    { // looping wraps modularly, so any speed behaves — including one step that
      // overshoots the clip several times over, which `time -= duration` would fail
        AnimationClipRegistry::clear();
        AnimationClipRegistry::registerClip("Slide", makeSlideClip());

        Registry reg;
        const Entity e = reg.createEntity();
        reg.names.add(e, { "Arm" });
        reg.transforms.add(e, {});
        AnimationPlayerComponent player;
        player.clip = "Slide";
        player.speed = 250.0f; // 250 loops per second: >4 loops in a single step
        reg.animations.add(e, player);

        AnimationSystem::update(reg, step);
        const float wrapped = reg.animations.get(e).time;
        ok &= wrapped >= 0.0f && wrapped < 1.0f; // still in range after overshoot

        // A rewind (negative speed) wraps back into range rather than going negative.
        reg.animations.get(e).time = 0.1f;
        reg.animations.get(e).speed = -60.0f; // -1.0 s per step
        AnimationSystem::update(reg, step);
        const float rewound = reg.animations.get(e).time;
        ok &= rewound >= 0.0f && rewound < 1.0f;
    }

    { // a one-shot clamps at the end and stops *authoritatively*, so gameplay reads
      // "finished" from a component instead of inferring it from a derived pose
        AnimationClipRegistry::clear();
        AnimationClipRegistry::registerClip("Slide", makeSlideClip());

        Registry reg;
        const Entity e = reg.createEntity();
        reg.names.add(e, { "Arm" });
        reg.transforms.add(e, {});
        AnimationPlayerComponent player;
        player.clip = "Slide";
        player.loop = false;
        player.time = 0.98f;
        reg.animations.add(e, player);

        for (int i = 0; i < 10; i++) {
            AnimationSystem::update(reg, step);
        }
        ok &= !reg.animations.get(e).playing;
        ok &= nearly(reg.animations.get(e).time, 1.0f); // clamped, not wrapped
        ok &= nearly(xOf(reg, e), 10.0f);               // holds the final pose
    }

    { // determinism: same clip + same time => same pose, which is the entire basis
      // for time travel restoring animation for free
        AnimationClipRegistry::clear();
        AnimationClipRegistry::registerClip("Slide", makeSlideClip());

        const auto runFor = [&](int steps) {
            Registry reg;
            const Entity e = reg.createEntity();
            reg.names.add(e, { "Arm" });
            reg.transforms.add(e, {});
            AnimationPlayerComponent player;
            player.clip = "Slide";
            reg.animations.add(e, player);
            for (int i = 0; i < steps; i++) {
                AnimationSystem::update(reg, step);
            }
            return xOf(reg, e);
        };
        ok &= runFor(37) == runFor(37); // bit-identical, not merely close
    }

    { // playback state survives an in-place snapshot restore with the id preserved —
      // RULES.md Rule 21's worked example (`Animator { float currentTime; }` →
      // restore → animation jumps) turned into a passing assertion
        AnimationClipRegistry::clear();
        AnimationClipRegistry::registerClip("Slide", makeSlideClip());

        Registry reg;
        std::vector<Light> lights;
        const Entity e = reg.createEntity();
        reg.names.add(e, { "Arm" });
        reg.transforms.add(e, {});
        reg.hierarchy.add(e, {});
        AnimationPlayerComponent player;
        player.clip = "Slide";
        player.time = 0.25f;
        player.speed = 1.5f;
        player.loop = false;
        reg.animations.add(e, player);
        // Also carries a text input, so the entity emits the optional component
        // *preceding* animation in the serializer's comma-chain — the one branch
        // adding a trailing component can silently break (a missing comma, or a
        // stale "last one wins" tail). Cheap here; a parse error in the field.
        reg.textInputs.add(e, { "field", "abc", 2 });

        const std::string frame = SceneSerializer::saveToString(reg, lights);
        ok &= !frame.empty();

        // Simulate the sim running on past the captured frame.
        for (int i = 0; i < 20; i++) {
            AnimationSystem::update(reg, step);
        }
        ok &= reg.animations.get(e).time > 0.25f;

        // Scrub back — patch in place.
        ok &= SceneSerializer::patchFromString(reg, lights, frame);
        ok &= reg.animations.has(e); // same id preserved
        const auto& restored = reg.animations.get(e);
        ok &= nearly(restored.time, 0.25f) && nearly(restored.speed, 1.5f);
        ok &= restored.clip == "Slide" && restored.playing && !restored.loop;
        ok &= reg.textInputs.get(e).buffer == "abc"; // the neighbour survived too

        // And the pose re-derives from the restored time: Pose = f(clip, time).
        AnimationSystem::update(reg, 0.0f); // advance by nothing, just re-sample
        ok &= nearly(xOf(reg, e), 2.5f);
    }

    { // the Animation system honors its declared access: reads Animation/Name/
      // Hierarchy, writes Animation + Transform, touches nothing else
        AnimationClipRegistry::clear();
        AnimationClipRegistry::registerClip("Slide", makeSlideClip());

        Registry reg;
        const Entity e = reg.createEntity();
        reg.names.add(e, { "Arm" });
        reg.transforms.add(e, {});
        AnimationPlayerComponent player;
        player.clip = "Slide";
        reg.animations.add(e, player);

        ComponentAccessTracker tracker;
        {
            ComponentAccess::Scope scope(&tracker);
            AnimationSystem::update(reg, step);
        }
        const ComponentMask declaredReads =
            maskOf(ComponentType::Animation, ComponentType::Name, ComponentType::Hierarchy);
        const ComponentMask declaredWrites =
            maskOf(ComponentType::Animation, ComponentType::Transform);
        ok &= (tracker.touched() & ~(declaredReads | declaredWrites)) == 0;
        ok &= (tracker.mutated() & ~declaredWrites) == 0;
    }

    AnimationClipRegistry::clear(); // leave no clips behind for later tests
    return ok;
}

// --- Animation import: glTF animations become engine clips (Phase 17B) ------
// Reads a hand-written fixture (assets/models/AnimatedSpinner.gltf) with two
// animations on one node: "Spin" (LINEAR rotation + translation) and "Jump" (STEP
// translation). Parsing needs no GPU — tinygltf is parse-only — so this runs
// headless like everything else. Requires the working directory to be the project
// root, same as the editor.
inline bool testAnimationImport() {
    const std::string path = "assets/models/AnimatedSpinner.gltf";
    GltfModelData model;
    GltfLoader::loadModel(path, model); // throws on a missing/broken fixture -> FAIL

    bool ok = model.animations.size() == 2;
    if (!ok) {
        return false;
    }

    const auto nearly = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };

    const AnimationClip& spin = model.animations[0];
    ok &= spin.name == "Spin";
    ok &= nearly(spin.duration, 1.0f);          // derived from the input accessor
    ok &= spin.tracks.size() == 1;              // two channels on one node -> ONE track
    if (!ok) {
        return false;
    }

    // Node *index* 0 resolved to the node *name*: nothing downstream sees glTF's
    // numbering, which is the whole point of converting at the boundary.
    const TransformTrack& track = spin.tracks[0];
    ok &= track.target == "Spinner";
    ok &= track.rotation.times.size() == 2 && track.translation.times.size() == 2;
    ok &= track.scale.empty(); // not animated -> absent, not defaulted
    ok &= track.rotation.interpolation == Interpolation::Linear;

    // glTF quats are [x,y,z,w]; glm is (w,x,y,z). Getting that swap wrong is silent
    // and ruinous, so assert the decoded values rather than merely the count.
    ok &= nearly(track.rotation.values[0].w, 1.0f);  // identity
    ok &= nearly(track.rotation.values[1].y, 1.0f);  // 180 deg about Y
    ok &= nearly(track.rotation.values[1].w, 0.0f);
    ok &= nearly(track.translation.values[1].x, 10.0f);

    // STEP survives the round trip as STEP.
    const AnimationClip& jump = model.animations[1];
    ok &= jump.name == "Jump" && jump.tracks.size() == 1;
    ok &= jump.tracks[0].translation.interpolation == Interpolation::Step;

    // End to end: import into ECS, then play the clip through the real system.
    AnimationClipRegistry::clear();
    Registry reg;
    const Entity root = ModelImporter::importGltf(reg, path);
    ok &= root != INVALID_ENTITY;
    if (!ok) {
        return false;
    }

    ok &= AnimationClipRegistry::has(ModelImporter::animationClipKey(path, "Spin"));
    ok &= AnimationClipRegistry::has(ModelImporter::animationClipKey(path, "Jump"));
    ok &= reg.animations.has(root);
    // Stopped on import: the importer registers clips, it doesn't decide gameplay.
    ok &= !reg.animations.get(root).playing;
    ok &= reg.animations.get(root).clip == ModelImporter::animationClipKey(path, "Spin");

    // Drive it: 30 steps at 60 Hz = 0.5 s = halfway through the 1 s clip.
    reg.animations.get(root).playing = true;
    for (int i = 0; i < 30; i++) {
        AnimationSystem::update(reg, 1.0f / 60.0f);
    }
    const Transform& posed = reg.transforms.get(root).transform;
    ok &= nearly(posed.position.x, 5.0f);                 // lerp of 0 -> 10
    ok &= nearly(posed.rotation.w, 0.70710678f) &&        // slerp identity -> 180 deg
          nearly(posed.rotation.y, 0.70710678f);          // = 90 deg about Y

    AnimationClipRegistry::clear();
    return ok;
}

// --- Skinning: joint matrices are derived from ECS transforms (Phase 17C) ---
// The pose lives in ordinary entity transforms (posed by AnimationSystem, or by
// hand here); joint matrices are recomputed from them, never stored. Pure math over
// the registry, so no GPU is involved — which is the point.
inline bool testSkinning() {
    bool ok = true;
    const auto nearly = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };

    // A two-joint skeleton under a mesh's sibling: Root at origin, Tip 2 units up.
    // Inverse binds are the inverse of each joint's world transform at bind time.
    Skin skin;
    skin.name = "Armature";
    skin.joints = { "Root", "Tip" };
    skin.inverseBindMatrices = {
        glm::mat4(1.0f),
        glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f)))
    };

    Registry reg;
    const Entity character = reg.createEntity();
    reg.names.add(character, { "Character" });
    reg.transforms.add(character, {});
    reg.hierarchy.add(character, {});

    const Entity bar = reg.createEntity(); // the skinned mesh, a *sibling* of the skeleton
    reg.names.add(bar, { "Bar" });
    reg.transforms.add(bar, {});
    reg.hierarchy.add(bar, {});
    reg.setParent(bar, character);

    const Entity root = reg.createEntity();
    reg.names.add(root, { "Root" });
    reg.transforms.add(root, {});
    reg.hierarchy.add(root, {});
    reg.setParent(root, character);

    const Entity tip = reg.createEntity();
    reg.names.add(tip, { "Tip" });
    Transform tipTransform;
    tipTransform.position = glm::vec3(0.0f, 2.0f, 0.0f);
    reg.transforms.add(tip, { tipTransform });
    reg.hierarchy.add(tip, {});
    reg.setParent(tip, root);

    { // at bind pose, every joint matrix is identity — the definitive check that
      // the inverse-bind convention is the right way round
        std::vector<glm::mat4> matrices;
        ok &= Skinning::computeJointMatrices(reg, bar, skin, matrices);
        ok &= matrices.size() == 2;
        if (!ok) {
            return false;
        }
        for (const glm::mat4& matrix : matrices) {
            ok &= nearly(matrix[3][0], 0.0f) && nearly(matrix[3][1], 0.0f) && nearly(matrix[3][2], 0.0f);
            ok &= nearly(matrix[0][0], 1.0f) && nearly(matrix[1][1], 1.0f) && nearly(matrix[2][2], 1.0f);
        }
    }

    { // move a joint away from bind: its matrix picks up exactly that delta, and
      // joints are resolved through the *root ancestor*, not down from the mesh
      // (skeleton and mesh are siblings — searching from the mesh would find nothing)
        reg.transforms.get(tip).transform.position = glm::vec3(0.0f, 5.0f, 0.0f);
        std::vector<glm::mat4> matrices;
        ok &= Skinning::computeJointMatrices(reg, bar, skin, matrices);
        ok &= nearly(matrices[0][3][1], 0.0f); // Root unmoved
        ok &= nearly(matrices[1][3][1], 3.0f); // Tip moved 5 - 2 = 3 up
    }

    { // ROTATE a joint, which is the only way to pin the multiplication *order*.
      // Every translation-only case above passes just as happily with the operands
      // reversed, because translation matrices commute — so they cannot tell
      // `world * inverseBind` from `inverseBind * world`. Rotation does not commute
      // with translation, and this case is what actually holds the convention.
        reg.transforms.get(tip).transform.position = glm::vec3(0.0f, 2.0f, 0.0f); // bind
        reg.transforms.get(root).transform.rotation =
            glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

        std::vector<glm::mat4> matrices;
        ok &= Skinning::computeJointMatrices(reg, bar, skin, matrices);
        // world(Tip) = rotZ(90) * translate(0,2,0); right-multiplying the inverse
        // bind translate(0,-2,0) cancels the offset, leaving a pure rotation about
        // the joint — zero translation. The reversed product leaves (-2,-2,0).
        ok &= nearly(matrices[1][3][0], 0.0f) && nearly(matrices[1][3][1], 0.0f);
        // And it is genuinely a 90 deg rotation, not an identity that happens to
        // have no translation: rotZ(90) maps the X axis onto +Y.
        ok &= nearly(matrices[1][0][1], 1.0f);

        reg.transforms.get(root).transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    { // the skinned entity's own transform is cancelled out: glTF says a skinned
      // mesh's node transform must be ignored, and cancelling it here is what lets
      // the renderer keep applying its ordinary per-entity model matrix
        reg.transforms.get(tip).transform.position = glm::vec3(0.0f, 2.0f, 0.0f); // back to bind
        reg.transforms.get(bar).transform.position = glm::vec3(100.0f, 0.0f, 0.0f);
        std::vector<glm::mat4> matrices;
        ok &= Skinning::computeJointMatrices(reg, bar, skin, matrices);
        ok &= nearly(matrices[0][3][0], -100.0f); // relative to the mesh, not the world
        reg.transforms.get(bar).transform.position = glm::vec3(0.0f);
    }

    { // a joint that resolves to nothing becomes identity, NOT a dropped entry:
      // `out` must stay parallel to the skin's joint order because JOINTS_0 indexes
      // into it, so a hole would silently re-map every later joint
        Skin missing = skin;
        missing.joints = { "Root", "NoSuchBone" };
        std::vector<glm::mat4> matrices;
        ok &= Skinning::computeJointMatrices(reg, bar, missing, matrices);
        ok &= matrices.size() == 2;
        ok &= nearly(matrices[1][0][0], 1.0f) && nearly(matrices[1][3][1], 0.0f);

        // Nothing resolvable at all reports failure, so a caller can draw unskinned
        // rather than collapse the mesh onto the origin.
        Skin unbound = skin;
        unbound.joints = { "Ghost", "Phantom" };
        std::vector<glm::mat4> none;
        ok &= !Skinning::computeJointMatrices(reg, bar, unbound, none) && none.empty();

        // A malformed skin (joints and inverse binds out of step) is rejected rather
        // than read past the end of the shorter array.
        Skin malformed;
        malformed.joints = { "Root", "Tip" };
        malformed.inverseBindMatrices = { glm::mat4(1.0f) };
        ok &= !malformed.valid();
        ok &= !Skinning::computeJointMatrices(reg, bar, malformed, none);
    }

    { // animation drives skinning end to end: AnimationSystem poses the joint
      // entity, and the joint matrix follows — with no coupling between them beyond
      // the transform. Skinning = f(mesh, skeleton pose).
        AnimationClipRegistry::clear();
        TransformTrack track;
        track.target = "Tip";
        track.translation.times = { 0.0f, 1.0f };
        track.translation.values = { glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 6.0f, 0.0f) };
        AnimationClip clip;
        clip.name = "Raise";
        clip.tracks = { track };
        clip.duration = computeDuration(clip.tracks);
        AnimationClipRegistry::registerClip("Raise", clip);

        AnimationPlayerComponent player;
        player.clip = "Raise";
        reg.animations.add(character, player);

        for (int i = 0; i < 30; i++) { // 0.5 s -> Tip at y = 4
            AnimationSystem::update(reg, 1.0f / 60.0f);
        }
        ok &= nearly(reg.transforms.get(tip).transform.position.y, 4.0f);

        std::vector<glm::mat4> matrices;
        ok &= Skinning::computeJointMatrices(reg, bar, skin, matrices);
        ok &= nearly(matrices[1][3][1], 2.0f); // 4 - 2 (bind) = 2
        AnimationClipRegistry::clear();
    }

    return ok;
}

// --- Skin import: glTF skins become engine Skins (Phase 17C) ----------------
// Parse-only (GltfLoader::loadModel touches no ResourceManager), so this stays
// headless even though the fixture carries a mesh.
inline bool testSkinImport() {
    GltfModelData model;
    GltfLoader::loadModel("assets/models/SkinnedBar.gltf", model);

    bool ok = model.skins.size() == 1 && model.nodes.size() == 3;
    if (!ok) {
        return false;
    }

    const auto nearly = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    const Skin& skin = model.skins[0];

    ok &= skin.name == "Armature";
    ok &= skin.valid();
    // Joint node *indices* [1, 2] resolved to names, order preserved — JOINTS_0
    // indexes into this, so order is not cosmetic.
    ok &= skin.joints.size() == 2 && skin.joints[0] == "Root" && skin.joints[1] == "Tip";

    // Inverse bind matrices copy across column-major: Tip's bind inverse is
    // translate(0, -2, 0), so element [3][1] is -2. Getting the matrix layout
    // transposed is silent and ruinous, hence asserting the value.
    ok &= skin.inverseBindMatrices.size() == 2;
    ok &= nearly(skin.inverseBindMatrices[0][0][0], 1.0f);
    ok &= nearly(skin.inverseBindMatrices[0][3][1], 0.0f);  // Root: identity
    ok &= nearly(skin.inverseBindMatrices[1][3][1], -2.0f); // Tip: inverse of +2 up

    // The mesh node carries the skin; the joint nodes do not.
    ok &= model.nodes[0].name == "Bar" && model.nodes[0].skinIndex == 0;
    ok &= model.nodes[1].skinIndex == -1 && model.nodes[2].skinIndex == -1;
    if (!ok) {
        return false;
    }

    // Vertex influences (Phase 17C.2). Reading geometry needs no device — only
    // uploading it does — so JOINTS_0/WEIGHTS_0 parsing is testable headless.
    Mesh mesh;
    GltfLoader::loadGltfMesh("assets/models/SkinnedBar.gltf", 0, mesh);
    ok &= mesh.vertices.size() == 3 && mesh.indices.size() == 3;
    if (!ok) {
        return false;
    }

    // JOINTS_0 is *integer* data: read as floats it would be garbage rather than an
    // error, so assert the decoded indices. The fixture binds vertices 0/1 to joint
    // 0 (Root) and vertex 2 to joint 1 (Tip), each fully weighted.
    ok &= mesh.vertices[0].joints[0] == 0 && nearly(mesh.vertices[0].weights[0], 1.0f);
    ok &= mesh.vertices[2].joints[0] == 1 && nearly(mesh.vertices[2].weights[0], 1.0f);
    // Unused influences stay zero-weighted, so their joint index cannot matter.
    ok &= nearly(mesh.vertices[2].weights[1], 0.0f);
    ok &= nearly(mesh.vertices[2].pos[1], 2.0f); // and positions still land correctly

    // Static geometry has no influences and keeps the all-zero default, which is
    // what routes it to the unskinned pipeline.
    Mesh staticMesh;
    GltfLoader::loadGltfMesh("assets/models/Box.gltf", 0, staticMesh);
    ok &= !staticMesh.vertices.empty();
    if (ok) {
        ok &= nearly(staticMesh.vertices[0].weights[0], 0.0f);
        ok &= staticMesh.vertices[0].joints[0] == 0;
    }

    // A scene loaded from disk names clips/skins that nothing has registered — the
    // importer never ran in that session. Components hold *names* exactly so they
    // can be re-resolved; without that, a saved animated scene reloads with the
    // component intact and the animation silently dead.
    AnimationClipRegistry::clear();
    SkinRegistry::clear();
    const std::string barSkinKey = ModelImporter::skinKey("assets/models/SkinnedBar.gltf", "Armature");
    ok &= !SkinRegistry::has(barSkinKey);
    ModelImporter::ensureModelAssets(barSkinKey);
    ok &= SkinRegistry::has(barSkinKey);

    const std::string bend = ModelImporter::animationClipKey("assets/models/SkinnedBend.gltf", "Bend");
    ModelImporter::ensureModelAssets(bend);
    ok &= AnimationClipRegistry::has(bend);
    // Resolving a clip key must also bring in that model's skin: a character needs
    // both, and only one of them names the file.
    ok &= SkinRegistry::has(ModelImporter::skinKey("assets/models/SkinnedBend.gltf", "Armature"));

    // Junk keys and missing files are ignored rather than throwing mid-scene-load.
    ModelImporter::ensureModelAssets("no-separator");
    ModelImporter::ensureModelAssets("assets/models/DoesNotExist.gltf#Nope");
    AnimationClipRegistry::clear();
    SkinRegistry::clear();

    return ok;
}

// --- Animation graph: blend trees + state machines (Phase 17D) --------------
// The graph is an immutable asset; the active state, its phase, and the
// transition's progress are authoritative ECS state; weights and poses are derived.
inline bool testAnimationGraph() {
    bool ok = true;
    constexpr float step = 1.0f / 60.0f;
    const auto nearly = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };

    // Two clips over one target, deliberately different lengths so phase-vs-seconds
    // actually matters: Idle (2 s, x stays 0) and Run (1 s, x sweeps 0 -> 10).
    const auto makeClip = [](const char* name, float duration, float endX) {
        TransformTrack track;
        track.target = "Body";
        track.translation.times = { 0.0f, duration };
        track.translation.values = { glm::vec3(0.0f), glm::vec3(endX, 0.0f, 0.0f) };
        AnimationClip clip;
        clip.name = name;
        clip.tracks = { track };
        clip.duration = computeDuration(clip.tracks);
        return clip;
    };

    const auto setup = [&](Registry& reg) {
        AnimationClipRegistry::clear();
        AnimationGraphRegistry::clear();
        AnimationClipRegistry::registerClip("Idle", makeClip("Idle", 2.0f, 0.0f));
        AnimationClipRegistry::registerClip("Run", makeClip("Run", 1.0f, 10.0f));

        const Entity root = reg.createEntity();
        reg.names.add(root, { "Character" });
        reg.transforms.add(root, {});
        reg.hierarchy.add(root, {});
        const Entity body = reg.createEntity();
        reg.names.add(body, { "Body" });
        reg.transforms.add(body, {});
        reg.hierarchy.add(body, {});
        reg.setParent(body, root);
        return std::pair<Entity, Entity>{ root, body };
    };

    { // pose blending: the primitive every state machine rests on
        Pose a;
        a.entries.push_back({ "Body", TransformSample{} });
        a.entries[0].sample.hasTranslation = true;
        a.entries[0].sample.translation = glm::vec3(0.0f);
        Pose b;
        b.entries.push_back({ "Body", TransformSample{} });
        b.entries[0].sample.hasTranslation = true;
        b.entries[0].sample.translation = glm::vec3(10.0f, 0.0f, 0.0f);

        Pose out;
        blendPoses(a, b, 0.25f, out);
        ok &= out.entries.size() == 1 && nearly(out.entries[0].sample.translation.x, 2.5f);

        // Weight clamps: a transition that overshoots its duration by a fraction of a
        // step must not extrapolate past the target pose.
        blendPoses(a, b, 1.7f, out);
        ok &= nearly(out.entries[0].sample.translation.x, 10.0f);
        blendPoses(a, b, -0.5f, out);
        ok &= nearly(out.entries[0].sample.translation.x, 0.0f);

        // A target only one pose animates is taken as-is, not faded toward a guess.
        Pose c;
        c.entries.push_back({ "Tail", TransformSample{} });
        c.entries[0].sample.hasScale = true;
        c.entries[0].sample.scale = glm::vec3(3.0f);
        blendPoses(a, c, 0.5f, out);
        ok &= out.entries.size() == 2;
        ok &= out.find("Body") != nullptr && out.find("Tail") != nullptr;
        ok &= nearly(out.find("Tail")->sample.scale.x, 3.0f);   // unfaded
        ok &= nearly(out.find("Body")->sample.translation.x, 0.0f);
        // ...and an absent channel stays absent rather than becoming a zero.
        ok &= !out.find("Body")->sample.hasScale;
    }

    { // a single-clip state plays, and phase is normalized rather than seconds
        Registry reg;
        const auto [root, body] = setup(reg);

        AnimationGraph graph;
        graph.name = "Loco";
        graph.entryState = "Run";
        AnimationGraphState run;
        run.name = "Run";
        run.clip = "Run";
        graph.states = { run };
        AnimationGraphRegistry::registerGraph("Loco", graph);

        AnimationStateComponent machine;
        machine.graph = "Loco";
        reg.animationStates.add(root, machine);

        // Entry state is adopted automatically rather than requiring authoring.
        AnimationStateSystem::update(reg, 0.0f);
        ok &= reg.animationStates.get(root).currentState == "Run";

        // 30 steps = 0.5 s of a 1 s clip -> phase 0.5 -> x = 5.
        for (int i = 0; i < 30; i++) {
            AnimationStateSystem::update(reg, step);
        }
        ok &= nearly(reg.animationStates.get(root).statePhase, 0.5f);
        ok &= nearly(xOf(reg, body), 5.0f);
    }

    { // 1D blend tree: the parameter picks the mix, and clips of different lengths
      // stay phase-synced rather than sliding apart
        Registry reg;
        const auto [root, body] = setup(reg);

        AnimationGraph graph;
        graph.name = "Blend";
        graph.entryState = "Move";
        AnimationGraphState move;
        move.name = "Move";
        move.blendParameter = "speed";
        move.blendEntries = { { "Idle", 0.0f }, { "Run", 1.0f } };
        // Non-looping so the assertions below can pin phase at exactly 1.0 and read
        // each clip's end value. A looping state wraps 1.0 back to 0.0 — correct, but
        // it would make these checks about wrapping instead of about blending.
        move.loop = false;
        graph.states = { move };
        AnimationGraphRegistry::registerGraph("Blend", graph);

        AnimationStateComponent machine;
        machine.graph = "Blend";
        reg.animationStates.add(root, machine);
        AnimationParametersComponent parameters;
        parameters.values["speed"] = 0.5f; // half-way between Idle and Run
        reg.animationParameters.add(root, parameters);

        // Force a known phase so the assertion is about the blend, not the advance.
        AnimationStateSystem::update(reg, 0.0f);
        reg.animationStates.get(root).statePhase = 1.0f;
        AnimationStateSystem::update(reg, 0.0f);
        // Idle ends at x=0, Run ends at x=10, blended 50/50 -> 5.
        ok &= nearly(xOf(reg, body), 5.0f);

        // Parameter is gameplay's; the animator only reads it. Move it and the mix
        // follows with no animator-side state to update.
        reg.animationParameters.get(root).values["speed"] = 1.0f;
        reg.animationStates.get(root).statePhase = 1.0f;
        AnimationStateSystem::update(reg, 0.0f);
        ok &= nearly(xOf(reg, body), 10.0f); // all Run

        reg.animationParameters.get(root).values["speed"] = 0.0f;
        reg.animationStates.get(root).statePhase = 1.0f;
        AnimationStateSystem::update(reg, 0.0f);
        ok &= nearly(xOf(reg, body), 0.0f); // all Idle

        // Outside the thresholds the end entry wins outright — a speed past the
        // fastest clip plays the fastest clip, it does not extrapolate.
        reg.animationParameters.get(root).values["speed"] = 99.0f;
        reg.animationStates.get(root).statePhase = 1.0f;
        AnimationStateSystem::update(reg, 0.0f);
        ok &= nearly(xOf(reg, body), 10.0f);
    }

    { // a parameter transition cross-fades, and the progress is real ECS state
        Registry reg;
        const auto [root, body] = setup(reg);
        (void)body;

        AnimationGraph graph;
        graph.name = "Machine";
        graph.entryState = "Idle";
        AnimationGraphState idle;
        idle.name = "Idle";
        idle.clip = "Idle";
        AnimationGraphState run;
        run.name = "Run";
        run.clip = "Run";
        graph.states = { idle, run };
        AnimationTransition toRun;
        toRun.from = "Idle";
        toRun.to = "Run";
        toRun.parameter = "speed";
        toRun.condition = TransitionCondition::Greater;
        toRun.threshold = 0.5f;
        toRun.duration = 0.5f;
        graph.transitions = { toRun };
        AnimationGraphRegistry::registerGraph("Machine", graph);

        AnimationStateComponent machine;
        machine.graph = "Machine";
        reg.animationStates.add(root, machine);
        reg.animationParameters.add(root, {});

        AnimationStateSystem::update(reg, step);
        ok &= reg.animationStates.get(root).currentState == "Idle";
        ok &= !reg.animationStates.get(root).transitioning(); // condition false

        // Gameplay raises speed -> the transition starts.
        reg.animationParameters.get(root).values["speed"] = 1.0f;
        AnimationStateSystem::update(reg, step);
        ok &= reg.animationStates.get(root).transitioning();
        ok &= reg.animationStates.get(root).transitionTarget == "Run";
        ok &= reg.animationStates.get(root).currentState == "Idle"; // still blending out

        // Halfway through the 0.5 s cross-fade.
        for (int i = 0; i < 14; i++) {
            AnimationStateSystem::update(reg, step);
        }
        const auto& midway = reg.animationStates.get(root);
        ok &= midway.transitioning();
        ok &= midway.transitionElapsed > 0.2f && midway.transitionElapsed < 0.3f;

        // ...and it completes, adopting the target as the state.
        for (int i = 0; i < 20; i++) {
            AnimationStateSystem::update(reg, step);
        }
        ok &= reg.animationStates.get(root).currentState == "Run";
        ok &= !reg.animationStates.get(root).transitioning();
    }

    { // OnFinished: a one-shot reports its own end, so gameplay never has to infer
      // "the attack is over" from a derived pose
        Registry reg;
        const auto [root, body] = setup(reg);
        (void)body;

        AnimationGraph graph;
        graph.name = "OneShot";
        graph.entryState = "Attack";
        AnimationGraphState attack;
        attack.name = "Attack";
        attack.clip = "Run";
        attack.loop = false;
        AnimationGraphState idle;
        idle.name = "Idle";
        idle.clip = "Idle";
        graph.states = { attack, idle };
        AnimationTransition done;
        done.from = "Attack";
        done.to = "Idle";
        done.condition = TransitionCondition::OnFinished;
        done.duration = 0.0f; // snap
        graph.transitions = { done };
        AnimationGraphRegistry::registerGraph("OneShot", graph);

        AnimationStateComponent machine;
        machine.graph = "OneShot";
        reg.animationStates.add(root, machine);

        for (int i = 0; i < 30; i++) { // 0.5 s of a 1 s clip: not finished yet
            AnimationStateSystem::update(reg, step);
        }
        ok &= reg.animationStates.get(root).currentState == "Attack";

        for (int i = 0; i < 40; i++) { // past the end
            AnimationStateSystem::update(reg, step);
        }
        ok &= reg.animationStates.get(root).currentState == "Idle";
    }

    { // a mid-transition character survives a snapshot restore *mid-blend* — the
      // whole reason transition progress is authoritative rather than derived
        Registry reg;
        std::vector<Light> lights;
        const auto [root, body] = setup(reg);
        (void)body;

        AnimationGraph graph;
        graph.name = "Machine";
        graph.entryState = "Idle";
        AnimationGraphState idle;
        idle.name = "Idle";
        idle.clip = "Idle";
        AnimationGraphState run;
        run.name = "Run";
        run.clip = "Run";
        graph.states = { idle, run };
        AnimationGraphRegistry::registerGraph("Machine", graph);

        AnimationStateComponent machine;
        machine.graph = "Machine";
        machine.currentState = "Idle";
        machine.statePhase = 0.25f;
        machine.transitionTarget = "Run";
        machine.targetPhase = 0.1f;
        machine.transitionElapsed = 0.2f;
        machine.transitionDuration = 0.5f;
        reg.animationStates.add(root, machine);
        AnimationParametersComponent parameters;
        parameters.values["speed"] = 3.25f;
        parameters.values["health"] = 0.5f;
        reg.animationParameters.add(root, parameters);

        const std::string frame = SceneSerializer::saveToString(reg, lights);
        ok &= !frame.empty();

        // Let the sim run on past the captured frame.
        for (int i = 0; i < 40; i++) {
            AnimationStateSystem::update(reg, step);
        }
        ok &= !reg.animationStates.get(root).transitioning(); // it completed

        // Scrub back: the character must be mid-blend again, exactly.
        ok &= SceneSerializer::patchFromString(reg, lights, frame);
        const auto& restored = reg.animationStates.get(root);
        ok &= restored.currentState == "Idle" && restored.transitionTarget == "Run";
        ok &= nearly(restored.statePhase, 0.25f) && nearly(restored.targetPhase, 0.1f);
        ok &= nearly(restored.transitionElapsed, 0.2f) && nearly(restored.transitionDuration, 0.5f);
        ok &= nearly(reg.animationParameters.get(root).get("speed"), 3.25f);
        ok &= nearly(reg.animationParameters.get(root).get("health"), 0.5f);
    }

    { // the Animation system honors its declared access with state machines running
        Registry reg;
        const auto [root, body] = setup(reg);
        (void)body;

        AnimationGraph graph;
        graph.name = "Loco";
        graph.entryState = "Run";
        AnimationGraphState run;
        run.name = "Run";
        run.clip = "Run";
        graph.states = { run };
        AnimationGraphRegistry::registerGraph("Loco", graph);
        AnimationStateComponent machine;
        machine.graph = "Loco";
        reg.animationStates.add(root, machine);
        reg.animationParameters.add(root, {});

        ComponentAccessTracker tracker;
        {
            ComponentAccess::Scope scope(&tracker);
            AnimationStateSystem::update(reg, step);
        }
        const ComponentMask declaredReads =
            maskOf(ComponentType::Animation, ComponentType::Name, ComponentType::Hierarchy,
                   ComponentType::AnimationParameters);
        const ComponentMask declaredWrites =
            maskOf(ComponentType::Animation, ComponentType::Transform, ComponentType::AnimationState);
        ok &= (tracker.touched() & ~(declaredReads | declaredWrites)) == 0;
        ok &= (tracker.mutated() & ~declaredWrites) == 0;
    }

    AnimationClipRegistry::clear();
    AnimationGraphRegistry::clear();
    return ok;
}

// --- Serializer: save produces byte-exact scene text ------------------------
// A golden test, deliberately brittle: it pins the exact bytes for an entity that
// carries every optional component the headless build can hold, so the whole
// optional-field comma chain is covered in one place. Formatting is allowed to
// change — but only on purpose, by updating this expectation in the same commit.
//
// It is exact rather than a substring check because the serializer's writer and
// parser are separate code: a round-trip test proves only that the two *agree*,
// and would happily pass while the on-disk format silently drifted. Snapshots and
// time travel ride on this format.
//
// (Round-trip *load* needs a Vulkan device for mesh/texture assets, so this is
// save-only; audiosource is likewise omitted since it resolves a clip through
// ResourceManager.)
inline bool testSerializer() {
    Registry reg;
    std::vector<Light> lights;
    const Entity e = reg.createEntity();
    reg.names.add(e, { "Probe" });
    Transform t;
    t.position = glm::vec3(1.0f, 2.0f, 3.0f);
    reg.transforms.add(e, { t });
    reg.hierarchy.add(e, {});

    reg.scripts.add(e, { "Spin", false });

    RigidBodyComponent body;
    body.velocity = glm::vec3(4.0f, 5.0f, 6.0f);
    body.mass = 2.0f;
    body.restitution = 0.25f;
    body.friction = 0.5f;
    reg.rigidBodies.add(e, body);

    ColliderComponent collider;
    collider.type = ColliderType::Box;
    collider.halfExtents = glm::vec3(0.5f, 1.0f, 1.5f);
    collider.radius = 2.0f;
    reg.colliders.add(e, collider);

    reg.prefabInstances.add(e, { "p.prefab" });
    reg.audioListeners.add(e, { 0.75f });

    UIScreenComponent screen;
    screen.screenStack = { "HUD", "Inventory" };
    reg.uiScreens.add(e, screen);
    reg.focus.add(e, { "SlotA" });
    reg.textInputs.add(e, { "name", "abc", 2 });

    AnimationPlayerComponent player;
    player.clip = "Slide";
    player.time = 0.25f;
    player.speed = 1.5f;
    player.loop = false;
    reg.animations.add(e, player);

    reg.skinnedMeshes.add(e, { "hero.gltf#Armature" });

    const std::string expected =
        "{\n"
        "  \"version\": 2,\n"
        "  \"objects\": [\n"
        "    {\n"
        "      \"name\": \"Probe\",\n"
        "      \"parent\": -1,\n"
        "      \"transform\": {\n"
        "        \"pos\": [1, 2, 3],\n"
        "        \"rot\": [0, 0, 0, 1],\n"
        "        \"scale\": [1, 1, 1]\n"
        "      },\n"
        "      \"mesh\": \"\",\n"
        "      \"material\": {\n"
        "        \"albedo\": \"\",\n"
        "        \"metallic\": 0,\n"
        "        \"roughness\": 0.5,\n"
        "        \"ao\": 1\n"
        "      },\n"
        "      \"script\": \"Spin\",\n"
        "      \"rigidbody\": {\n"
        "        \"velocity\": [4, 5, 6],\n"
        "        \"mass\": 2,\n"
        "        \"restitution\": 0.25,\n"
        "        \"friction\": 0.5,\n"
        "        \"useGravity\": true,\n"
        "        \"isStatic\": false\n"
        "      },\n"
        "      \"collider\": {\n"
        "        \"type\": \"box\",\n"
        "        \"halfExtents\": [0.5, 1, 1.5],\n"
        "        \"radius\": 2\n"
        "      },\n"
        "      \"prefab\": \"p.prefab\",\n"
        "      \"audiolistener\": {\n"
        "        \"gain\": 0.75\n"
        "      },\n"
        "      \"uiscreen\": [\"HUD\", \"Inventory\"],\n"
        "      \"focus\": \"SlotA\",\n"
        "      \"textinput\": {\n"
        "        \"element\": \"name\",\n"
        "        \"buffer\": \"abc\",\n"
        "        \"caret\": 2\n"
        "      },\n"
        "      \"animation\": {\n"
        "        \"clip\": \"Slide\",\n"
        "        \"time\": 0.25,\n"
        "        \"speed\": 1.5,\n"
        "        \"playing\": true,\n"
        "        \"loop\": false\n"
        "      },\n"
        "      \"skinnedmesh\": \"hero.gltf#Armature\"\n"
        "    }\n"
        "  ],\n"
        "  \"lights\": [\n"
        "  ]\n"
        "}\n";

    const std::string text = SceneSerializer::saveToString(reg, lights);
    if (text != expected) {
        std::cout << "[selftest] serializer text mismatch:\n--- expected ---\n"
                  << expected << "--- actual ---\n" << text << "----------------\n";
        return false;
    }

    // The trailing optional must close *without* a comma. Dropping the last field
    // is the case a comma-chain gets wrong, so assert it separately rather than
    // trusting the golden text above to have covered it by luck.
    reg.skinnedMeshes.remove(e);
    const std::string withoutSkin = SceneSerializer::saveToString(reg, lights);
    return withoutSkin.find("\"skinnedmesh\"") == std::string::npos &&
           withoutSkin.find("      }\n    }\n  ],\n") != std::string::npos;
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
        { "Animation",        testAnimation },
        { "AnimationImport",  testAnimationImport },
        { "Skinning",         testSkinning },
        { "SkinImport",       testSkinImport },
        { "AnimationGraph",   testAnimationGraph },
        { "Serializer",       testSerializer },
        { "BehaviorRegistry", testBehaviorRegistry },
        { "RegistryGraph",    testRegistryGraph },
    };

    int passed = 0;
    const int total = static_cast<int>(sizeof(cases) / sizeof(cases[0]));
    for (const Case& test : cases) {
        const auto start = std::chrono::high_resolution_clock::now();

        // A throwing test is a failing test, not a dead suite. Without this, the
        // first test to throw (e.g. a component lookup that ECS no longer has)
        // aborts the whole run: every later test goes unreported and the table you
        // read to diagnose the problem is the thing you lose. Report it as a FAIL
        // with its message and carry on.
        bool ok = false;
        std::string thrown;
        try {
            ok = test.fn();
        } catch (const std::exception& error) {
            thrown = error.what();
        } catch (...) {
            thrown = "non-std exception";
        }

        const auto end = std::chrono::high_resolution_clock::now();
        const double milliseconds = std::chrono::duration<double, std::milli>(end - start).count();

        std::string label = test.name;
        while (label.size() < 18) {
            label += '.';
        }
        std::cout << "[selftest] " << label << ' ' << (ok ? "PASS" : "FAIL")
                  << " (" << std::fixed << std::setprecision(2) << milliseconds << " ms)";
        if (!thrown.empty()) {
            std::cout << "  threw: " << thrown;
        }
        std::cout << "\n";
        passed += ok ? 1 : 0;
    }
    std::cout << "[selftest] ResourceManager.. SKIPPED (needs Vulkan device)\n";
    std::cout << "[selftest] " << (passed == total ? "ALL PASS" : "FAILURES PRESENT") << "\n";
    return { passed, total };
}

} // namespace SelfTests
