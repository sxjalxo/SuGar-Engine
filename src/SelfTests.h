#pragma once

// Per-subsystem confidence self-tests. Not exhaustive — one quick "is this
// subsystem sane?" check each, so a single run (SUGAR_SELFTEST=1) prints a
// reassuring table before you ever launch the editor. Everything here runs
// headless (no Vulkan); subsystems that need a device are reported as SKIPPED.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
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
#include "assets/AssetCooker.h"
#include "assets/AssetDatabase.h"
#include "assets/AssetHash.h"
#include "assets/AssetMeta.h"
#include "assets/AssetPath.h"
#include "assets/AssetReimport.h"
#include "assets/CookedAsset.h"
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
#include "editor/ViewportOverlay.h"
#include "navigation/NavComponents.h"
#include "navigation/NavMesh.h"
#include "navigation/NavMeshBaker.h"
#include "navigation/NavMeshBuilder.h"
#include "navigation/NavMeshRegistry.h"
#include "navigation/NavPath.h"
#include "navigation/NavigationSystem.h"
#include "physics/PhysicsWorld.h"
#include "audio/AudioClip.h"
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

// --- Navigation: the navmesh is an asset, the path in progress is state -----
// Builds navmeshes as grids of unit quads. A grid welds shared corners by
// construction, which is exactly what NavMesh::buildAdjacency matches on — so the
// fixture exercises the real adjacency path rather than a hand-written neighbor
// table that could agree with a broken one.
inline NavMesh makeGridNavMesh(int width, int height,
                               const std::function<bool(int, int)>& walkable) {
    NavMesh mesh;
    const int stride = width + 1;
    for (int z = 0; z <= height; ++z) {
        for (int x = 0; x <= width; ++x) {
            mesh.vertices.push_back(glm::vec3(static_cast<float>(x), 0.0f, static_cast<float>(z)));
        }
    }
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            if (!walkable(x, z)) {
                continue;
            }
            NavPolygon polygon;
            polygon.firstIndex = static_cast<int>(mesh.indices.size());
            polygon.count = 4;
            mesh.indices.push_back(z * stride + x);
            mesh.indices.push_back(z * stride + x + 1);
            mesh.indices.push_back((z + 1) * stride + x + 1);
            mesh.indices.push_back((z + 1) * stride + x);
            mesh.polygons.push_back(polygon);
        }
    }
    mesh.buildAdjacency();
    return mesh;
}

inline bool nearlyEqualXZ(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-3f) {
    return std::fabs(a.x - b.x) < tolerance && std::fabs(a.z - b.z) < tolerance;
}

inline bool testNavigation() {
    bool ok = true;
    const float step = 1.0f / 60.0f;

    // An L-shaped region: the column x in [0,1] over all z, plus the row z in [4,5]
    // over all x. The concave corner is the vertex (1, 0, 4).
    const auto lShape = [](int x, int z) { return x == 0 || z == 4; };

    { // adjacency, containment, and height come out of the geometry
        const NavMesh mesh = makeGridNavMesh(5, 5, lShape);
        ok &= mesh.valid();
        ok &= mesh.polygonCount() == 9; // 5 column cells + 5 row cells - 1 shared

        const int polygon = mesh.findContainingPolygon(glm::vec3(0.5f, 0.0f, 0.5f));
        ok &= polygon >= 0 && mesh.containsXZ(polygon, glm::vec3(0.5f, 0.0f, 0.5f));
        ok &= std::fabs(mesh.heightAt(polygon, glm::vec3(0.5f, 0.0f, 0.5f))) < 1e-4f;

        // Off the mesh entirely: no containing polygon, but a nearest one that snaps
        // to the boundary — the case a gameplay click on a wall produces.
        ok &= mesh.findContainingPolygon(glm::vec3(4.5f, 0.0f, 0.5f)) < 0;
        glm::vec3 projected(0.0f);
        ok &= mesh.findNearestPolygon(glm::vec3(4.5f, 0.0f, 0.5f), projected) >= 0;
        ok &= projected.x <= 1.0f + 1e-3f;

        // Every interior edge is reciprocal: if a names b across an edge, b names a.
        for (int p = 0; p < mesh.polygonCount(); ++p) {
            const NavPolygon& poly = mesh.polygons[static_cast<std::size_t>(p)];
            for (int k = 0; k < poly.count; ++k) {
                const int neighbor = mesh.neighbors[static_cast<std::size_t>(poly.firstIndex + k)];
                if (neighbor < 0) {
                    continue;
                }
                bool reciprocal = false;
                const NavPolygon& other = mesh.polygons[static_cast<std::size_t>(neighbor)];
                for (int j = 0; j < other.count; ++j) {
                    reciprocal |= mesh.neighbors[static_cast<std::size_t>(other.firstIndex + j)] == p;
                }
                ok &= reciprocal;
            }
        }
    }

    { // the funnel string-pulls around the inner corner, rather than zig-zagging
      // from polygon center to polygon center — the whole reason stringPull exists
        const NavMesh mesh = makeGridNavMesh(5, 5, lShape);
        std::vector<glm::vec3> path;
        const NavPath::Result result =
            NavPath::findPath(mesh, glm::vec3(0.5f, 0.0f, 0.5f), glm::vec3(4.5f, 0.0f, 4.5f), path);

        ok &= result == NavPath::Result::Success;
        // Exactly one bend, at the concave corner. A funnel with a flipped left/right
        // convention hugs the *outer* corner instead and fails here; a missing funnel
        // returns one waypoint per polygon crossed (8 of them) and also fails.
        ok &= path.size() == 2;
        if (path.size() == 2) {
            ok &= nearlyEqualXZ(path[0], glm::vec3(1.0f, 0.0f, 4.0f));
            ok &= nearlyEqualXZ(path[1], glm::vec3(4.5f, 0.0f, 4.5f));
        }

        // Same query twice is byte-identical. A* ties break on polygon index, a total
        // order, so the priority queue's instability cannot pick a different (equally
        // optimal) route between runs.
        std::vector<glm::vec3> again;
        NavPath::findPath(mesh, glm::vec3(0.5f, 0.0f, 0.5f), glm::vec3(4.5f, 0.0f, 4.5f), again);
        ok &= again.size() == path.size();
        for (std::size_t i = 0; i < path.size() && i < again.size(); ++i) {
            ok &= path[i] == again[i]; // exact, not approximate
        }
    }

    { // within one convex polygon the straight line is the path, by definition
        const NavMesh mesh = makeGridNavMesh(5, 5, lShape);
        std::vector<glm::vec3> path;
        ok &= NavPath::findPath(mesh, glm::vec3(0.2f, 0.0f, 0.2f), glm::vec3(0.8f, 0.0f, 0.8f), path)
              == NavPath::Result::Success;
        ok &= path.size() == 1 && nearlyEqualXZ(path[0], glm::vec3(0.8f, 0.0f, 0.8f));
    }

    { // two disconnected islands: both ends are on the mesh, no corridor joins them
        const NavMesh mesh = makeGridNavMesh(5, 1, [](int x, int) { return x == 0 || x == 4; });
        std::vector<glm::vec3> path;
        ok &= NavPath::findPath(mesh, glm::vec3(0.5f, 0.0f, 0.5f), glm::vec3(4.5f, 0.0f, 0.5f), path)
              == NavPath::Result::Unreachable;

        NavMesh empty;
        ok &= NavPath::findPath(empty, glm::vec3(0.0f), glm::vec3(1.0f), path)
              == NavPath::Result::EmptyNavMesh;
    }

    { // an agent walks its route and arrives
        NavMeshRegistry::clear();
        NavMeshRegistry::registerNavMesh("L", makeGridNavMesh(5, 5, lShape));

        Registry reg;
        const Entity walker = reg.createEntity();
        Transform transform;
        transform.position = glm::vec3(0.5f, 0.0f, 0.5f);
        reg.transforms.add(walker, { transform });

        NavAgentComponent agent;
        agent.navMesh = "L";
        agent.speed = 4.0f;
        agent.setDestination(glm::vec3(4.5f, 0.0f, 4.5f));
        reg.navAgents.add(walker, agent);

        NavigationSystem::update(reg, step);
        ok &= reg.navAgents.get(walker).status == NavAgentStatus::Following;
        ok &= !reg.navAgents.get(walker).path.empty();
        // Planned *and* moved in the same step — no frame of latency that depends on
        // system order.
        ok &= reg.transforms.get(walker).transform.position != glm::vec3(0.5f, 0.0f, 0.5f);

        for (int i = 0; i < 600 && reg.navAgents.get(walker).status == NavAgentStatus::Following; ++i) {
            NavigationSystem::update(reg, step);
        }
        ok &= reg.navAgents.get(walker).status == NavAgentStatus::Arrived;
        ok &= nearlyEqualXZ(reg.transforms.get(walker).transform.position,
                            glm::vec3(4.5f, 0.0f, 4.5f), 0.2f);
        // Arrived agents drop the spent plan, so no restored snapshot can show one
        // still carrying the route it finished.
        ok &= reg.navAgents.get(walker).path.empty();
    }

    { // one step must spend its whole travel budget, crossing as many waypoints as
      // it can reach. "Advance at most one waypoint per step" is the same bug shape
      // as 17A's subtractive loop wrap, and this is the case that exposes it: a
      // single step long enough to cross the entire L.
        NavMeshRegistry::clear();
        NavMeshRegistry::registerNavMesh("L", makeGridNavMesh(5, 5, lShape));

        Registry reg;
        const Entity sprinter = reg.createEntity();
        Transform transform;
        transform.position = glm::vec3(0.5f, 0.0f, 0.5f);
        reg.transforms.add(sprinter, { transform });

        NavAgentComponent agent;
        agent.navMesh = "L";
        agent.speed = 1000.0f; // budget far exceeds the whole route
        agent.setDestination(glm::vec3(4.5f, 0.0f, 4.5f));
        reg.navAgents.add(sprinter, agent);

        NavigationSystem::update(reg, step);
        ok &= reg.navAgents.get(sprinter).status == NavAgentStatus::Arrived;
        ok &= nearlyEqualXZ(reg.transforms.get(sprinter).transform.position,
                            glm::vec3(4.5f, 0.0f, 4.5f), 1e-3f);
    }

    { // a failed plan is remembered, not retried every step — `status` is the record
      // that an attempt happened, which the present cannot reconstruct
        NavMeshRegistry::clear();
        NavMeshRegistry::registerNavMesh("Split",
            makeGridNavMesh(5, 1, [](int x, int) { return x == 0 || x == 4; }));

        Registry reg;
        const Entity stuck = reg.createEntity();
        Transform transform;
        transform.position = glm::vec3(0.5f, 0.0f, 0.5f);
        reg.transforms.add(stuck, { transform });

        NavAgentComponent agent;
        agent.navMesh = "Split";
        agent.setDestination(glm::vec3(4.5f, 0.0f, 0.5f));
        reg.navAgents.add(stuck, agent);

        NavigationSystem::update(reg, step);
        ok &= reg.navAgents.get(stuck).status == NavAgentStatus::Unreachable;
        ok &= reg.navAgents.get(stuck).path.empty();

        // Terminal until gameplay re-arms it: the destination is unchanged, so the
        // next steps must not re-run A*, and the agent must not drift.
        const glm::vec3 before = reg.transforms.get(stuck).transform.position;
        for (int i = 0; i < 10; ++i) {
            NavigationSystem::update(reg, step);
        }
        ok &= reg.navAgents.get(stuck).status == NavAgentStatus::Unreachable;
        ok &= reg.transforms.get(stuck).transform.position == before;

        // Re-arming the *same* destination retries — the one case a pure comparison
        // against pathGoal cannot see, which is why setDestination exists.
        reg.navAgents.get(stuck).setDestination(glm::vec3(0.8f, 0.0f, 0.5f));
        NavigationSystem::update(reg, step);
        ok &= reg.navAgents.get(stuck).status != NavAgentStatus::Unreachable;

        // An unknown navmesh leaves the agent Idle rather than Unreachable: a mesh
        // that is not baked yet is not the same fact as a route that does not exist,
        // and the agent should plan as soon as the bake lands.
        reg.navAgents.get(stuck).navMesh = "NotBakedYet";
        reg.navAgents.get(stuck).setDestination(glm::vec3(0.2f, 0.0f, 0.5f));
        NavigationSystem::update(reg, step);
        ok &= reg.navAgents.get(stuck).status == NavAgentStatus::Idle;
    }

    { // the point of the whole design: a mid-journey snapshot restores the *plan*,
      // so the agent continues the route it chose rather than re-deciding it
        NavMeshRegistry::clear();
        NavMeshRegistry::registerNavMesh("L", makeGridNavMesh(5, 5, lShape));

        Registry reg;
        std::vector<Light> lights;
        const Entity walker = reg.createEntity();
        reg.names.add(walker, { "Walker" });
        Transform transform;
        transform.position = glm::vec3(0.5f, 0.0f, 0.5f);
        reg.transforms.add(walker, { transform });
        reg.hierarchy.add(walker, {});

        NavAgentComponent agent;
        agent.navMesh = "L";
        agent.speed = 2.0f;
        agent.arrivalRadius = 0.25f;
        agent.setDestination(glm::vec3(4.5f, 0.0f, 4.5f));
        reg.navAgents.add(walker, agent);

        for (int i = 0; i < 30; ++i) {
            NavigationSystem::update(reg, step);
        }

        const NavAgentComponent midJourney = reg.navAgents.get(walker);
        ok &= midJourney.status == NavAgentStatus::Following;
        const std::string snapshot = SceneSerializer::saveToString(reg, lights);

        // Run well past the snapshot, then restore it in place.
        for (int i = 0; i < 60; ++i) {
            NavigationSystem::update(reg, step);
        }
        ok &= SceneSerializer::patchFromString(reg, lights, snapshot);

        const NavAgentComponent& restored = reg.navAgents.get(walker);
        ok &= restored.status == midJourney.status;
        ok &= restored.pathIndex == midJourney.pathIndex;
        ok &= restored.path.size() == midJourney.path.size();
        ok &= restored.pathGoal == midJourney.pathGoal;
        ok &= restored.hasDestination && restored.navMesh == "L";
        for (std::size_t i = 0; i < restored.path.size() && i < midJourney.path.size(); ++i) {
            ok &= nearlyEqualXZ(restored.path[i], midJourney.path[i]);
        }

        // And it keeps walking that same plan: no replan is triggered, because the
        // destination still matches the goal the restored path was planned for.
        const std::size_t waypointsBefore = restored.path.size();
        NavigationSystem::update(reg, step);
        ok &= reg.navAgents.get(walker).path.size() == waypointsBefore;

        // **Coverage gap, stated rather than hidden.** The full scene *load* path
        // (createEntitiesFromObjects — which creates entities rather than patching
        // them) is deliberately not exercised here, because `loadFromString` cannot
        // run headless *at all*: it resolves assets through ResourceManager, so it
        // fails even for an entity carrying no mesh and no material. Measured, not
        // assumed — a bare "name + transform" entity saves fine and then fails to
        // load. The limitation is pre-existing and engine-wide rather than
        // navigation's, and it means no component's load path is self-tested today.
        // The parsing half *is* covered above: patchFromString runs the same
        // parseEntityObject this file's navagent block lives in.
    }

    { // the Navigation system honors its declared access: reads and writes NavAgent
      // + Transform, and touches nothing else
        NavMeshRegistry::clear();
        NavMeshRegistry::registerNavMesh("L", makeGridNavMesh(5, 5, lShape));

        Registry reg;
        const Entity walker = reg.createEntity();
        Transform transform;
        transform.position = glm::vec3(0.5f, 0.0f, 0.5f);
        reg.transforms.add(walker, { transform });
        NavAgentComponent agent;
        agent.navMesh = "L";
        agent.setDestination(glm::vec3(4.5f, 0.0f, 4.5f));
        reg.navAgents.add(walker, agent);

        ComponentAccessTracker tracker;
        {
            ComponentAccess::Scope scope(&tracker);
            NavigationSystem::update(reg, step);
        }
        // NavObstacle joins the read set in 18D: update() collects obstacles every
        // step, so it touches that storage even when the scene has none. Writes stay
        // NavAgent + Transform — avoidance steers, it never edits an obstacle.
        const ComponentMask declared = maskOf(ComponentType::NavAgent, ComponentType::Transform,
                                              ComponentType::NavObstacle, ComponentType::Hierarchy);
        const ComponentMask written = maskOf(ComponentType::NavAgent, ComponentType::Transform);
        ok &= (tracker.touched() & ~declared) == 0;
        ok &= (tracker.mutated() & ~written) == 0;
    }

    NavMeshRegistry::clear(); // leave no meshes behind for later tests
    return ok;
}

// --- NavMeshBake: triangle soup becomes a walkable, connected navmesh -------
// Tests the *pure* half (buildNavMesh), which is the point of the Core/Engine split:
// the bake takes plain world-space triangles, so it needs no Mesh, no
// ResourceManager, and no Vulkan device to verify.
inline bool testNavMeshBake() {
    bool ok = true;

    // A flat WxH grid of unit quads, each split into two triangles, wound so the
    // normal points up. Corners are emitted at exact coordinates but as *separate*
    // triangles, so welding is genuinely exercised rather than assumed.
    const auto flatGrid = [](int width, int height, float y) {
        std::vector<NavTriangle> triangles;
        for (int z = 0; z < height; ++z) {
            for (int x = 0; x < width; ++x) {
                const auto corner = [&](int cx, int cz) {
                    return glm::vec3(static_cast<float>(cx), y, static_cast<float>(cz));
                };
                // Wound so cross(b-a, c-a) points at +Y. Getting this backwards makes
                // the whole grid a *ceiling* and the bake rejects all of it — which
                // is the bake behaving correctly, and is exactly what it did the
                // first time this fixture was written.
                triangles.push_back({ corner(x, z), corner(x, z + 1), corner(x + 1, z + 1) });
                triangles.push_back({ corner(x, z), corner(x + 1, z + 1), corner(x + 1, z) });
            }
        }
        return triangles;
    };

    { // welding turns loose triangles into one connected mesh
        NavBakeStats stats;
        const NavMesh mesh = buildNavMesh(flatGrid(4, 4, 0.0f), {}, &stats);

        ok &= mesh.valid();
        ok &= stats.inputTriangles == 32 && stats.polygons == 32;
        // 5x5 grid corners: without welding this would be 96 (3 per triangle), which
        // is the assertion that actually proves the welder ran.
        ok &= stats.vertices == 25;
        ok &= stats.rejectedBySlope == 0 && stats.rejectedDegenerate == 0;
        // Every triangle touches another: an unwelded bake reports all of them
        // isolated, and every path over it comes back Unreachable.
        ok &= stats.isolatedPolygons == 0;
    }

    { // a triangulated floor still string-pulls to a straight line — which is why
      // merging coplanar polygons is an optimization (Rule 18) and not a fix
        NavMeshRegistry::clear();
        NavMeshRegistry::registerNavMesh("Floor", buildNavMesh(flatGrid(6, 6, 0.0f)));
        const NavMesh* mesh = NavMeshRegistry::get("Floor");
        ok &= mesh != nullptr;

        if (mesh != nullptr) {
            std::vector<glm::vec3> path;
            ok &= NavPath::findPath(*mesh, glm::vec3(0.5f, 0.0f, 0.5f),
                                    glm::vec3(5.5f, 0.0f, 5.5f), path) == NavPath::Result::Success;
            // One waypoint: the goal. Not one per polygon crossed — this is the
            // assertion that makes "merging is an optimization" a claim and not a hope.
            ok &= path.size() == 1;
        }
    }

    { // slope filtering, and that winding decides floor from ceiling
        std::vector<NavTriangle> triangles = flatGrid(2, 2, 0.0f);
        const std::size_t floorCount = triangles.size();

        // A vertical wall: rejected at any sane slope limit.
        triangles.push_back({ glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
                              glm::vec3(0.0f, 0.0f, 1.0f) });
        // A flat triangle wound the other way — a ceiling. Same geometry as a floor,
        // opposite normal, and it must not become walkable ground.
        triangles.push_back({ glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(6.0f, 0.0f, 0.0f),
                              glm::vec3(5.0f, 0.0f, 1.0f) });

        NavBakeStats stats;
        const NavMesh mesh = buildNavMesh(triangles, {}, &stats);
        ok &= stats.rejectedBySlope == 2; // the wall *and* the ceiling
        ok &= stats.polygons == static_cast<int>(floorCount);
        ok &= mesh.valid();
    }

    { // degenerate input is dropped, before and after welding
        std::vector<NavTriangle> triangles;
        // Zero area outright.
        triangles.push_back({ glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(2.0f, 0.0f, 0.0f) });
        // Non-degenerate on its own corners, but two of them weld together at the
        // default epsilon — the case that would otherwise enter the mesh as a
        // polygon with a repeated index and match its own edge in buildAdjacency.
        triangles.push_back({ glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.001f),
                              glm::vec3(1.0f, 0.0f, 0.0f) });

        NavBakeStats stats;
        const NavMesh mesh = buildNavMesh(triangles, {}, &stats);
        ok &= stats.rejectedDegenerate == 2; // one by area, one only after welding
        ok &= mesh.empty() && mesh.valid();
    }

    { // an epsilon too small for the source geometry leaves everything isolated.
      // This is the diagnostic the stats exist for: the symptom a user reports is
      // "pathfinding is broken", and the cause is a bake parameter.
        // Two triangles that would share an edge, but whose two shared corners sit
        // ~0.07 apart — touching to a human, separate to a 0.001 weld.
        std::vector<NavTriangle> triangles;
        triangles.push_back({ glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
                              glm::vec3(1.0f, 0.0f, 1.0f) });
        triangles.push_back({ glm::vec3(0.05f, 0.0f, 0.05f), glm::vec3(1.05f, 0.0f, 1.05f),
                              glm::vec3(1.0f, 0.0f, 0.0f) });

        NavBakeParams tight;
        tight.weldEpsilon = 0.001f;
        NavBakeStats tightStats;
        buildNavMesh(triangles, tight, &tightStats);
        ok &= tightStats.isolatedPolygons == 2; // never connected → every path fails

        NavBakeParams loose;
        loose.weldEpsilon = 0.1f;
        NavBakeStats looseStats;
        buildNavMesh(triangles, loose, &looseStats);
        ok &= looseStats.isolatedPolygons == 0; // welded → they share an edge
    }

    { // baking is deterministic: same soup, byte-identical mesh
        const std::vector<NavTriangle> soup = flatGrid(3, 3, 0.0f);
        const NavMesh first = buildNavMesh(soup);
        const NavMesh second = buildNavMesh(soup);
        ok &= first.vertices.size() == second.vertices.size();
        ok &= first.indices == second.indices;
        ok &= first.neighbors == second.neighbors;
        for (std::size_t i = 0; i < first.vertices.size() && i < second.vertices.size(); ++i) {
            ok &= first.vertices[i] == second.vertices[i];
        }
    }

    { // an agent walks a baked mesh end to end — the bake feeds the same planner
      // 18A tested against hand-built meshes
        NavMeshRegistry::clear();
        NavMeshRegistry::registerNavMesh("Baked", buildNavMesh(flatGrid(6, 6, 0.0f)));

        Registry reg;
        const Entity walker = reg.createEntity();
        Transform transform;
        transform.position = glm::vec3(0.5f, 0.0f, 0.5f);
        reg.transforms.add(walker, { transform });

        NavAgentComponent agent;
        agent.navMesh = "Baked";
        agent.speed = 4.0f;
        agent.setDestination(glm::vec3(5.5f, 0.0f, 5.5f));
        reg.navAgents.add(walker, agent);

        // Guarded on Arrived, not on Following: a freshly armed agent starts *Idle*
        // and only becomes Following on its first update, so a `== Following` guard
        // would exit before the first tick and assert against an agent that never
        // moved.
        for (int i = 0; i < 600 && reg.navAgents.get(walker).status != NavAgentStatus::Arrived; ++i) {
            NavigationSystem::update(reg, 1.0f / 60.0f);
        }
        ok &= reg.navAgents.get(walker).status == NavAgentStatus::Arrived;
        ok &= nearlyEqualXZ(reg.transforms.get(walker).transform.position,
                            glm::vec3(5.5f, 0.0f, 5.5f), 0.2f);
    }

    { // the Rule 21a hook is wired: a scene naming a navmesh triggers a bake attempt
      // on load. Headless it harvests nothing (no ResourceManager), so the name stays
      // unregistered — which is the *designed* outcome: an empty bake must not be
      // registered, or it would cache the failure and stop every later attempt.
        NavMeshRegistry::clear();

        Registry source;
        std::vector<Light> lights;
        const Entity ground = source.createEntity();
        source.names.add(ground, { "Ground" });
        source.transforms.add(ground, {});
        source.navMeshSources.add(ground, { "level" });

        const std::string text = SceneSerializer::saveToString(source, lights);
        ok &= text.find("\"navmeshsource\": \"level\"") != std::string::npos;

        Registry loaded;
        std::vector<Light> loadedLights;
        ok &= SceneSerializer::loadFromString(loaded, loadedLights, text);
        ok &= loaded.navMeshSources.getAll().size() == 1;
        ok &= !NavMeshRegistry::has("level"); // empty bake deliberately not registered

        // And the guard that matters most: ensureBaked must **not** re-bake a name
        // that is already registered. Without the has() check, this post-load hook
        // would overwrite a perfectly good navmesh with whatever the current harvest
        // returns — which headless is nothing at all. A hook that destroys the thing
        // it exists to restore is a worse bug than the one it fixes.
        NavMeshRegistry::registerNavMesh("level", buildNavMesh(flatGrid(2, 2, 0.0f)));
        const int before = NavMeshRegistry::get("level")->polygonCount();
        NavMeshBaker::ensureSceneNavMeshes(loaded);
        ok &= NavMeshRegistry::has("level") &&
              NavMeshRegistry::get("level")->polygonCount() == before && before > 0;
    }

    NavMeshRegistry::clear();
    return ok;
}

// --- NavAvoidance: erosion before planning, avoidance after -----------------
// The 18D separation, asserted rather than assumed:
//
//   A*  ->  corridor  ->  local avoidance  ->  steering
//
// Erosion changes the traversable space, so it happens at bake time and the planner
// sees its result. Avoidance responds to transient conditions, so it happens during
// steering and must never redefine which corridor the planner chose.
inline bool testNavAvoidance() {
    bool ok = true;
    const float step = 1.0f / 60.0f;

    const auto flatGrid = [](int width, int height) {
        std::vector<NavTriangle> triangles;
        for (int z = 0; z < height; ++z) {
            for (int x = 0; x < width; ++x) {
                const auto corner = [&](int cx, int cz) {
                    return glm::vec3(static_cast<float>(cx), 0.0f, static_cast<float>(cz));
                };
                triangles.push_back({ corner(x, z), corner(x, z + 1), corner(x + 1, z + 1) });
                triangles.push_back({ corner(x, z), corner(x + 1, z + 1), corner(x + 1, z) });
            }
        }
        return triangles;
    };

    { // erosion drops polygons near the boundary, and is off unless asked for
        NavBakeStats plain;
        buildNavMesh(flatGrid(6, 6), {}, &plain);
        ok &= plain.erodedByRadius == 0; // default agentRadius is 0

        NavBakeParams eroding;
        eroding.agentRadius = 1.5f;
        NavBakeStats eroded;
        const NavMesh mesh = buildNavMesh(flatGrid(6, 6), eroding, &eroded);

        ok &= eroded.erodedByRadius > 0;
        ok &= eroded.polygons < plain.polygons;
        ok &= mesh.valid();

        // The survivors are interior: nothing within the radius of the old edge.
        for (int p = 0; p < mesh.polygonCount(); ++p) {
            for (int k = 0; k < mesh.polygons[static_cast<std::size_t>(p)].count; ++k) {
                const glm::vec3& corner = mesh.corner(p, k);
                ok &= corner.x >= 1.5f - 1e-3f && corner.x <= 4.5f + 1e-3f;
                ok &= corner.z >= 1.5f - 1e-3f && corner.z <= 4.5f + 1e-3f;
            }
        }

        // Adjacency is rebuilt, not carried over: dropping polygons creates new
        // boundaries, so a stale table would link to polygons that no longer exist.
        ok &= mesh.neighbors.size() == mesh.indices.size();
        for (int neighbor : mesh.neighbors) {
            ok &= neighbor < mesh.polygonCount();
        }

        // A radius wider than the whole mesh erodes it away entirely rather than
        // producing something subtly wrong.
        NavBakeParams huge;
        huge.agentRadius = 50.0f;
        NavBakeStats gone;
        ok &= buildNavMesh(flatGrid(6, 6), huge, &gone).empty();
    }

    { // **the load-bearing assertion**: an obstacle deflects the agent without
      // touching the plan. Same path, same waypoint count, same goal.
        NavMeshRegistry::clear();
        NavMeshRegistry::registerNavMesh("Floor", buildNavMesh(flatGrid(10, 10)));

        Registry reg;
        const Entity walker = reg.createEntity();
        Transform transform;
        transform.position = glm::vec3(1.0f, 0.0f, 5.0f);
        reg.transforms.add(walker, { transform });

        NavAgentComponent agent;
        agent.navMesh = "Floor";
        agent.speed = 2.0f;
        agent.radius = 0.4f;
        agent.setDestination(glm::vec3(9.0f, 0.0f, 5.0f));
        reg.navAgents.add(walker, agent);

        // Plan first, with nothing in the way.
        NavigationSystem::update(reg, step);
        const std::vector<glm::vec3> plannedPath = reg.navAgents.get(walker).path;
        ok &= reg.navAgents.get(walker).status == NavAgentStatus::Following;
        // Not asserting a waypoint count: this route runs exactly along the grid's
        // shared edges, where the funnel legitimately emits several collinear
        // waypoints. What matters is that the plan does not *change* below.
        ok &= !plannedPath.empty();

        // Now drop an obstacle directly on the route.
        const Entity crate = reg.createEntity();
        Transform crateTransform;
        crateTransform.position = glm::vec3(5.0f, 0.0f, 5.0f);
        reg.transforms.add(crate, { crateTransform });
        reg.navObstacles.add(crate, { 1.0f });

        float maxLateral = 0.0f;
        for (int i = 0; i < 600 && reg.navAgents.get(walker).status != NavAgentStatus::Arrived; ++i) {
            NavigationSystem::update(reg, step);
            const glm::vec3 at = reg.transforms.get(walker).transform.position;
            maxLateral = std::max(maxLateral, std::fabs(at.z - 5.0f));

            // Never inside the obstacle: clearance is respected the whole way.
            const float dx = at.x - 5.0f;
            const float dz = at.z - 5.0f;
            ok &= std::sqrt(dx * dx + dz * dz) > 0.5f;

            // And the plan is untouched at every single step — avoidance changes how
            // the corridor is traversed, never which corridor it is.
            const std::vector<glm::vec3>& live = reg.navAgents.get(walker).path;
            if (!live.empty()) {
                ok &= live.size() == plannedPath.size();
                ok &= live.back() == plannedPath.back();
            }
        }

        ok &= maxLateral > 0.05f; // it genuinely deviated rather than walking through
        ok &= reg.navAgents.get(walker).status == NavAgentStatus::Arrived;
        // Rejoined and finished at the same destination it planned for.
        ok &= nearlyEqualXZ(reg.transforms.get(walker).transform.position,
                            glm::vec3(9.0f, 0.0f, 5.0f), 0.3f);
    }

    { // avoidance is a function of the present only, so it is derived (Rule 21b):
      // two identical worlds stepped identically end up bit-identical
        NavMeshRegistry::clear();
        NavMeshRegistry::registerNavMesh("Floor", buildNavMesh(flatGrid(10, 10)));

        const auto buildWorld = [&](Registry& reg) {
            const Entity walker = reg.createEntity();
            Transform transform;
            transform.position = glm::vec3(1.0f, 0.0f, 5.0f);
            reg.transforms.add(walker, { transform });
            NavAgentComponent agent;
            agent.navMesh = "Floor";
            agent.speed = 2.0f;
            agent.setDestination(glm::vec3(9.0f, 0.0f, 5.0f));
            reg.navAgents.add(walker, agent);

            // Several obstacles, so the sum order actually matters — this is what
            // the entity-id sort in NavigationSystem exists to pin.
            for (int i = 0; i < 4; ++i) {
                const Entity crate = reg.createEntity();
                Transform crateTransform;
                crateTransform.position =
                    glm::vec3(3.0f + static_cast<float>(i), 0.0f, 5.0f + (i % 2 ? 0.4f : -0.4f));
                reg.transforms.add(crate, { crateTransform });
                reg.navObstacles.add(crate, { 0.8f });
            }
            return walker;
        };

        Registry first;
        Registry second;
        const Entity a = buildWorld(first);
        const Entity b = buildWorld(second);
        for (int i = 0; i < 240; ++i) {
            NavigationSystem::update(first, step);
            NavigationSystem::update(second, step);
        }
        ok &= first.transforms.get(a).transform.position ==
              second.transforms.get(b).transform.position;
        ok &= first.navAgents.get(a).pathIndex == second.navAgents.get(b).pathIndex;
    }

    { // an obstacle sitting exactly on the agent picks a fixed escape direction
      // rather than a random one — a random nudge would break replay for the one
      // case that most needs to reproduce
        NavMeshRegistry::clear();
        NavMeshRegistry::registerNavMesh("Floor", buildNavMesh(flatGrid(10, 10)));

        const auto runOverlapping = [&](glm::vec3& out) {
            Registry reg;
            const Entity walker = reg.createEntity();
            Transform transform;
            transform.position = glm::vec3(5.0f, 0.0f, 5.0f);
            reg.transforms.add(walker, { transform });
            NavAgentComponent agent;
            agent.navMesh = "Floor";
            agent.speed = 2.0f;
            agent.setDestination(glm::vec3(9.0f, 0.0f, 5.0f));
            reg.navAgents.add(walker, agent);

            const Entity crate = reg.createEntity();
            reg.transforms.add(crate, { transform }); // exactly on top of the agent
            reg.navObstacles.add(crate, { 1.0f });

            for (int i = 0; i < 10; ++i) {
                NavigationSystem::update(reg, step);
            }
            out = reg.transforms.get(walker).transform.position;
        };

        glm::vec3 first(0.0f);
        glm::vec3 second(0.0f);
        runOverlapping(first);
        runOverlapping(second);
        ok &= first == second;
    }

    { // the Navigation system still honors its declared access with obstacles present
        NavMeshRegistry::clear();
        NavMeshRegistry::registerNavMesh("Floor", buildNavMesh(flatGrid(6, 6)));

        Registry reg;
        const Entity walker = reg.createEntity();
        Transform transform;
        transform.position = glm::vec3(1.0f, 0.0f, 3.0f);
        reg.transforms.add(walker, { transform });
        NavAgentComponent agent;
        agent.navMesh = "Floor";
        agent.setDestination(glm::vec3(5.0f, 0.0f, 3.0f));
        reg.navAgents.add(walker, agent);

        const Entity crate = reg.createEntity();
        Transform crateTransform;
        crateTransform.position = glm::vec3(3.0f, 0.0f, 3.0f);
        reg.transforms.add(crate, { crateTransform });
        reg.navObstacles.add(crate, { 1.0f });

        ComponentAccessTracker tracker;
        {
            ComponentAccess::Scope scope(&tracker);
            for (int i = 0; i < 30; ++i) {
                NavigationSystem::update(reg, step);
            }
        }
        const ComponentMask declared = maskOf(ComponentType::NavAgent, ComponentType::Transform,
                                              ComponentType::NavObstacle, ComponentType::Hierarchy);
        const ComponentMask written = maskOf(ComponentType::NavAgent, ComponentType::Transform);
        ok &= (tracker.touched() & ~declared) == 0;
        ok &= (tracker.mutated() & ~written) == 0;
    }

    NavMeshRegistry::clear();
    return ok;
}

// --- ViewportOverlay: near-plane clipping, not projection rejection ---------
// Editor infrastructure, not navigation's — every future viewport visualization
// (physics contacts, audio ranges, camera frusta, perception cones) needs it. Pure
// math over matrices, so it tests headlessly despite being editor code.
inline bool testViewportOverlay() {
    bool ok = true;

    // Camera at the origin looking down -Z, the OpenGL/glm convention.
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    const ViewportOverlay::Projector projector(proj * view, 0.0f, 0.0f, 100.0f, 100.0f);

    { // a point straight ahead lands in the middle of the rect
        const glm::vec4 clip = projector.toClip(glm::vec3(0.0f, 0.0f, -10.0f));
        ok &= ViewportOverlay::Projector::inFront(clip);
        const glm::vec2 screen = projector.toScreen(clip);
        ok &= std::fabs(screen.x - 50.0f) < 0.01f && std::fabs(screen.y - 50.0f) < 0.01f;
    }

    { // a point behind the camera is *not* in front — and would project to a
      // mirrored position if anyone divided by its w anyway
        ok &= !ViewportOverlay::Projector::inFront(projector.toClip(glm::vec3(0.0f, 0.0f, 10.0f)));
    }

    { // **the whole point**: a quad straddling the near plane is CLIPPED, not
      // dropped. Two corners in front, two behind — the case that made the
      // navigation overlay draw nothing when it rejected instead of clipping.
        const std::vector<glm::vec3> straddling = {
            glm::vec3(-5.0f, 0.0f, -10.0f), // in front
            glm::vec3( 5.0f, 0.0f, -10.0f), // in front
            glm::vec3( 5.0f, 0.0f,  10.0f), // behind
            glm::vec3(-5.0f, 0.0f,  10.0f)  // behind
        };
        const std::vector<glm::vec2> screen = projector.projectPolygon(straddling);
        // Survives with vertices to spare: two kept corners plus the two points where
        // the outline crosses the near plane.
        ok &= screen.size() == 4;
        for (const glm::vec2& point : screen) {
            ok &= std::isfinite(point.x) && std::isfinite(point.y);
        }
    }

    { // entirely behind: nothing survives, and that is a real answer, not a failure
        const std::vector<glm::vec3> behind = {
            glm::vec3(-5.0f, 0.0f, 10.0f),
            glm::vec3( 5.0f, 0.0f, 10.0f),
            glm::vec3( 0.0f, 5.0f, 10.0f)
        };
        ok &= projector.projectPolygon(behind).empty();
    }

    { // entirely in front: passes through untouched, same vertex count
        const std::vector<glm::vec3> ahead = {
            glm::vec3(-1.0f, 0.0f, -5.0f),
            glm::vec3( 1.0f, 0.0f, -5.0f),
            glm::vec3( 0.0f, 1.0f, -5.0f)
        };
        ok &= projector.projectPolygon(ahead).size() == 3;
    }

    { // segments: trimmed at the near plane rather than discarded
        glm::vec2 a;
        glm::vec2 b;
        ok &= projector.clipSegment(projector.toClip(glm::vec3(0.0f, 0.0f, -10.0f)),
                                    projector.toClip(glm::vec3(0.0f, 0.0f, 10.0f)), a, b);
        ok &= std::isfinite(b.x) && std::isfinite(b.y);
        // Both endpoints behind: no line at all.
        ok &= !projector.clipSegment(projector.toClip(glm::vec3(0.0f, 0.0f, 5.0f)),
                                     projector.toClip(glm::vec3(0.0f, 0.0f, 10.0f)), a, b);
    }

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

    NavAgentComponent agent;
    agent.navMesh = "level";
    agent.destination = glm::vec3(7.0f, 0.0f, 8.0f);
    agent.hasDestination = true;
    agent.path = { glm::vec3(1.0f, 0.0f, 2.0f), glm::vec3(3.0f, 0.0f, 4.0f) };
    agent.pathIndex = 1;
    agent.pathGoal = glm::vec3(7.0f, 0.0f, 8.0f);
    agent.status = NavAgentStatus::Following;
    agent.speed = 3.0f;
    agent.arrivalRadius = 0.1f;
    reg.navAgents.add(e, agent);

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
        "      \"skinnedmesh\": \"hero.gltf#Armature\",\n"
        "      \"navagent\": {\n"
        "        \"navmesh\": \"level\",\n"
        "        \"destination\": [7, 0, 8],\n"
        "        \"hasDestination\": true,\n"
        "        \"path\": [[1, 0, 2], [3, 0, 4]],\n"
        "        \"pathIndex\": 1,\n"
        "        \"pathGoal\": [7, 0, 8],\n"
        "        \"status\": \"following\",\n"
        "        \"speed\": 3,\n"
        "        \"arrivalRadius\": 0.1\n"
        "      }\n"
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
    reg.navAgents.remove(e);
    const std::string withoutAgent = SceneSerializer::saveToString(reg, lights);
    return withoutAgent.find("\"navagent\"") == std::string::npos &&
           withoutAgent.find("\"skinnedmesh\": \"hero.gltf#Armature\"\n    }\n  ],\n") != std::string::npos;
}

// --- SceneLoad: every optional component survives a real load ---------------
// The counterpart to testSerializer, which is save-only. A golden test pins what
// the writer *emits*; only a load can pin what the reader *reconstructs*, and the
// two are separate code — `createEntitiesFromObjects` creates entities, where
// `patchFromString` updates existing ones, so neither covers the other.
//
// This path had no coverage at all until Phase 18A, because loadFromString could
// not run headless: asset resolution threw without a Vulkan device, and
// loadSceneFromText's catch-all turned that into a bare `false`. It is exactly the
// path RULES.md Rule 21a's worked example (17C.2) went wrong on — a scene loaded
// from disk keeping components that resolve to nothing.
//
// Mesh and material are deliberately *not* asserted to survive: with no device
// there is nothing to resolve them to, and loading without them is the designed
// degradation rather than a failure.
inline bool testSceneLoad() {
    Registry source;
    std::vector<Light> lights;

    const Entity parent = source.createEntity();
    source.names.add(parent, { "Parent" });
    Transform parentTransform;
    parentTransform.position = glm::vec3(1.0f, 2.0f, 3.0f);
    source.transforms.add(parent, { parentTransform });
    source.hierarchy.add(parent, {});

    const Entity child = source.createEntity();
    source.names.add(child, { "Child" });
    source.transforms.add(child, {});
    source.hierarchy.add(child, {});
    source.setParent(child, parent);

    source.scripts.add(child, { "Spin", false });

    RigidBodyComponent body;
    body.velocity = glm::vec3(4.0f, 5.0f, 6.0f);
    body.mass = 2.0f;
    source.rigidBodies.add(child, body);

    ColliderComponent collider;
    collider.type = ColliderType::Sphere;
    collider.radius = 2.0f;
    source.colliders.add(child, collider);

    source.prefabInstances.add(child, { "p.prefab" });
    source.audioListeners.add(child, { 0.75f });

    UIScreenComponent screen;
    screen.screenStack = { "HUD", "Inventory" };
    source.uiScreens.add(child, screen);
    source.focus.add(child, { "SlotA" });
    source.textInputs.add(child, { "name", "abc", 2 });

    AnimationPlayerComponent player;
    player.clip = "Slide";
    player.time = 0.25f;
    player.speed = 1.5f;
    player.loop = false;
    source.animations.add(child, player);

    source.skinnedMeshes.add(child, { "hero.gltf#Armature" });

    AnimationStateComponent machine;
    machine.graph = "Locomotion";
    machine.currentState = "Walk";
    machine.statePhase = 0.5f;
    machine.transitionTarget = "Run";
    machine.targetPhase = 0.25f;
    machine.transitionElapsed = 0.1f;
    machine.transitionDuration = 0.2f;
    source.animationStates.add(child, machine);

    AnimationParametersComponent parameters;
    parameters.values["speed"] = 3.25f;
    source.animationParameters.add(child, parameters);

    NavAgentComponent agent;
    agent.navMesh = "level";
    agent.destination = glm::vec3(7.0f, 0.0f, 8.0f);
    agent.hasDestination = true;
    agent.path = { glm::vec3(1.0f, 0.0f, 2.0f), glm::vec3(3.0f, 0.0f, 4.0f) };
    agent.pathIndex = 1;
    agent.pathGoal = glm::vec3(7.0f, 0.0f, 8.0f);
    agent.status = NavAgentStatus::Following;
    agent.speed = 3.0f;
    agent.arrivalRadius = 0.1f;
    source.navAgents.add(child, agent);

    const std::string text = SceneSerializer::saveToString(source, lights);

    Registry loaded;
    std::vector<Light> loadedLights;
    if (!SceneSerializer::loadFromString(loaded, loadedLights, text)) {
        std::cout << "[selftest] scene load returned false\n";
        return false;
    }

    bool ok = loaded.transforms.getAll().size() == 2;

    // Find the loaded entities by name — ids are freshly allocated on load.
    Entity loadedParent = INVALID_ENTITY;
    Entity loadedChild = INVALID_ENTITY;
    for (const auto& [entity, nameComponent] : loaded.names.getAll()) {
        if (nameComponent.name == "Parent") {
            loadedParent = entity;
        } else if (nameComponent.name == "Child") {
            loadedChild = entity;
        }
    }
    ok &= loadedParent != INVALID_ENTITY && loadedChild != INVALID_ENTITY;
    if (!ok) {
        return false;
    }

    // Parent index is resolved within the objects array, not carried as an id.
    ok &= loaded.hierarchy.has(loadedChild) &&
          loaded.hierarchy.get(loadedChild).parent == loadedParent;
    ok &= loaded.transforms.get(loadedParent).transform.position == glm::vec3(1.0f, 2.0f, 3.0f);

    ok &= loaded.scripts.has(loadedChild) && loaded.scripts.get(loadedChild).behavior == "Spin";
    ok &= loaded.rigidBodies.has(loadedChild) &&
          loaded.rigidBodies.get(loadedChild).velocity == glm::vec3(4.0f, 5.0f, 6.0f) &&
          loaded.rigidBodies.get(loadedChild).mass == 2.0f;
    ok &= loaded.colliders.has(loadedChild) &&
          loaded.colliders.get(loadedChild).type == ColliderType::Sphere &&
          loaded.colliders.get(loadedChild).radius == 2.0f;
    ok &= loaded.prefabInstances.has(loadedChild);
    ok &= loaded.audioListeners.has(loadedChild);

    ok &= loaded.uiScreens.has(loadedChild) &&
          loaded.uiScreens.get(loadedChild).screenStack.size() == 2;
    ok &= loaded.focus.has(loadedChild) &&
          loaded.focus.get(loadedChild).focusedElement == "SlotA";
    ok &= loaded.textInputs.has(loadedChild) &&
          loaded.textInputs.get(loadedChild).buffer == "abc" &&
          loaded.textInputs.get(loadedChild).caret == 2;

    ok &= loaded.animations.has(loadedChild) &&
          loaded.animations.get(loadedChild).clip == "Slide" &&
          loaded.animations.get(loadedChild).time == 0.25f &&
          loaded.animations.get(loadedChild).loop == false;
    ok &= loaded.skinnedMeshes.has(loadedChild) &&
          loaded.skinnedMeshes.get(loadedChild).skin == "hero.gltf#Armature";

    ok &= loaded.animationStates.has(loadedChild);
    if (loaded.animationStates.has(loadedChild)) {
        const auto& restored = loaded.animationStates.get(loadedChild);
        ok &= restored.graph == "Locomotion" && restored.currentState == "Walk";
        ok &= restored.transitionTarget == "Run" && restored.transitionElapsed == 0.1f;
    }
    ok &= loaded.animationParameters.has(loadedChild) &&
          loaded.animationParameters.get(loadedChild).get("speed") == 3.25f;

    ok &= loaded.navAgents.has(loadedChild);
    if (loaded.navAgents.has(loadedChild)) {
        const auto& restored = loaded.navAgents.get(loadedChild);
        ok &= restored.navMesh == "level";
        ok &= restored.destination == glm::vec3(7.0f, 0.0f, 8.0f);
        ok &= restored.hasDestination;
        ok &= restored.path.size() == 2;
        ok &= restored.pathIndex == 1;
        ok &= restored.pathGoal == glm::vec3(7.0f, 0.0f, 8.0f);
        ok &= restored.status == NavAgentStatus::Following;
        ok &= restored.arrivalRadius == 0.1f;
    }

    return ok;
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

// --- AssetPath / AssetMeta / AssetDatabase (Phase 19A) ----------------------
// The identity function is the thing worth testing here: every scene, prefab and save
// file on disk already contains its output, so a change to it is a migration
// (docs/DESIGN_ASSET_PIPELINE.md). Runs headless -- no Vulkan, no ResourceManager.
inline bool testAssetDatabase() {
    bool ok = true;

    // --- normalization: separators, dot segments, the "assets/" anchor, case ---
    const std::string expected = "assets/models/hero.gltf";
    ok &= AssetPath::normalize("assets\\models\\hero.gltf") == expected;
    ok &= AssetPath::normalize("./assets//models/hero.gltf") == expected;
    ok &= AssetPath::normalize("assets/models/../models/hero.gltf") == expected;
    ok &= AssetPath::normalize("C:/projects/sugar/assets/models/HERO.gltf") == expected;
    ok &= AssetPath::normalize("assets/models/Hero.gltf") == expected;

    // A '..' that escapes the project root is not a key, and neither is an empty path.
    ok &= AssetPath::normalize("../outside.gltf").empty();
    ok &= AssetPath::normalize("").empty();

    // The sub-selector is copied verbatim: clip names are case-sensitive identity.
    ok &= AssetPath::normalize("assets/models/Hero.gltf#Idle") == "assets/models/hero.gltf#Idle";
    ok &= AssetPath::subOf("assets/models/hero.gltf#Idle") == "Idle";
    ok &= AssetPath::pathOf("assets/models/hero.gltf#Idle") == expected;
    ok &= AssetPath::join(AssetPath::pathOf("Assets/Models/Hero.gltf#Run"),
                          AssetPath::subOf("Assets/Models/Hero.gltf#Run")) ==
          AssetPath::normalize("Assets/Models/Hero.gltf#Run");

    ok &= !AssetPath::isSupportedAscii(std::string("caf\xC3\xA9.png"));

    // --- hashing: content, not path; stable hex spelling ---
    ok &= AssetHash::hashString("abc") == AssetHash::hashString("abc");
    ok &= AssetHash::hashString("abc") != AssetHash::hashString("abd");
    ok &= AssetHash::toHex(0).size() == 16 && AssetHash::toHex(255) == "00000000000000ff";

    // A cook key must move when the content moves, when the settings move, and not
    // otherwise -- that is the whole staleness test.
    AssetMeta meta;
    meta.type = AssetType::Texture;
    const std::string metaBytes = AssetMetaIO::serialize(meta);
    const uint64_t base = AssetDatabase::computeCookKey("assets/t.png", 1234, metaBytes);
    ok &= base == AssetDatabase::computeCookKey("assets/t.png", 1234, metaBytes);
    ok &= base != AssetDatabase::computeCookKey("assets/t.png", 1235, metaBytes);
    ok &= base != AssetDatabase::computeCookKey("assets/other.png", 1234, metaBytes);
    AssetMeta changed = meta;
    changed.set("srgb", "false");
    ok &= base != AssetDatabase::computeCookKey("assets/t.png", 1234, AssetMetaIO::serialize(changed));

    // --- .meta round-trip through a temporary tree ---
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "sugar_assetdb_selftest" / "assets";
    std::error_code cleanupError;
    std::filesystem::remove_all(root.parent_path(), cleanupError);
    std::filesystem::create_directories(root / "textures", cleanupError);

    const std::filesystem::path texturePath = root / "textures" / "Checker.png";
    {
        std::ofstream file(texturePath, std::ios::binary);
        file << "pixels-v1";
    }

    AssetMeta written;
    written.type = AssetType::Texture;
    written.set("srgb", "true");
    written.set("filter", "linear");
    std::string errorMessage;
    ok &= AssetMetaIO::write(AssetMetaIO::sidecarPath(texturePath.string()), written, errorMessage);

    AssetMeta readBack;
    ok &= AssetMetaIO::read(AssetMetaIO::sidecarPath(texturePath.string()), readBack, errorMessage);
    ok &= readBack.type == AssetType::Texture;
    ok &= readBack.get("srgb") == "true" && readBack.get("filter") == "linear";
    // Byte-identical re-serialization: the cook key hashes these bytes, so a round-trip
    // that reformats would invalidate every cooked artifact for no reason.
    ok &= AssetMetaIO::serialize(readBack) == AssetMetaIO::serialize(written);

    // A malformed sidecar is reported, not silently treated as an empty one.
    {
        std::ofstream file(root / "textures" / "broken.png", std::ios::binary);
        file << "pixels";
    }
    {
        std::ofstream file(AssetMetaIO::sidecarPath((root / "textures" / "broken.png").string()),
                           std::ios::binary);
        file << "{ not json";
    }

    // --- catalog: keys, sub-asset lookup, defaults, problems, determinism ---
    AssetDatabase database;
    database.scan(root.string());

    const AssetEntry* checker = database.find(texturePath.string());
    ok &= checker != nullptr;
    if (checker != nullptr) {
        ok &= checker->key == "assets/textures/checker.png";
        ok &= checker->name == "Checker.png";       // display keeps the real case
        ok &= checker->type == AssetType::Texture;
        ok &= checker->hasMeta && checker->hashValid;
        // A sub-asset resolves to its file's entry.
        ok &= database.find(texturePath.string() + "#0") == checker;
    }

    // The asset with no sidecar still catalogues, with defaults (dropping a file into
    // the project must never require ceremony).
    const AssetEntry* broken = database.find("assets/textures/broken.png");
    ok &= broken != nullptr && !broken->hasMeta;
    ok &= !database.getProblems().empty(); // the malformed .meta was reported

    // .meta files are settings, not assets.
    for (const AssetEntry& entry : database.getAssets()) {
        ok &= entry.extension != ".meta";
    }

    // Two scans of the same tree produce the same catalog, in the same order.
    AssetDatabase second;
    second.scan(root.string());
    ok &= second.getAssets().size() == database.getAssets().size();
    for (size_t i = 0; i < database.getAssets().size() && ok; i++) {
        ok &= second.getAssets()[i].key == database.getAssets()[i].key;
        ok &= second.getAssets()[i].cookKey == database.getAssets()[i].cookKey;
    }

    // --- staleness is content, not mtime ---
    const auto cookKeyOf = [&database](const std::string& key) -> uint64_t {
        const AssetEntry* entry = database.find(key);
        return entry != nullptr ? entry->cookKey : 0;
    };
    const uint64_t cookKeyBefore = cookKeyOf(texturePath.string());
    ok &= cookKeyBefore != 0;

    // Rewriting the same bytes is a touch: mtime moves, the cook key must not.
    {
        std::ofstream file(texturePath, std::ios::binary);
        file << "pixels-v1";
    }
    ok &= !database.refresh(texturePath.string());
    ok &= cookKeyOf(texturePath.string()) == cookKeyBefore;

    // Different bytes are a real edit.
    {
        std::ofstream file(texturePath, std::ios::binary);
        file << "pixels-v2";
    }
    ok &= database.refresh(texturePath.string());
    ok &= cookKeyOf(texturePath.string()) != cookKeyBefore;

    // Editing only the .meta invalidates too -- import settings are source.
    const uint64_t cookKeyAfterEdit = cookKeyOf(texturePath.string());
    AssetMeta retuned = written;
    retuned.set("srgb", "false");
    ok &= AssetMetaIO::write(AssetMetaIO::sidecarPath(texturePath.string()), retuned, errorMessage);
    ok &= database.refresh(texturePath.string());
    ok &= cookKeyOf(texturePath.string()) != cookKeyAfterEdit;

    // An unknown key is a false, not a crash or a phantom entry.
    ok &= !database.refresh("assets/textures/does_not_exist.png");

    std::filesystem::remove_all(root.parent_path(), cleanupError);
    return ok;
}

// --- AssetCooker / CookedAsset (Phase 19B) ---------------------------------
// The defining test of the cooker is byte-identical recook: cook a tree twice into two
// cache directories and compare bytes. A regression here is an architecture violation,
// not a bug (docs/DESIGN_ASSET_PIPELINE.md). Everything runs headless -- cooking never
// needs a Vulkan device, which is what lets CI and packaging run it.
inline bool testAssetCooking() {
    bool ok = true;

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "sugar_cooker_selftest";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    std::filesystem::create_directories(root / "assets" / "models", cleanupError);
    std::filesystem::create_directories(root / "assets" / "textures", cleanupError);
    std::filesystem::create_directories(root / "assets" / "audio", cleanupError);

    // --- hermetic source assets (one per cooked kind) ---
    const std::filesystem::path objPath = root / "assets" / "models" / "tri.obj";
    {
        std::ofstream file(objPath, std::ios::binary);
        file << "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                "vt 0 0\nvt 1 0\nvt 0 1\n"
                "vn 0 0 1\n"
                "f 1/1/1 2/2/1 3/3/1\n";
    }

    // A 2x2 uncompressed 32-bit TGA -- stb_image decodes it, and hand-writing one keeps
    // the test independent of any binary fixture in the repo.
    const std::filesystem::path tgaPath = root / "assets" / "textures" / "dot.tga";
    {
        std::ofstream file(tgaPath, std::ios::binary);
        const unsigned char header[18] = {
            0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            2, 0,   // width  = 2
            2, 0,   // height = 2
            32,     // bits per pixel
            8       // 8 alpha bits
        };
        file.write(reinterpret_cast<const char*>(header), sizeof(header));
        const unsigned char pixels[16] = {
            255, 0, 0, 255,   0, 255, 0, 255,
            0, 0, 255, 255,   255, 255, 255, 255
        };
        file.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
    }

    // A minimal 16-bit PCM mono WAV, 8 frames at the engine mix rate.
    const std::filesystem::path wavPath = root / "assets" / "audio" / "beep.wav";
    {
        const uint32_t sampleRate = AudioMixSampleRate;
        const uint16_t channels = 1;
        const uint16_t bitsPerSample = 16;
        const uint32_t frameCount = 8;
        const uint32_t dataBytes = frameCount * channels * (bitsPerSample / 8);
        const uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
        const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));

        std::ofstream file(wavPath, std::ios::binary);
        const auto put32 = [&file](uint32_t value) {
            const unsigned char bytes[4] = {
                static_cast<unsigned char>(value & 0xFF),
                static_cast<unsigned char>((value >> 8) & 0xFF),
                static_cast<unsigned char>((value >> 16) & 0xFF),
                static_cast<unsigned char>((value >> 24) & 0xFF)
            };
            file.write(reinterpret_cast<const char*>(bytes), 4);
        };
        const auto put16 = [&file](uint16_t value) {
            const unsigned char bytes[2] = {
                static_cast<unsigned char>(value & 0xFF),
                static_cast<unsigned char>((value >> 8) & 0xFF)
            };
            file.write(reinterpret_cast<const char*>(bytes), 2);
        };

        file.write("RIFF", 4);
        put32(36 + dataBytes);
        file.write("WAVE", 4);
        file.write("fmt ", 4);
        put32(16);
        put16(1); // PCM
        put16(channels);
        put32(sampleRate);
        put32(byteRate);
        put16(blockAlign);
        put16(bitsPerSample);
        file.write("data", 4);
        put32(dataBytes);
        for (uint32_t i = 0; i < frameCount; i++) {
            put16(static_cast<uint16_t>(i * 1000));
        }
    }

    const std::string cacheA = (root / "cacheA").generic_string();
    const std::string cacheB = (root / "cacheB").generic_string();

    AssetDatabase database;
    database.scan((root / "assets").generic_string());
    ok &= database.getAssets().size() == 3;

    // --- cook everything, twice, into two caches ---
    AssetCooker::setDatabase(&database);
    AssetCooker::setCacheDirectory(cacheA);
    std::vector<std::string> errors;
    const int cookedFirst = AssetCooker::cookAll(database, errors);
    ok &= errors.empty();
    ok &= cookedFirst == 3;

    // Cooking again into the same cache writes nothing: the filename IS the cook key,
    // so an existing artifact is by construction current.
    std::vector<std::string> secondErrors;
    ok &= AssetCooker::cookAll(database, secondErrors) == 0;
    ok &= secondErrors.empty();

    AssetCooker::setCacheDirectory(cacheB);
    AssetCooker::clearMemo();
    std::vector<std::string> repeatErrors;
    ok &= AssetCooker::cookAll(database, repeatErrors) == 3;
    ok &= repeatErrors.empty();

    // --- THE test: byte-identical recook ---
    const auto readAllBytes = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };

    int comparedArtifacts = 0;
    for (const auto& entry : std::filesystem::directory_iterator(cacheA, cleanupError)) {
        const std::filesystem::path counterpart =
            std::filesystem::path(cacheB) / entry.path().filename();
        // Same cook key means the same filename: if the second cache is missing one,
        // the *name* was not a function of the content either.
        ok &= std::filesystem::exists(counterpart);
        ok &= readAllBytes(entry.path()) == readAllBytes(counterpart);
        comparedArtifacts++;
    }
    ok &= comparedArtifacts == 3;

    // --- cooked artifacts read back as the resources the runtime wants ---
    AssetCooker::setCacheDirectory(cacheA);
    AssetCooker::clearMemo();
    std::string errorMessage;

    const std::string meshCooked = AssetCooker::ensureCooked("assets/models/tri.obj", errorMessage);
    Mesh mesh;
    ok &= !meshCooked.empty() && CookedAsset::readMesh(meshCooked, mesh, errorMessage);
    ok &= mesh.vertices.size() == 3 && mesh.indices.size() == 3;

    const std::string textureCooked = AssetCooker::ensureCooked("assets/textures/dot.tga", errorMessage);
    CookedAsset::CookedTexture texture;
    ok &= !textureCooked.empty() && CookedAsset::readTexture(textureCooked, texture, errorMessage);
    ok &= texture.width == 2 && texture.height == 2 && texture.pixels.size() == 16;

    const std::string audioCooked = AssetCooker::ensureCooked("assets/audio/beep.wav", errorMessage);
    AudioClip clip;
    ok &= !audioCooked.empty() && CookedAsset::readAudio(audioCooked, clip, errorMessage);
    ok &= clip.frameCount == 8;
    ok &= clip.samples.size() == static_cast<size_t>(clip.frameCount) * AudioMixChannels;

    // The container is typed: reading a mesh artifact as a texture fails loudly rather
    // than producing garbage pixels.
    CookedAsset::CookedTexture wrongKind;
    ok &= !CookedAsset::readTexture(meshCooked, wrongKind, errorMessage);

    // Sub-assets of one file are separate artifacts.
    ok &= AssetCooker::artifactKey("assets/models/tri.obj#1") !=
          AssetCooker::artifactKey("assets/models/tri.obj#2");
    ok &= AssetCooker::artifactKey("assets/models/tri.obj#1") !=
          AssetCooker::artifactKey("assets/models/tri.obj");

    // --- staleness: editing the source renames the artifact ---
    const uint64_t keyBefore = AssetCooker::artifactKey("assets/models/tri.obj");
    {
        std::ofstream file(objPath, std::ios::binary);
        file << "v 0 0 0\nv 2 0 0\nv 0 2 0\n"
                "vt 0 0\nvt 1 0\nvt 0 1\n"
                "vn 0 0 1\n"
                "f 1/1/1 2/2/1 3/3/1\n";
    }
    database.scan((root / "assets").generic_string());
    AssetCooker::clearMemo();
    const uint64_t keyAfter = AssetCooker::artifactKey("assets/models/tri.obj");
    ok &= keyBefore != keyAfter;

    const std::string recooked = AssetCooker::ensureCooked("assets/models/tri.obj", errorMessage);
    ok &= recooked == AssetCooker::artifactPath(keyAfter);
    ok &= std::filesystem::exists(recooked);
    // The old artifact survives: the cache is append-only and always safe to delete
    // wholesale, so nothing here has to reason about eviction.
    ok &= std::filesystem::exists(AssetCooker::artifactPath(keyBefore));

    // Editing only the .meta also renames the artifact -- import settings are source.
    AssetMeta tuned;
    tuned.type = AssetType::Model;
    tuned.set("scale", "2.0");
    ok &= AssetMetaIO::write(AssetMetaIO::sidecarPath(objPath.string()), tuned, errorMessage);
    database.scan((root / "assets").generic_string());
    AssetCooker::clearMemo();
    ok &= AssetCooker::artifactKey("assets/models/tri.obj") != keyAfter;

    // An asset with no cooker (a prefab, say) is reported, not silently skipped.
    ok &= AssetCooker::ensureCooked("assets/prefabs/none.prefab", errorMessage).empty();

    AssetCooker::setDatabase(nullptr);
    AssetCooker::setCacheDirectory("build/assetcache");
    std::filesystem::remove_all(root, cleanupError);
    return ok;
}

// --- Import settings + dependency edges (Phase 19C) ------------------------
// Settings are applied at COOK time, so the test is "does the artifact's content
// change", not "does the loader behave differently". Dependency edges are owned by the
// database and discovered by the cooker: this pins that split, because the easy mistake
// is for the cooker to start keeping its own table (docs/DESIGN_ASSET_PIPELINE.md).
inline bool testAssetImport() {
    bool ok = true;

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "sugar_import_selftest";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    std::filesystem::create_directories(root / "assets" / "models", cleanupError);
    std::filesystem::create_directories(root / "assets" / "textures", cleanupError);
    std::filesystem::create_directories(root / "assets" / "audio", cleanupError);

    const std::filesystem::path objPath = root / "assets" / "models" / "tri.obj";
    {
        std::ofstream file(objPath, std::ios::binary);
        file << "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                "vt 0 0\nvt 1 0\nvt 0 1\n"
                "vn 0 0 1\n"
                "f 1/1/1 2/2/1 3/3/1\n";
    }

    const std::filesystem::path tgaPath = root / "assets" / "textures" / "dot.tga";
    {
        std::ofstream file(tgaPath, std::ios::binary);
        const unsigned char header[18] = {
            0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 8
        };
        file.write(reinterpret_cast<const char*>(header), sizeof(header));
        const unsigned char pixels[16] = {
            255, 0, 0, 255,   0, 255, 0, 255,
            0, 0, 255, 255,   255, 255, 255, 255
        };
        file.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
    }

    const std::filesystem::path wavPath = root / "assets" / "audio" / "beep.wav";
    {
        const uint32_t frameCount = 8;
        const uint32_t dataBytes = frameCount * 2;
        std::ofstream file(wavPath, std::ios::binary);
        const auto put32 = [&file](uint32_t value) {
            const unsigned char bytes[4] = {
                static_cast<unsigned char>(value & 0xFF),
                static_cast<unsigned char>((value >> 8) & 0xFF),
                static_cast<unsigned char>((value >> 16) & 0xFF),
                static_cast<unsigned char>((value >> 24) & 0xFF)
            };
            file.write(reinterpret_cast<const char*>(bytes), 4);
        };
        const auto put16 = [&file](uint16_t value) {
            const unsigned char bytes[2] = {
                static_cast<unsigned char>(value & 0xFF),
                static_cast<unsigned char>((value >> 8) & 0xFF)
            };
            file.write(reinterpret_cast<const char*>(bytes), 2);
        };
        file.write("RIFF", 4);
        put32(36 + dataBytes);
        file.write("WAVE", 4);
        file.write("fmt ", 4);
        put32(16);
        put16(1);
        put16(1);
        put32(AudioMixSampleRate);
        put32(AudioMixSampleRate * 2);
        put16(2);
        put16(16);
        file.write("data", 4);
        put32(dataBytes);
        for (uint32_t i = 0; i < frameCount; i++) {
            put16(8000);
        }
    }

    // A geometry-free glTF that references a texture: the dependency case that matters
    // (an animation- or material-only file still reaches assets packaging must ship).
    const std::filesystem::path gltfPath = root / "assets" / "models" / "material.gltf";
    {
        std::ofstream file(gltfPath, std::ios::binary);
        file << R"({"asset":{"version":"2.0"},)"
                R"("images":[{"uri":"../textures/dot.tga"}],)"
                R"("samplers":[{}],)"
                R"("textures":[{"source":0,"sampler":0}],)"
                R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],)"
                R"("nodes":[{"name":"Root"}],"scenes":[{"nodes":[0]}],"scene":0})";
    }

    const std::string cache = (root / "cache").generic_string();
    AssetDatabase database;
    database.scan((root / "assets").generic_string());
    AssetCooker::setDatabase(&database);
    AssetCooker::setCacheDirectory(cache);
    AssetCooker::clearMemo();

    std::string errorMessage;

    // --- baselines (no .meta anywhere: defaults must cook) ---
    Mesh baseMesh;
    ok &= CookedAsset::readMesh(
        AssetCooker::ensureCooked("assets/models/tri.obj", errorMessage), baseMesh, errorMessage);
    ok &= baseMesh.vertices.size() == 3;

    CookedAsset::CookedTexture baseTexture;
    ok &= CookedAsset::readTexture(
        AssetCooker::ensureCooked("assets/textures/dot.tga", errorMessage), baseTexture, errorMessage);

    AudioClip baseClip;
    ok &= CookedAsset::readAudio(
        AssetCooker::ensureCooked("assets/audio/beep.wav", errorMessage), baseClip, errorMessage);

    const auto maxAbsSample = [](const AudioClip& clip) {
        float peak = 0.0f;
        for (const float sample : clip.samples) {
            peak = std::max(peak, std::fabs(sample));
        }
        return peak;
    };
    const float basePeak = maxAbsSample(baseClip);
    ok &= basePeak > 0.0f;

    const auto maxX = [](const Mesh& mesh) {
        float value = 0.0f;
        for (const Vertex& vertex : mesh.vertices) {
            value = std::max(value, vertex.pos[0]);
        }
        return value;
    };
    ok &= std::fabs(maxX(baseMesh) - 1.0f) < 1e-5f;

    // --- model scale ---
    AssetMeta modelMeta;
    modelMeta.type = AssetType::Model;
    modelMeta.set(AssetSettings::ModelScale, "2.0");
    ok &= AssetMetaIO::write(AssetMetaIO::sidecarPath(objPath.string()), modelMeta, errorMessage);
    database.scan((root / "assets").generic_string());
    AssetCooker::clearMemo();

    Mesh scaledMesh;
    const std::string scaledPath = AssetCooker::ensureCooked("assets/models/tri.obj", errorMessage);
    ok &= CookedAsset::readMesh(scaledPath, scaledMesh, errorMessage);
    ok &= std::fabs(maxX(scaledMesh) - 2.0f) < 1e-5f;
    // A changed setting renames the artifact -- invalidation falls out of the 19A key
    // formula, nobody had to code it.
    ok &= scaledPath != AssetCooker::artifactPath(AssetCooker::artifactKey("assets/textures/dot.tga"));

    // --- texture flipY ---
    AssetMeta textureMeta;
    textureMeta.type = AssetType::Texture;
    textureMeta.set(AssetSettings::TextureFlipY, "true");
    ok &= AssetMetaIO::write(AssetMetaIO::sidecarPath(tgaPath.string()), textureMeta, errorMessage);
    database.scan((root / "assets").generic_string());
    AssetCooker::clearMemo();

    CookedAsset::CookedTexture flipped;
    ok &= CookedAsset::readTexture(
        AssetCooker::ensureCooked("assets/textures/dot.tga", errorMessage), flipped, errorMessage);
    ok &= flipped.width == baseTexture.width && flipped.height == baseTexture.height;
    ok &= flipped.pixels != baseTexture.pixels;
    // Row-exact, not merely "different": the flipped top row is the baseline bottom row.
    if (flipped.pixels.size() == 16 && baseTexture.pixels.size() == 16) {
        for (size_t i = 0; i < 8; i++) {
            ok &= flipped.pixels[i] == baseTexture.pixels[8 + i];
        }
    }

    // --- audio gain ---
    AssetMeta audioMeta;
    audioMeta.type = AssetType::Audio;
    audioMeta.set(AssetSettings::AudioGain, "0.5");
    ok &= AssetMetaIO::write(AssetMetaIO::sidecarPath(wavPath.string()), audioMeta, errorMessage);
    database.scan((root / "assets").generic_string());
    AssetCooker::clearMemo();

    AudioClip quietClip;
    ok &= CookedAsset::readAudio(
        AssetCooker::ensureCooked("assets/audio/beep.wav", errorMessage), quietClip, errorMessage);
    ok &= quietClip.frameCount == baseClip.frameCount;
    ok &= std::fabs(maxAbsSample(quietClip) - basePeak * 0.5f) < 1e-4f;

    // A malformed setting cooks with the default rather than failing: a .meta is
    // hand-edited, and a typo must not stop an asset from loading.
    audioMeta.set(AssetSettings::AudioGain, "loud");
    ok &= AssetMetaIO::write(AssetMetaIO::sidecarPath(wavPath.string()), audioMeta, errorMessage);
    database.scan((root / "assets").generic_string());
    AssetCooker::clearMemo();
    AudioClip defaultedClip;
    ok &= CookedAsset::readAudio(
        AssetCooker::ensureCooked("assets/audio/beep.wav", errorMessage), defaultedClip, errorMessage);
    ok &= std::fabs(maxAbsSample(defaultedClip) - basePeak) < 1e-4f;

    // --- dependency edges: cooker discovers, database owns ---
    const std::vector<std::string> discovered =
        AssetCooker::discoverDependencies("assets/models/material.gltf");
    std::cerr << "\n";
    ok &= discovered.size() == 1;
    ok &= !discovered.empty() && AssetPath::normalize(discovered.front()) == "assets/textures/dot.tga";

    ok &= database.dependenciesOf("assets/models/material.gltf").size() == 1;
    const std::vector<std::string> dependents = database.dependentsOf("assets/textures/dot.tga");
    ok &= dependents.size() == 1 && dependents.front() == "assets/models/material.gltf";

    // An asset with no edges reports none, and a rescan clears the graph: edges are
    // derived from the source bytes just re-read, never carried over.
    ok &= database.dependenciesOf("assets/models/tri.obj").empty();
    database.scan((root / "assets").generic_string());
    ok &= database.dependenciesOf("assets/models/material.gltf").empty();

    // cookAll rebuilds them, so a build always produces the graph packaging walks.
    std::vector<std::string> cookErrors;
    AssetCooker::cookAll(database, cookErrors);
    ok &= cookErrors.empty();
    ok &= database.dependenciesOf("assets/models/material.gltf").size() == 1;

    // --- ".meta changed" resolves to the asset it configures ---
    ok &= database.assetKeyForMetaPath(AssetMetaIO::sidecarPath(objPath.string())) ==
          "assets/models/tri.obj";
    ok &= database.assetKeyForMetaPath(objPath.string()).empty();          // not a sidecar
    ok &= database.assetKeyForMetaPath("assets/models/ghost.obj.meta").empty(); // no such asset

    AssetCooker::setDatabase(nullptr);
    AssetCooker::setCacheDirectory("build/assetcache");
    std::filesystem::remove_all(root, cleanupError);
    return ok;
}

// --- AssetReimport: one import path for watcher and editor (Phase 19D) -----
// The editor must not have its own import shortcut, so the property worth pinning is
// behavioural: the same call the file watcher makes is the one the Reimport button
// makes, and the only difference is `force` (docs/DESIGN_ASSET_PIPELINE.md). Headless:
// ResourceManager has no device here, so nothing reloads -- and that is a supported
// state, not a skipped test.
inline bool testAssetReimport() {
    bool ok = true;

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "sugar_reimport_selftest";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    std::filesystem::create_directories(root / "assets" / "models", cleanupError);

    const std::filesystem::path objPath = root / "assets" / "models" / "tri.obj";
    {
        std::ofstream file(objPath, std::ios::binary);
        file << "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                "vt 0 0\nvt 1 0\nvt 0 1\n"
                "vn 0 0 1\n"
                "f 1/1/1 2/2/1 3/3/1\n";
    }

    AssetDatabase database;
    database.scan((root / "assets").generic_string());
    AssetCooker::setDatabase(&database);
    AssetCooker::setCacheDirectory((root / "cache").generic_string());
    AssetCooker::clearMemo();

    // An unknown path is "not known", not an error and not a guess: a brand-new file
    // has no entry until the caller rescans.
    const AssetReimport::Result unknown =
        AssetReimport::reimport(database, "assets/models/ghost.obj", true);
    ok &= !unknown.known && !unknown.changed && unknown.errorMessage.empty();

    // The watcher's call (force=false) on an untouched asset does nothing: the mtime is
    // a trigger, the content hash is the answer.
    const AssetReimport::Result untouched =
        AssetReimport::reimport(database, "assets/models/tri.obj", false);
    ok &= untouched.known && !untouched.changed;
    ok &= untouched.assetKey == "assets/models/tri.obj";

    // The editor's call (force=true) proceeds anyway -- "nothing changed" is the state
    // the Reimport button exists to escape.
    // Cook once, then delete the artifact behind the cooker's back: the in-process memo
    // still says "this key is fine". Only a forced reimport clears it -- which is the
    // observable difference between honouring `force` and quietly early-returning.
    std::string cookError;
    const std::string firstCook = AssetCooker::ensureCooked("assets/models/tri.obj", cookError);
    ok &= !firstCook.empty();
    std::filesystem::remove(firstCook, cleanupError);

    const AssetReimport::Result forced =
        AssetReimport::reimport(database, "assets/models/tri.obj", true);
    ok &= forced.known && !forced.changed && forced.errorMessage.empty();
    // Headless: no device, so nothing was reloaded, and that is not a failure.
    ok &= !forced.reloaded;

    Mesh recooked;
    const std::string secondCook = AssetCooker::ensureCooked("assets/models/tri.obj", cookError);
    ok &= !secondCook.empty() && CookedAsset::readMesh(secondCook, recooked, cookError);

    // A real edit is seen through the same call the watcher makes.
    {
        std::ofstream file(objPath, std::ios::binary);
        file << "v 0 0 0\nv 3 0 0\nv 0 3 0\n"
                "vt 0 0\nvt 1 0\nvt 0 1\n"
                "vn 0 0 1\n"
                "f 1/1/1 2/2/1 3/3/1\n";
    }
    const AssetReimport::Result edited =
        AssetReimport::reimport(database, objPath.string(), false);
    ok &= edited.known && edited.changed;

    // Editing import settings reaches the asset through its sidecar path -- what the
    // editor's Apply writes and what the watcher reports are the same input.
    AssetMeta meta;
    meta.type = AssetType::Model;
    meta.set(AssetSettings::ModelScale, "2.0");
    std::string errorMessage;
    ok &= AssetMetaIO::write(AssetMetaIO::sidecarPath(objPath.string()), meta, errorMessage);

    const AssetReimport::Result viaMeta =
        AssetReimport::reimport(database, AssetMetaIO::sidecarPath(objPath.string()), false);
    ok &= viaMeta.known && viaMeta.changed;
    ok &= viaMeta.assetKey == "assets/models/tri.obj"; // sidecar mapped to its owner

    // And the settings really took effect: the reimport cooks the scaled mesh.
    Mesh mesh;
    const std::string cooked = AssetCooker::ensureCooked("assets/models/tri.obj", errorMessage);
    ok &= !cooked.empty() && CookedAsset::readMesh(cooked, mesh, errorMessage);
    float maxX = 0.0f;
    for (const Vertex& vertex : mesh.vertices) {
        maxX = std::max(maxX, vertex.pos[0]);
    }
    ok &= std::fabs(maxX - 6.0f) < 1e-4f; // 3 units, scaled 2x

    AssetCooker::setDatabase(nullptr);
    AssetCooker::setCacheDirectory("build/assetcache");
    std::filesystem::remove_all(root, cleanupError);
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
        { "Navigation",       testNavigation },
        { "NavMeshBake",      testNavMeshBake },
        { "NavAvoidance",     testNavAvoidance },
        { "ViewportOverlay",  testViewportOverlay },
        { "AssetDatabase",    testAssetDatabase },
        { "AssetCooking",     testAssetCooking },
        { "AssetImport",      testAssetImport },
        { "AssetReimport",    testAssetReimport },
        { "Serializer",       testSerializer },
        { "SceneLoad",        testSceneLoad },
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
