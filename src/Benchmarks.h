#pragma once

// Phase 14C — measure, don't assume. After 14A/14B removed a pile of restore/remap
// complexity there is finally a stable baseline to profile, so we can answer
// "do we actually need binary/delta snapshots?" (and where else time goes) with
// numbers instead of guesses. Opt in with SUGAR_BENCH=1; runs headless (no Vulkan)
// so it needs a scene of device-free components — transforms, rigid bodies,
// colliders, scripts, hierarchy — which is representative of the gameplay state
// that actually dominates snapshot size and per-step cost. Resource handles
// (mesh/material/audio) serialize as short keys, so they'd add little to the
// numbers that matter here.
//
// Scene size defaults to 500 entities; override with SUGAR_BENCH_ENTITIES to probe
// scaling. Hot-reload latency isn't here — it needs a live DLL swap; that path is
// instrumented directly (see reloadGameModule's "ms swap" log).

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "core/SnapshotStorage.h"
#include "ecs/Registry.h"
#include "ecs/SystemSchedule.h"
#include "editor/EntityQuery.h"
#include "physics/PhysicsWorld.h"
#include "scene/BehaviorRegistry.h"
#include "scene/Light.h"
#include "scene/SceneSerializer.h"

namespace Benchmarks {

using Clock = std::chrono::high_resolution_clock;

inline double millisSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Median-of-repeats keeps a stray scheduling hiccup from skewing a fast op. Runs
// `fn` `iterations` times and returns the best (min) elapsed ms — closest to the
// true cost with the least noise.
template <typename Fn>
inline double timeBest(int iterations, Fn&& fn) {
    double best = 1e30;
    for (int i = 0; i < iterations; ++i) {
        const auto start = Clock::now();
        fn();
        const double ms = millisSince(start);
        if (ms < best) {
            best = ms;
        }
    }
    return best;
}

inline void printRow(const std::string& name, const std::string& value) {
    std::string label = name;
    while (label.size() < 26) {
        label += '.';
    }
    std::cout << "[bench] " << label << ' ' << value << "\n";
}

// A representative gameplay scene: a flat field of dynamic bodies (transform +
// rigid body + box collider + script), a fraction of them parented, all with
// names. Mirrors what a mid-size scene's snapshot actually contains.
inline void buildScene(Registry& registry, int entityCount) {
    std::vector<Entity> roots;
    for (int i = 0; i < entityCount; ++i) {
        const Entity e = registry.createEntity();
        registry.names.add(e, { "Entity_" + std::to_string(i) });

        Transform t;
        t.position = glm::vec3(static_cast<float>(i % 32), 1.0f, static_cast<float>(i / 32));
        registry.transforms.add(e, { t });
        registry.hierarchy.add(e, {});

        RigidBodyComponent body{};
        body.velocity = glm::vec3(0.0f, -static_cast<float>(i % 5), 0.0f);
        body.mass = 1.0f + static_cast<float>(i % 3);
        registry.rigidBodies.add(e, body);

        ColliderComponent collider{};
        collider.type = ColliderType::Box;
        collider.halfExtents = glm::vec3(0.5f);
        registry.colliders.add(e, collider);

        registry.scripts.add(e, { "Spinner", false });

        // Parent every 4th entity to a prior one to exercise the hierarchy.
        if (i % 4 == 0) {
            roots.push_back(e);
        } else if (!roots.empty()) {
            registry.setParent(e, roots.back());
        }
    }
}

inline void run() {
    int entityCount = 500;
    if (const char* override = std::getenv("SUGAR_BENCH_ENTITIES")) {
        const int parsed = std::atoi(override);
        if (parsed > 0) {
            entityCount = parsed;
        }
    }

    Registry registry;
    std::vector<Light> lights;
    buildScene(registry, entityCount);

    std::cout << "[bench] scene: " << entityCount << " entities "
              << "(transform + rigidbody + collider + script + hierarchy)\n";

    // --- Snapshot size: the number that decides the binary/delta question -------
    const std::string oneSnapshot = SceneSerializer::saveToString(registry, lights);
    const double snapshotKiB = static_cast<double>(oneSnapshot.size()) / 1024.0;
    printRow("snapshot size (1 frame)",
             std::to_string(snapshotKiB).substr(0, 6) + " KiB (" +
             std::to_string(oneSnapshot.size() / std::max(1, entityCount)) + " B/entity)");

    // The Play-mode ring holds 600 frames (~10 s at 60 Hz). Fill it and measure
    // actual retained bytes — this is the "18 MB or 600 MB?" answer.
    constexpr int RingCapacity = 600;
    JsonSnapshotStorage ring(RingCapacity);
    for (int i = 0; i < RingCapacity; ++i) {
        ring.push(SceneSerializer::saveToString(registry, lights));
    }
    uint64_t ringBytes = 0;
    for (int i = 0; i < ring.count(); ++i) {
        ringBytes += ring.get(i).size();
    }
    const double ringMiB = static_cast<double>(ringBytes) / (1024.0 * 1024.0);
    printRow("snapshot ring (600 frames)", std::to_string(ringMiB).substr(0, 6) + " MiB total");

    // --- Timings ----------------------------------------------------------------
    // A sink the results feed into, so the compiler can't elide the measured work.
    uint64_t sink = 0;

    const double saveMs = timeBest(50, [&] {
        sink += SceneSerializer::saveToString(registry, lights).size();
    });
    printRow("snapshot save", std::to_string(saveMs).substr(0, 6) + " ms");

    // In-place restore (Phase 14A) over the whole scene.
    const double patchMs = timeBest(50, [&] {
        sink += SceneSerializer::patchFromString(registry, lights, oneSnapshot) ? 1u : 0u;
    });
    printRow("patch restore (14A)", std::to_string(patchMs).substr(0, 6) + " ms");

    // Query execution over the authoritative ECS.
    const double queryMs = timeBest(200, [&] {
        sink += EntityQuery::run(registry, "rigidbody where vel.y < 0").entities.size();
    });
    printRow("query (rigidbody where...)", std::to_string(queryMs).substr(0, 6) + " ms");

    // Physics step — the real per-frame hot system (all-pairs broadphase, so this
    // also shows the O(n^2) the roadmap flagged for a future uniform grid).
    PhysicsWorld physics;
    const double physicsMs = timeBest(50, [&] {
        physics.step(registry, 1.0f / 60.0f);
    });
    printRow("physics step", std::to_string(physicsMs).substr(0, 6) + " ms");

    // Scheduler overhead: run four no-op systems and compare enforcement off vs on,
    // isolating the per-step cost the scheduler itself adds (tracker scopes under
    // SUGAR_ACCESS_TRACKING). The gameplay work above is separate.
    SystemScheduler scheduler;
    for (int i = 0; i < 4; ++i) {
        scheduler.add(System{ "noop" + std::to_string(i), 0, 0, [](float) {} });
    }
    scheduler.setEnforcement(AccessEnforcement::Off);
    const double schedOff = timeBest(1000, [&] { scheduler.run(1.0f / 60.0f); });
    scheduler.setEnforcement(AccessEnforcement::Warn);
    const double schedOn = timeBest(1000, [&] { scheduler.run(1.0f / 60.0f); });
    printRow("scheduler run (enforce off)", std::to_string(schedOff * 1000.0).substr(0, 6) + " us");
    printRow("scheduler run (enforce on)", std::to_string(schedOn * 1000.0).substr(0, 6) + " us" +
             (ComponentAccess::trackingEnabled() ? "" : " (tracking compiled out)"));

    std::cout << "[bench] hot-reload swap latency: run the editor + F8 (see "
                 "'[GameModule] hot reload complete (N ms swap)')\n";
    std::cout << "[bench] done — decide binary/delta snapshots from the 600-frame MiB above"
              << (sink == 0xFFFFFFFFFFFFFFFFull ? " ." : "") << "\n"; // keep `sink` live
}

} // namespace Benchmarks
