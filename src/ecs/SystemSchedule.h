#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Phase 13A — opinionated scheduling. Gameplay systems stop being a hardcoded
// call sequence and become *declared* units of work: each carries the set of
// component storages it reads and writes. A SystemScheduler runs them in a
// deterministic order (registration order — determinism is the default, in
// tension with async time-travel) while the declared read/write sets let it
// reason about which systems are provably independent. That analysis is the
// seed of Pillar 5 (run independent systems in parallel) and Pillar 4 (reject
// hidden-coupling patterns): a system that touches a component it never declared
// is a boundary violation the engine can eventually catch.
//
// Lives in Core: it depends only on the *identity* of component storages, never
// on the renderer or any engine service. Concrete systems (physics, audio, the
// script driver) are assembled in the Engine layer, which captures whatever
// services they need in each System's run closure.

// One bit per component storage in Registry. Kept in lockstep with Registry's
// storages; a system declares the ones it touches so the scheduler can tell
// disjoint systems (parallelizable) from conflicting ones (must be ordered).
enum class ComponentType : uint32_t {
    Name = 0,
    Transform,
    Mesh,
    Material,
    Hierarchy,
    Script,
    RigidBody,
    Collider,
    PrefabInstance,
    AudioSource,
    AudioListener,
    Count
};

// A small bitset over ComponentType. 11 components fit comfortably in 32 bits.
using ComponentMask = uint32_t;

inline constexpr ComponentMask componentBit(ComponentType type) {
    return ComponentMask{1} << static_cast<uint32_t>(type);
}

// Fold a pack of ComponentType values into a single mask, e.g.
// maskOf(ComponentType::Transform, ComponentType::RigidBody).
inline ComponentMask maskOf() { return 0; }

template <typename... Rest>
inline ComponentMask maskOf(ComponentType first, Rest... rest) {
    return componentBit(first) | maskOf(rest...);
}

// A unit of gameplay work with its data dependencies made explicit. `run` is a
// closure that performs the work for one fixed step (it captures whatever engine
// services it needs); `reads`/`writes` describe which component storages it
// touches so the scheduler can order and (later) parallelize systems.
struct System {
    std::string name;
    ComponentMask reads = 0;
    ComponentMask writes = 0;
    std::function<void(float dt)> run;
};

// Two systems have a data dependency (cannot run concurrently, must not be
// reordered relative to each other) when one writes a component the other reads
// or writes: write-write, read-write, or write-read hazards.
inline bool systemsConflict(const System& a, const System& b) {
    return (a.writes & b.writes) | (a.writes & b.reads) | (a.reads & b.writes);
}

// Runs a fixed, ordered pipeline of systems each gameplay step. Registration
// order is the deterministic schedule; the declared read/write sets are used to
// analyze independence, not to reorder (reordering independent-but-adjacent work
// is a later, opt-in optimization).
class SystemScheduler {
public:
    void add(System system) {
        systems_.push_back(std::move(system));
    }

    void clear() {
        systems_.clear();
    }

    bool empty() const { return systems_.empty(); }
    std::size_t size() const { return systems_.size(); }
    const std::vector<System>& systems() const { return systems_; }

    // Execute every system once, in deterministic registration order.
    void run(float dt) const {
        for (const System& system : systems_) {
            if (system.run) {
                system.run(dt);
            }
        }
    }

    // Group systems into ordered stages of mutually-independent work: a greedy,
    // order-preserving pass where each system joins the current stage unless it
    // conflicts with a system already in it, in which case a new stage opens.
    // Stages run in order; systems within one stage are provably independent, so
    // they *could* run in parallel. Conservative (a system that conflicts with
    // the running stage always starts a fresh one) but always correct: any two
    // conflicting systems land in different stages with their order preserved.
    // Returns indices into systems(). This is the Pillar 5 foundation; the
    // engine still executes sequentially via run() until parallelism is opted in.
    std::vector<std::vector<std::size_t>> stages() const {
        std::vector<std::vector<std::size_t>> result;
        for (std::size_t i = 0; i < systems_.size(); ++i) {
            bool placed = false;
            if (!result.empty()) {
                bool conflictsWithStage = false;
                for (std::size_t member : result.back()) {
                    if (systemsConflict(systems_[i], systems_[member])) {
                        conflictsWithStage = true;
                        break;
                    }
                }
                if (!conflictsWithStage) {
                    result.back().push_back(i);
                    placed = true;
                }
            }
            if (!placed) {
                result.push_back({i});
            }
        }
        return result;
    }

private:
    std::vector<System> systems_;
};
