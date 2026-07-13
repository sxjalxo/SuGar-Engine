#pragma once

#include <cstddef>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ecs/ComponentAccess.h"

// Phase 13A — opinionated scheduling. Gameplay systems stop being a hardcoded
// call sequence and become *declared* units of work: each carries the set of
// component storages it reads and writes. A SystemScheduler runs them in a
// deterministic order (registration order — determinism is the default, in
// tension with async time-travel) while the declared read/write sets let it
// reason about which systems are provably independent. That analysis is the
// seed of Pillar 5 (run independent systems in parallel).
//
// Phase 13B — those declarations are now *enforced* (Pillar 4). With enforcement
// on, each system runs inside a ComponentAccessTracker scope and the scheduler
// reports any storage it touched but never declared, or mutated while declaring
// read-only. Hidden coupling becomes a message instead of a mystery, and the
// masks become trustworthy enough to schedule against.
//
// Lives in Core: it depends only on the *identity* of component storages, never
// on the renderer or any engine service. Concrete systems (physics, audio, the
// script driver) are assembled in the Engine layer, which captures whatever
// services they need in each System's run closure.

// A unit of gameplay work with its data dependencies made explicit. `run` is a
// closure that performs the work for one fixed step (it captures whatever engine
// services it needs); `reads`/`writes` describe which component storages it
// touches so the scheduler can order, verify, and (later) parallelize systems.
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

// What a system did that its declaration didn't allow.
struct AccessViolation {
    std::string system;
    ComponentMask undeclaredAccess = 0; // touched, but in neither reads nor writes
    ComponentMask undeclaredWrites = 0; // mutated, but not in writes
};

// Off in release and by default. Warn reports violations without halting the sim,
// which is what you want mid-session (a wrong declaration shouldn't kill the game
// you're debugging) — it's the editor default. Strict throws on the first
// violation: fail-fast for headless/CI runs where an undeclared access is a bug
// to catch, not tolerate.
enum class AccessEnforcement {
    Off,
    Warn,
    Strict
};

// Thrown by Strict enforcement. Carries the offending system + storages.
struct AccessViolationError : std::logic_error {
    explicit AccessViolationError(const std::string& message) : std::logic_error(message) {}
};

// Runs a fixed, ordered pipeline of systems each gameplay step. Registration
// order is the deterministic schedule; the declared read/write sets are used to
// verify and analyze, not to reorder (reordering independent-but-adjacent work
// is a later, opt-in optimization).
class SystemScheduler {
public:
    void add(System system) {
        systems_.push_back(std::move(system));
        reported_.emplace_back();
    }

    void clear() {
        systems_.clear();
        reported_.clear();
    }

    bool empty() const { return systems_.empty(); }
    std::size_t size() const { return systems_.size(); }
    const std::vector<System>& systems() const { return systems_; }

    // Verify each system's declared access as it runs. Only observable in builds
    // with SUGAR_ACCESS_TRACKING (Debug); elsewhere this is inert by design, so
    // the release hot path keeps its original codegen.
    void setEnforcement(AccessEnforcement mode) { enforcement_ = mode; }
    AccessEnforcement enforcement() const { return enforcement_; }

    // Replaces the default stderr report. Each distinct violation is delivered
    // once per system (a 60 Hz sim would otherwise drown the log).
    void setViolationHandler(std::function<void(const AccessViolation&)> handler) {
        violationHandler_ = std::move(handler);
    }

    // Recent distinct violations, newest last (bounded). The editor Systems panel
    // reads this so guard-rail breaches show in-editor, not only on stderr.
    const std::vector<AccessViolation>& violationLog() const { return violationLog_; }
    void clearViolationLog() { violationLog_.clear(); }

    // Execute every system once, in deterministic registration order.
    void run(float dt) {
        const bool verify = enforcement_ != AccessEnforcement::Off && ComponentAccess::trackingEnabled();

        for (std::size_t i = 0; i < systems_.size(); ++i) {
            System& system = systems_[i];
            if (!system.run) {
                continue;
            }

            if (!verify) {
                system.run(dt);
                continue;
            }

            ComponentAccessTracker tracker;
            {
                ComponentAccess::Scope scope(&tracker);
                system.run(dt);
            }

            AccessViolation violation;
            violation.system = system.name;
            violation.undeclaredAccess = tracker.touched() & ~(system.reads | system.writes);
            violation.undeclaredWrites = tracker.mutated() & ~system.writes;
            if (violation.undeclaredAccess != 0 || violation.undeclaredWrites != 0) {
                report(i, violation);
                if (enforcement_ == AccessEnforcement::Strict) {
                    std::string message = "system '" + violation.system + "' violated declared access";
                    if (violation.undeclaredAccess != 0) {
                        message += "; touched undeclared: " + describeComponentMask(violation.undeclaredAccess);
                    }
                    if (violation.undeclaredWrites != 0) {
                        message += "; mutated read-only: " + describeComponentMask(violation.undeclaredWrites);
                    }
                    throw AccessViolationError(message);
                }
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
    // Last violation signature seen per system, so a repeating one is reported
    // once rather than every fixed step.
    struct ReportedViolation {
        ComponentMask access = 0;
        ComponentMask writes = 0;
        bool seen = false;
    };

    void report(std::size_t index, const AccessViolation& violation) {
        ReportedViolation& last = reported_[index];
        if (last.seen && last.access == violation.undeclaredAccess && last.writes == violation.undeclaredWrites) {
            return;
        }
        last = { violation.undeclaredAccess, violation.undeclaredWrites, true };

        constexpr std::size_t maxLog = 32;
        if (violationLog_.size() >= maxLog) {
            violationLog_.erase(violationLog_.begin());
        }
        violationLog_.push_back(violation);

        if (violationHandler_) {
            violationHandler_(violation);
            return;
        }

        if (violation.undeclaredAccess != 0) {
            std::cerr << "[access] system '" << violation.system << "' touched undeclared components: "
                      << describeComponentMask(violation.undeclaredAccess) << "\n";
        }
        if (violation.undeclaredWrites != 0) {
            std::cerr << "[access] system '" << violation.system << "' mutated components declared read-only: "
                      << describeComponentMask(violation.undeclaredWrites) << "\n";
        }
    }

    std::vector<System> systems_;
    std::vector<ReportedViolation> reported_;
    std::vector<AccessViolation> violationLog_;
    AccessEnforcement enforcement_ = AccessEnforcement::Off;
    std::function<void(const AccessViolation&)> violationHandler_;
};
