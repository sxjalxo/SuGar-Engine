#pragma once

// Opt-in QA / stress harness (SUGAR_STRESS=1). Where SelfTests.h checks each
// subsystem is *sane*, this hammers the load-bearing ones at scale and at edge
// inputs and asserts invariants — catching crashes, non-determinism, id
// collisions, and (most valuably) broadphase pairs that disagree with brute force.
// Headless (no Vulkan), single-pass (no timing loops), so it runs in seconds.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "animation/AnimationClip.h"
#include "animation/AnimationClipRegistry.h"
#include "animation/AnimationComponents.h"
#include "animation/AnimationGraph.h"
#include "animation/AnimationGraphRegistry.h"
#include "animation/AnimationStateSystem.h"
#include "animation/AnimationSystem.h"
#include "audio/AudioClip.h"
#include "audio/AudioEngine.h"
#include "core/SnapshotStorage.h"
#include "ecs/Registry.h"
#include "navigation/NavComponents.h"
#include "navigation/NavigationSystem.h"
#include "navigation/NavPath.h"
#include "navigation/NavMeshBuilder.h"
#include "navigation/NavMeshRegistry.h"
#include "physics/PhysicsWorld.h"
#include "scene/Light.h"
#include "scene/SceneSerializer.h"

namespace StressTests {

// Deterministic LCG so every run exercises the same scene.
struct Rng {
    uint32_t state;
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) * (1.0f / 16777216.0f); // [0,1)
    }
    float range(float lo, float hi) { return lo + (hi - lo) * next(); }
};

inline Entity addStaticBox(Registry& reg, const glm::vec3& pos, float half) {
    const Entity e = reg.createEntity();
    Transform t;
    t.position = pos;
    reg.transforms.add(e, { t });
    RigidBodyComponent body{};
    body.isStatic = true;
    body.useGravity = false;
    reg.rigidBodies.add(e, body);
    ColliderComponent collider{};
    collider.type = ColliderType::Box;
    collider.halfExtents = glm::vec3(half);
    reg.colliders.add(e, collider);
    return e;
}

// The ground-truth broadphase check: for an all-static-box scene the grid must
// report *exactly* the pairs whose axis-aligned boxes strictly overlap — no
// missed contacts (a physics bug), no spurious ones. Brute-force O(n^2) here is
// the oracle; the engine uses the grid.
inline bool gridMatchesBruteForce() {
    Registry reg;
    PhysicsWorld world;
    Rng rng{ 0xC0FFEEu };

    constexpr int N = 400;
    constexpr float half = 0.5f;
    // A tight volume so many boxes overlap (dense = the interesting case).
    std::vector<Entity> ids;
    std::vector<glm::vec3> pos;
    for (int i = 0; i < N; ++i) {
        const glm::vec3 p(rng.range(0.0f, 12.0f), rng.range(0.0f, 12.0f), rng.range(0.0f, 12.0f));
        ids.push_back(addStaticBox(reg, p, half));
        pos.push_back(p);
    }

    world.step(reg, 1.0f / 60.0f);

    std::set<std::pair<Entity, Entity>> gridPairs;
    for (const CollisionEvent& e : world.getCollisionEvents()) {
        gridPairs.insert({ std::min(e.a, e.b), std::max(e.a, e.b) });
    }

    std::set<std::pair<Entity, Entity>> refPairs;
    for (int i = 0; i < N; ++i) {
        for (int k = i + 1; k < N; ++k) {
            const glm::vec3 d = glm::abs(pos[i] - pos[k]);
            // Strict overlap on all axes matches testBoxBox (touching => no hit).
            if (d.x < 2.0f * half && d.y < 2.0f * half && d.z < 2.0f * half) {
                refPairs.insert({ std::min(ids[i], ids[k]), std::max(ids[i], ids[k]) });
            }
        }
    }

    if (gridPairs != refPairs) {
        std::cout << "[stress]   grid " << gridPairs.size() << " pairs vs brute " << refPairs.size() << "\n";
        return false;
    }
    return true;
}

// The grid must survive pathological inputs: huge coordinates (cell-key packing
// can wrap — the AABB reject must still keep it correct), and a big shape mixed
// with small ones (size disparity is the uniform grid's classic weak spot).
inline bool gridEdgeCases() {
    bool ok = true;

    { // extreme coordinates: two overlapping boxes 1e6 out, plus far singletons
        Registry reg;
        PhysicsWorld world;
        const Entity a = addStaticBox(reg, glm::vec3(1.0e6f, 0.0f, 0.0f), 0.5f);
        const Entity b = addStaticBox(reg, glm::vec3(1.0e6f + 0.3f, 0.0f, 0.0f), 0.5f);
        (void)addStaticBox(reg, glm::vec3(-1.0e6f, 0.0f, 0.0f), 0.5f);
        (void)addStaticBox(reg, glm::vec3(0.0f, 1.0e6f, 0.0f), 0.5f);
        world.step(reg, 1.0f / 60.0f);
        const auto& ev = world.getCollisionEvents();
        ok &= ev.size() == 1 && ((ev[0].a == a && ev[0].b == b) || (ev[0].a == b && ev[0].b == a));
    }

    { // size disparity: one large box overlapping many small ones
        Registry reg;
        PhysicsWorld world;
        const Entity big = addStaticBox(reg, glm::vec3(0.0f), 5.0f); // spans [-5,5]
        int expectedHits = 0;
        for (int i = 0; i < 50; ++i) {
            const float x = -6.0f + 0.3f * static_cast<float>(i); // some inside, some outside
            (void)addStaticBox(reg, glm::vec3(x, 0.0f, 0.0f), 0.5f);
            if (std::abs(x) < 5.0f + 0.5f) {
                ++expectedHits;
            }
        }
        world.step(reg, 1.0f / 60.0f);
        int bigHits = 0;
        for (const CollisionEvent& e : world.getCollisionEvents()) {
            if (e.a == big || e.b == big) {
                ++bigHits;
            }
        }
        // Small boxes are 0.6 apart, so adjacent ones don't touch; only the big
        // box's overlaps are asserted (its count must be exact).
        ok &= bigHits == expectedHits;
    }

    { // a NaN-positioned collider must not crash the grid (cellCoord clamps NaN to
      // a fixed bucket) and must not manufacture false contacts — a normal
      // overlapping pair alongside it is still found, and the NaN body touches no one.
        Registry reg;
        PhysicsWorld world;
        const Entity a = addStaticBox(reg, glm::vec3(0.0f), 0.5f);
        const Entity b = addStaticBox(reg, glm::vec3(0.3f, 0.0f, 0.0f), 0.5f); // overlaps a
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const Entity bad = addStaticBox(reg, glm::vec3(nan, nan, nan), 0.5f);
        world.step(reg, 1.0f / 60.0f);
        const auto& ev = world.getCollisionEvents();
        ok &= ev.size() == 1 && ((ev[0].a == a && ev[0].b == b) || (ev[0].a == b && ev[0].b == a));
        for (const CollisionEvent& e : ev) {
            ok &= e.a != bad && e.b != bad; // NaN body collides with nothing
        }
    }

    { // empty and single-collider scenes must not crash or emit
        Registry reg;
        PhysicsWorld world;
        world.step(reg, 1.0f / 60.0f);
        ok &= world.getCollisionEvents().empty();
        (void)addStaticBox(reg, glm::vec3(0.0f), 0.5f);
        world.step(reg, 1.0f / 60.0f);
        ok &= world.getCollisionEvents().empty();
    }

    return ok;
}

// The property the two tests above cannot see: that the grid is still a grid.
//
// Cell size used to come from the LARGEST shape, so one arena wall among a thousand
// projectiles made every cell wider than the playfield, every shape landed in one
// bucket, and the broadphase quietly became the all-pairs scan it replaced. Both
// correctness tests kept passing throughout — a degenerate grid answers every query
// right; it just takes O(n^2) comparisons to do it. So this one measures the work
// (DevDocs/DESIGN_BROADPHASE_SCALE.md).
inline bool gridScaleWithLargeShape() {
    const auto candidatesFor = [](int n) -> size_t {
        Registry reg;
        PhysicsWorld world;
        // The shape every game has: a floor far larger than anything standing on it.
        (void)addStaticBox(reg, glm::vec3(0.0f, -1.0f, 0.0f), 40.0f);
        // Small bodies spread out so almost none of them touch — the candidate count
        // is then a pure measure of how well the grid separates them.
        const int side = static_cast<int>(std::sqrt(static_cast<float>(n))) + 1;
        for (int i = 0; i < n; ++i) {
            const float x = static_cast<float>(i % side) * 2.0f - 60.0f;
            const float z = static_cast<float>(i / side) * 2.0f - 60.0f;
            (void)addStaticBox(reg, glm::vec3(x, 4.0f, z), 0.25f);
        }
        world.step(reg, 1.0f / 60.0f);
        return world.lastBroadphaseCandidateCount();
    };

    const size_t small = candidatesFor(300);
    const size_t large = candidatesFor(1200);

    // Linear-ish: 4x the bodies must not cost more than ~6x the tests. The degenerate
    // grid cost 16x (n^2), and the measured game case was 1 524 shapes -> 23 221 pairs.
    const bool ok = large < small * 6 && large < 12000;
    if (!ok) {
        std::cout << "[stress]   broadphase candidates: 300 -> " << small
                  << ", 1200 -> " << large << " (expected near-linear)\n";
    }
    return ok;
}

// A* work done by agents that cannot reach their goal.
//
// The pathology (friction log #36, second half): gameplay re-issues a destination every
// step — the natural way to write "chase the player" — and every agent whose goal is
// unreachable pays a **full A\* over the reachable component, every step, forever**. It
// was measured once at 58 agents: 243 FPS with the route open, 0.89 FPS with it closed.
//
// Nothing about it is visible to a correctness test: every one of those searches returns
// the right answer (Unreachable). Only the count shows the waste, which is why
// NavPath::searchesPerformed() exists (DevDocs/DESIGN_REPLAN_BACKOFF.md).
inline bool navReplanBackoff() {
    bool ok = true;

    // Two disconnected islands: agents stand on one, the goal is on the other, so every
    // search legitimately fails.
    // Built through the public builder, the way a game builds one: a triangle soup.
    std::vector<NavTriangle> triangles;
    const auto addQuad = [&triangles](float x0, float z0, float x1, float z1) {
        const glm::vec3 a(x0, 0.0f, z0), b(x0, 0.0f, z1), c(x1, 0.0f, z1), d(x1, 0.0f, z0);
        triangles.push_back({ a, b, c });
        triangles.push_back({ a, c, d });
    };
    addQuad(-10.0f, -10.0f, -2.0f, 10.0f);  // island A, where the agents stand
    addQuad(20.0f, -10.0f, 28.0f, 10.0f);   // island B, where the goal is
    NavMeshRegistry::registerNavMesh("islands", buildNavMesh(triangles));

    Registry reg;
    constexpr int AgentCount = 40;
    constexpr int Steps = 120;               // 2 seconds of fixed steps
    constexpr float Step = 1.0f / 60.0f;
    const glm::vec3 goal(24.0f, 0.0f, 0.0f); // on the far island: never reachable

    std::vector<Entity> agents;
    for (int i = 0; i < AgentCount; ++i) {
        const Entity e = reg.createEntity();
        Transform t;
        t.position = glm::vec3(-6.0f, 0.0f, -8.0f + 0.4f * static_cast<float>(i));
        reg.transforms.add(e, { t });
        NavAgentComponent agent;
        agent.navMesh = "islands";
        agent.speed = 3.0f;
        reg.navAgents.add(e, agent);
        agents.push_back(e);
    }

    NavPath::resetSearchCount();
    for (int step = 0; step < Steps; ++step) {
        // Gameplay re-issues the same unreachable destination every step — the shape
        // every chase behaviour has.
        for (const Entity e : agents) {
            reg.navAgents.get(e).setDestination(goal);
        }
        NavigationSystem::update(reg, Step);
    }
    const std::size_t searches = NavPath::searchesPerformed();
    std::cout << "[stress]   replan searches " << searches << " (unthrottled "
              << static_cast<std::size_t>(AgentCount) * Steps << ")" << std::endl;

    // Every agent must have *tried* — the backoff throttles retries, it does not
    // abandon the goal — and every one must still report Unreachable.
    for (const Entity e : agents) {
        ok &= reg.navAgents.get(e).status == NavAgentStatus::Unreachable;
    }
    ok &= searches >= static_cast<std::size_t>(AgentCount);

    // The property: bounded by the retry interval, not by the frame rate. Two seconds
    // at the default interval is a handful of attempts per agent, not one per step.
    const std::size_t unthrottled = static_cast<std::size_t>(AgentCount) * Steps;
    const std::size_t budget = static_cast<std::size_t>(AgentCount) * 8;
    if (searches > budget) {
        std::cout << "[stress]   replan searches " << searches << " (unthrottled would be "
                  << unthrottled << ", budget " << budget << ")\n";
        ok = false;
    }

    // A destination that actually CHANGES must still search immediately: the backoff
    // throttles repeats of a failed goal, never a new decision.
    NavPath::resetSearchCount();
    for (int i = 0; i < AgentCount; ++i) {
        reg.navAgents.get(agents[static_cast<std::size_t>(i)])
            .setDestination(goal + glm::vec3(0.0f, 0.0f, static_cast<float>(i) * 0.01f + 0.5f));
    }
    NavigationSystem::update(reg, Step);
    ok &= NavPath::searchesPerformed() == static_cast<std::size_t>(AgentCount);

    NavMeshRegistry::clear();
    return ok;
}

// Physics must be deterministic (the time-travel wedge depends on it): the same
// scene stepped twice yields identical events in identical order.
inline bool physicsDeterministic() {
    auto build = [](Registry& reg) {
        Rng rng{ 0x1234u };
        for (int i = 0; i < 500; ++i) {
            addStaticBox(reg, glm::vec3(rng.range(0.0f, 15.0f), rng.range(0.0f, 15.0f), rng.range(0.0f, 15.0f)), 0.5f);
        }
    };
    Registry r1, r2;
    build(r1);
    build(r2);
    PhysicsWorld w1, w2;
    w1.step(r1, 1.0f / 60.0f);
    w2.step(r2, 1.0f / 60.0f);
    const auto& e1 = w1.getCollisionEvents();
    const auto& e2 = w2.getCollisionEvents();
    if (e1.size() != e2.size()) {
        return false;
    }
    for (size_t i = 0; i < e1.size(); ++i) {
        if (e1[i].a != e2[i].a || e1[i].b != e2[i].b) {
            return false;
        }
    }
    return true;
}

// In-place restore at scale, and repeated: ids must never drift and state must
// return exactly, cycle after cycle (a scrub can restore hundreds of times).
inline bool patchStress() {
    Registry reg;
    std::vector<Light> lights;
    // N x cycles kept modest: patch is JSON-parse-bound (see the Phase 14C bench),
    // so this proves the invariant — no id drift, no leak, exact restore over many
    // cycles — without the harness running for minutes in a Debug build.
    constexpr int N = 2000;
    std::vector<Entity> ids;
    for (int i = 0; i < N; ++i) {
        const Entity e = reg.createEntity();
        reg.names.add(e, { "E" + std::to_string(i) });
        Transform t;
        t.position = glm::vec3(static_cast<float>(i), 0.0f, 0.0f);
        reg.transforms.add(e, { t });
        reg.hierarchy.add(e, {});
        RigidBodyComponent body{};
        body.velocity = glm::vec3(0.0f, static_cast<float>(i), 0.0f);
        reg.rigidBodies.add(e, body);
        ids.push_back(e);
    }

    const std::string frame0 = SceneSerializer::saveToString(reg, lights);
    if (frame0.empty()) {
        return false;
    }

    bool ok = true;
    constexpr int cycles = 30;
    for (int cycle = 0; cycle < cycles; ++cycle) {
        // Perturb, then restore.
        for (int i = 0; i < N; ++i) {
            reg.transforms.get(ids[i]).transform.position.x = -1.0f;
            reg.rigidBodies.get(ids[i]).velocity.y = -1.0f;
        }
        if (!SceneSerializer::patchFromString(reg, lights, frame0)) {
            return false;
        }
    }

    // Every original id still exists and holds its original data.
    for (int i = 0; i < N; ++i) {
        ok &= reg.transforms.has(ids[i]);
        ok &= reg.transforms.get(ids[i]).transform.position.x == static_cast<float>(i);
        ok &= reg.rigidBodies.get(ids[i]).velocity.y == static_cast<float>(i);
    }
    // No phantom entities were created across the restore cycles.
    ok &= static_cast<int>(reg.transforms.getAll().size()) == N;
    return ok;
}

// Create / destroy / recreate-with-id churn must never hand out a live id twice
// and must always restore the requested id.
inline bool idChurn() {
    Registry reg;
    constexpr int N = 2000;
    std::vector<Entity> ids;
    for (int i = 0; i < N; ++i) {
        const Entity e = reg.createEntity();
        reg.transforms.add(e, {});
        ids.push_back(e);
    }

    bool ok = true;
    for (int cycle = 0; cycle < 50; ++cycle) {
        // Destroy the middle half, then recreate exactly those ids.
        for (int i = N / 4; i < 3 * N / 4; ++i) {
            reg.destroyEntity(ids[i]);
        }
        for (int i = N / 4; i < 3 * N / 4; ++i) {
            const Entity got = reg.createEntityWithId(ids[i]);
            ok &= got == ids[i];
            reg.transforms.add(ids[i], {});
        }
    }

    // All original ids live, all distinct, count intact.
    std::set<Entity> live;
    for (Entity e : ids) {
        ok &= reg.transforms.has(e);
        live.insert(e);
    }
    ok &= static_cast<int>(live.size()) == N;
    ok &= static_cast<int>(reg.transforms.getAll().size()) == N;

    // A fresh entity gets a brand-new id, never one already live.
    const Entity fresh = reg.createEntity();
    ok &= live.find(fresh) == live.end();
    return ok;
}

// Ring churn far past capacity: count clamps, frame numbers stay monotonic and
// track the retained window.
inline bool ringChurn() {
    JsonSnapshotStorage ring(600);
    for (int i = 0; i < 100000; ++i) {
        ring.push("f" + std::to_string(i));
    }
    bool ok = ring.count() == 600;
    ok &= ring.frameNumber(0) == 100000 - 600;
    ok &= ring.frameNumber(599) == 99999;
    for (int i = 1; i < ring.count(); ++i) {
        ok &= ring.frameNumber(i) == ring.frameNumber(i - 1) + 1;
    }
    return ok;
}

// Builds a scene of many animated characters: half single-clip players, half
// state machines with a blend tree and a parameter transition. Each is a two-bone
// subtree (root + child), so the systems walk hierarchies and resolve targets by
// name at scale. Returns the roots.
inline std::vector<Entity> buildAnimationScene(Registry& reg, int count) {
    std::vector<Entity> roots;
    roots.reserve(count);
    Rng rng{ 0xA11CE };

    for (int i = 0; i < count; i++) {
        const Entity root = reg.createEntity();
        reg.names.add(root, { "Char" + std::to_string(i) });
        reg.transforms.add(root, {});
        reg.hierarchy.add(root, {});
        const Entity bone = reg.createEntity();
        reg.names.add(bone, { "Bone" });
        reg.transforms.add(bone, {});
        reg.hierarchy.add(bone, {});
        reg.setParent(bone, root);

        if (i % 2 == 0) {
            AnimationPlayerComponent player;
            player.clip = "Walk";
            player.time = rng.range(0.0f, 1.0f); // varied start phase
            player.speed = rng.range(0.5f, 2.0f);
            reg.animations.add(root, player);
        } else {
            AnimationStateComponent machine;
            machine.graph = "Loco";
            reg.animationStates.add(root, machine);
            AnimationParametersComponent parameters;
            parameters.values["speed"] = rng.range(0.0f, 1.0f);
            reg.animationParameters.add(root, parameters);
        }
        roots.push_back(root);
    }
    return roots;
}

inline void registerAnimationAssets() {
    AnimationClipRegistry::clear();
    AnimationGraphRegistry::clear();

    const auto clip = [](const char* name, float duration, float endX) {
        TransformTrack track;
        track.target = "Bone";
        track.translation.times = { 0.0f, duration };
        track.translation.values = { glm::vec3(0.0f), glm::vec3(endX, 0.0f, 0.0f) };
        AnimationClip c;
        c.name = name;
        c.tracks = { track };
        c.duration = computeDuration(c.tracks);
        return c;
    };
    AnimationClipRegistry::registerClip("Walk", clip("Walk", 1.0f, 3.0f));
    AnimationClipRegistry::registerClip("Run", clip("Run", 0.6f, 9.0f));

    AnimationGraph graph;
    graph.name = "Loco";
    graph.entryState = "Move";
    AnimationGraphState move;
    move.name = "Move";
    move.blendParameter = "speed";
    move.blendEntries = { { "Walk", 0.0f }, { "Run", 1.0f } };
    AnimationGraphState sprint;
    sprint.name = "Sprint";
    sprint.clip = "Run";
    graph.states = { move, sprint };
    AnimationTransition toSprint;
    toSprint.from = "Move";
    toSprint.to = "Sprint";
    toSprint.parameter = "speed";
    toSprint.condition = TransitionCondition::Greater;
    toSprint.threshold = 0.9f;
    toSprint.duration = 0.25f;
    graph.transitions = { toSprint };
    AnimationGraphRegistry::registerGraph("Loco", graph);
}

inline void stepAnimation(Registry& reg, int steps) {
    for (int i = 0; i < steps; i++) {
        AnimationSystem::update(reg, 1.0f / 60.0f);
        AnimationStateSystem::update(reg, 1.0f / 60.0f);
    }
}

inline glm::vec3 boneOf(Registry& reg, Entity root) {
    // The child bone is the one entity under root with a transform.
    for (Entity child : reg.hierarchy.get(root).children) {
        return reg.transforms.get(child).transform.position;
    }
    return glm::vec3(0.0f);
}

// 400 animated characters, 600 fixed steps: assert no crash, bit-identical
// determinism between two independent runs, and that a mid-sim snapshot survives a
// scrub. This is the first stress coverage of the Phase 17 pipeline end to end —
// players + state machines + blend trees + pose apply + serializer.
inline bool animationStress() {
    constexpr int kChars = 400;
    constexpr int kSteps = 600;
    bool ok = true;

    const auto runOnce = [&](std::vector<glm::vec3>& out) {
        registerAnimationAssets();
        Registry reg;
        const std::vector<Entity> roots = buildAnimationScene(reg, kChars);
        stepAnimation(reg, kSteps);
        out.clear();
        for (Entity root : roots) {
            out.push_back(boneOf(reg, root));
        }
    };

    std::vector<glm::vec3> a;
    std::vector<glm::vec3> b;
    runOnce(a);
    runOnce(b);
    ok &= a.size() == static_cast<size_t>(kChars) && a == b; // bit-identical

    // Nothing collapsed to the origin: every character actually posed.
    int moved = 0;
    for (const glm::vec3& p : a) {
        if (std::fabs(p.x) > 1e-4f) {
            moved++;
        }
    }
    ok &= moved > kChars / 2;

    // Snapshot survival at scale: capture mid-sim, run on, patch back, re-derive.
    registerAnimationAssets();
    Registry reg;
    std::vector<Light> lights;
    const std::vector<Entity> roots = buildAnimationScene(reg, kChars);
    stepAnimation(reg, 120);

    const std::string frame = SceneSerializer::saveToString(reg, lights);
    ok &= !frame.empty();

    std::vector<glm::vec3> captured;
    for (Entity root : roots) {
        captured.push_back(boneOf(reg, root));
    }

    stepAnimation(reg, 200); // diverge
    ok &= SceneSerializer::patchFromString(reg, lights, frame);
    // Re-derive the pose from the restored authoritative state (advance by nothing).
    AnimationSystem::update(reg, 0.0f);
    AnimationStateSystem::update(reg, 0.0f);

    for (size_t i = 0; i < roots.size(); i++) {
        const glm::vec3 now = boneOf(reg, roots[i]);
        if (std::fabs(now.x - captured[i].x) > 1e-3f) {
            ok = false;
            break;
        }
    }

    AnimationClipRegistry::clear();
    AnimationGraphRegistry::clear();
    return ok;
}

// --- Audio lock contention: DevDocs/DESIGN_AUDIO_THREAD_OWNERSHIP.md ------------
// The device callback (its own real-time thread) takes AudioEngine's mutex for the
// whole mix; the gameplay thread takes the same mutex in isActive()/setVoiceParams()
// -- and AudioSystem::update calls both, per source, per fixed step. That is a
// textbook priority-inversion shape, never contended by any game so far. This
// reproduces the exact access pattern -- isActive() then setVoiceParams(), two
// separate acquisitions per source -- for 1 000 simulated sources at 60 Hz, with
// one-shot play() bursts layered on, while a real device callback is actually
// mixing, and reports the three SUGAR_AUDIODBG instruments the design doc's
// Section 5 decision rule consumes. This case reports numbers; it does not render
// a verdict (that stays a human decision against the frozen rule).
//
// On a machine with no playback device, AudioEngine::init() returns false, the
// callback never runs, and every counter would read zero -- a green that measured
// nothing. That is detected below and reported SKIP, not PASS, the same way
// SelfTests.h treats an absent fixture.
//
// Unlike SelfTests.h's ResourceManager case -- which never has a device in this
// headless harness and so is simply left out of the table at compile time -- a
// playback device may or may not exist on the machine running this binary, so the
// SKIP can only be known after the case actually tries. run() below clears this
// flag before each case and reads it right after, so a SKIP renders as SKIPPED
// and is excluded from the passed/total measurement instead of counting as PASS.
// Exactly one case runs at a time in this harness, so plain (non-atomic) state
// is enough.
inline bool g_stressCaseSkipped = false;

inline bool audioLockContention() {
    // The instrument is opt-in and off by default; turn it on for the duration of
    // this measurement only, and restore whatever was there before.
#if defined(_WIN32)
    const char* previous = std::getenv("SUGAR_AUDIODBG");
    const std::string previousValue = previous != nullptr ? previous : "";
    const bool hadPrevious = previous != nullptr;
    _putenv_s("SUGAR_AUDIODBG", "1");
#else
    const char* previous = std::getenv("SUGAR_AUDIODBG");
    const std::string previousValue = previous != nullptr ? previous : "";
    const bool hadPrevious = previous != nullptr;
    setenv("SUGAR_AUDIODBG", "1", 1);
#endif

    const auto restoreEnv = [&]() {
#if defined(_WIN32)
        if (hadPrevious) {
            _putenv_s("SUGAR_AUDIODBG", previousValue.c_str());
        } else {
            _putenv("SUGAR_AUDIODBG=");
        }
#else
        if (hadPrevious) {
            setenv("SUGAR_AUDIODBG", previousValue.c_str(), 1);
        } else {
            unsetenv("SUGAR_AUDIODBG");
        }
#endif
    };

    AudioEngine engine;
    if (!engine.init()) {
        std::cerr << "[stress] AudioLockContention SKIPPED: no playback device on this "
                     "machine -- the device callback never ran, so nothing was measured. "
                     "This answers nothing about DESIGN_AUDIO_THREAD_OWNERSHIP.md Section 5.\n";
        restoreEnv();
        g_stressCaseSkipped = true; // tells run() to print SKIPPED, not PASS -- see above.
        return true; // SKIP, not a verdict -- see the SKIP precedent in SelfTests.h.
    }

    // A real, multi-second clip so the mixer actually resamples real audio rather
    // than an empty buffer.
    auto clip = std::make_shared<AudioClip>();
    clip->frameCount = AudioMixSampleRate * 2;
    clip->samples.assign(static_cast<size_t>(clip->frameCount) * AudioMixChannels, 0.1f);

    // Real background load, up toward the 64-voice cap, so isActive()/setVoiceParams()
    // on the low ids do real findVoice() work, not just acquire-and-return.
    constexpr int RealVoices = 64;
    constexpr int SimulatedSources = 1000;
    std::vector<uint32_t> voices(SimulatedSources, 0);
    for (int i = 0; i < RealVoices; i++) {
        voices[static_cast<size_t>(i)] = engine.play(clip, 0.5f, 1.0f, /*loop*/ true);
    }
    // ids [RealVoices, SimulatedSources) stay 0 -- an AudioSourceComponent whose voice
    // never started. AudioSystem::update still calls isActive() on those every step,
    // so the acquisition count matches even though findVoice() returns immediately.

    constexpr int Steps = 300; // 5 simulated seconds at 60 Hz
    const auto stepPeriod = std::chrono::microseconds(16667);

    for (int step = 0; step < Steps; step++) {
        const auto stepStart = std::chrono::steady_clock::now();

        // AudioSystem::update's exact per-source shape: isActive() then, only if
        // active, setVoiceParams() -- two separate lock acquisitions for one
        // logical operation, repeated for every one of the 1 000 sources.
        for (int i = 0; i < SimulatedSources; i++) {
            const uint32_t voice = voices[static_cast<size_t>(i)];
            if (engine.isActive(voice)) {
                const float gain = 0.4f + 0.01f * static_cast<float>(step % 10);
                engine.setVoiceParams(voice, gain, 1.0f);
            }
        }

        // One-shot bursts layered on, the way hit/footstep sfx fire in a real game --
        // exercises play()'s erase/remove_if + possible push_back allocation while
        // the device callback is mixing concurrently.
        if (step % 10 == 0) {
            for (int burst = 0; burst < 8; burst++) {
                engine.play(clip, 0.3f, 1.0f, /*loop*/ false);
            }
        }

        const auto elapsed = std::chrono::steady_clock::now() - stepStart;
        if (elapsed < stepPeriod) {
            std::this_thread::sleep_for(stepPeriod - elapsed);
        }
    }

    const AudioDebugStats stats = engine.debugStats();
    engine.shutdown();
    restoreEnv();

    // The single greppable line: every number DESIGN_AUDIO_THREAD_OWNERSHIP.md's
    // Section 5 decision rule needs, in one place.
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "[audiodbg] callbacks=" << stats.callbacksObserved
              << " overruns=" << stats.overruns
              << " deadline_ms=" << stats.lastDeadlineMs
              << " mix_max_ms=" << stats.maxMixDurationMs
              << " mix_median_ms=" << stats.medianMixDurationMs
              << " arrival_gaps=" << stats.arrivalGaps
              << " lockwait_max_ms=" << stats.maxLockWaitMs
              << " lockwait_max_frac_deadline=" << stats.maxLockWaitFractionOfDeadline
              << " lockwait_hist=[";
    for (int i = 0; i < AudioDebugStats::HistogramBuckets; i++) {
        std::cout << stats.lockWaitHistogram[i];
        if (i + 1 < AudioDebugStats::HistogramBuckets) {
            std::cout << ",";
        }
    }
    std::cout << "] mix_hist=[";
    for (int i = 0; i < AudioDebugStats::HistogramBuckets; i++) {
        std::cout << stats.mixDurationHistogram[i];
        if (i + 1 < AudioDebugStats::HistogramBuckets) {
            std::cout << ",";
        }
    }
    std::cout << "]\n";

    if (stats.overruns > 0) {
        // Section 5's PROMOTES condition. Report plainly; do not design a fix here.
        std::cout << "[stress]   AudioLockContention: " << stats.overruns
                  << " callback overrun(s) observed -- PROMOTES per "
                     "DESIGN_AUDIO_THREAD_OWNERSHIP.md Section 5.\n";
    }

    // The gate here is only "did this actually measure something real" -- the
    // overrun count itself is reported as evidence above, never turned into a
    // pass/fail verdict by this function.
    return stats.callbacksObserved > 0;
}

// Returns {passed, total}. Prints the per-test table as a side effect.
inline std::pair<int, int> run() {
    using TestFn = bool (*)();
    struct Case { const char* name; TestFn fn; };
    const Case cases[] = {
        { "GridVsBruteForce",   gridMatchesBruteForce },
        { "GridEdgeCases",      gridEdgeCases },
        { "GridScale(1200)",    gridScaleWithLargeShape },
        { "NavReplanBackoff",   navReplanBackoff },
        { "PhysicsDeterminism", physicsDeterministic },
        { "PatchStress(30x)",   patchStress },
        { "IdChurn(50x)",       idChurn },
        { "RingChurn(100k)",    ringChurn },
        { "AnimationScale(400)", animationStress },
        { "AudioLockContention", audioLockContention },
    };

    int passed = 0;
    int skipped = 0;
    const int total = static_cast<int>(sizeof(cases) / sizeof(cases[0]));
    for (const Case& test : cases) {
        g_stressCaseSkipped = false; // see AudioLockContention: a case sets this, not PASS/FAIL.
        const auto start = std::chrono::high_resolution_clock::now();
        const bool ok = test.fn();
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - start).count();
        std::string label = test.name;
        while (label.size() < 20) {
            label += '.';
        }
        if (g_stressCaseSkipped) {
            // A SKIP measured nothing -- it must be visibly distinct from PASS in this
            // harness's own output, and it must not count toward the pass/total tally
            // that "ALL PASS" is judged against (DESIGN_AUDIO_THREAD_OWNERSHIP.md Section 6).
            std::cout << "[stress] " << label << " SKIPPED"
                      << " (" << std::fixed << std::setprecision(1) << ms << " ms)\n";
            skipped++;
            continue;
        }
        std::cout << "[stress] " << label << ' ' << (ok ? "PASS" : "FAIL")
                  << " (" << std::fixed << std::setprecision(1) << ms << " ms)\n";
        passed += ok ? 1 : 0;
    }
    const int measured = total - skipped;
    std::cout << "[stress] " << (passed == measured ? "ALL PASS" : "FAILURES PRESENT");
    if (skipped > 0) {
        std::cout << " (" << skipped << " SKIPPED)";
    }
    std::cout << "\n";
    return { passed, measured };
}

} // namespace StressTests
