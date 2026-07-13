# RULES.md

# SuGar Engine Development Rules

This document defines the architectural rules of SuGar Engine.

Unlike the roadmap, these rules are **not goals**.

They are constraints.

Every new subsystem, feature, dependency, or refactor must obey these rules unless there is a very strong architectural reason not to.

---

# Rule 1 — Developer Iteration Comes First

The primary objective of SuGar Engine is to maximize developer iteration speed.

Before implementing any feature, ask:

> **Does this make developers faster?**

Examples:

✔ Hot Reload

✔ Time Travel

✔ Better Editor

✔ Better Debugging

✔ Better Profiling

✔ Better Error Messages

If the answer is "no", reconsider whether the feature belongs now.

---

# Rule 2 — Architecture Before Features

Never sacrifice architecture for short-term convenience.

If implementing a feature requires violating engine architecture, redesign the implementation instead.

Temporary hacks should be treated as technical debt with a documented removal plan.

---

# Rule 3 — Engine Systems Are Hand-Rolled

Core engine systems are intentionally built in-house.

This includes:

- ECS
- Physics
- Animation
- Scheduler
- Serialization
- Resource Management
- Hot Reload
- Scene Management

External libraries may support these systems but must never replace them.

---

# Rule 4 — External Libraries Solve Infrastructure

Use external libraries only for solved infrastructure problems.

Examples:

✔ Window creation

✔ Image decoding

✔ Audio devices

✔ glTF parsing

✔ Mathematics

Do NOT use libraries that replace SuGar Engine's architecture.

---

# Rule 5 — Clear Ownership

Every system has one owner.

Examples:

Renderer owns rendering.

Physics owns simulation.

Animation owns playback.

Audio owns audio.

Editor owns editor UI.

Avoid shared ownership between unrelated systems.

---

# Rule 6 — Data Owns State

Runtime state belongs inside ECS components.

Behaviors should remain stateless whenever practical.

Systems operate on data.

Systems should not secretly own gameplay state.

---

# Rule 7 — Prefer Better Invariants Over More Code

When solving a problem:

Prefer changing the architecture so the problem cannot exist.

Instead of:

Fixing bugs repeatedly.

Prefer:

Removing the entire class of bugs.

Examples:

Entity remapping removed through stable IDs.

Snapshot rebuild replaced with patching.

---

# Rule 8 — Delete Complexity Whenever Possible

Removing unnecessary systems is considered progress.

Good refactors often reduce total code size.

Deleting obsolete abstractions is encouraged.

---

# Rule 9 — Every Major System Must Be Testable

Each subsystem should have deterministic headless tests.

Self-tests should verify behavior, not implementation.

Prefer testing invariants.

Examples:

✔ Scheduler

✔ Physics

✔ Serializer

✔ Registry

✔ Snapshot Storage

✔ Command History

---

# Rule 10 — Determinism Is Required

Gameplay systems must behave deterministically.

This enables:

- Replay
- Time Travel
- Snapshot Restore
- Reliable Debugging

Avoid unnecessary nondeterminism.

---

# Rule 11 — Editor And Runtime Are Different Products

The editor exists to help developers.

The runtime exists for players.

Do not mix them.

Editor UI:

Dear ImGui

Runtime UI:

RmlUi

The runtime must never depend on Dear ImGui.

---

# Rule 12 — Runtime State Must Survive Tooling

Developer tools should preserve runtime state whenever possible.

Examples:

✔ Hot Reload

✔ Snapshot Restore

✔ Time Travel

✔ Play/Edit

Avoid destroying and rebuilding state unless absolutely necessary.

---

# Rule 13 — Prefer Explicit Over Implicit

Systems should declare:

- Dependencies
- Ownership
- Access
- Lifetime

Avoid hidden behavior.

Examples:

Explicit scheduler masks.

Explicit behavior registration.

Explicit serialization.

---

# Rule 14 — One Responsibility Per System

Each subsystem should solve one problem well.

Avoid "god objects."

Large systems should be decomposed into smaller focused modules.

---

# Rule 15 — Core Must Stay Independent

SuGarCore must never depend on:

- Vulkan
- Dear ImGui
- Renderer
- Editor
- Platform UI

Everything inside Core should compile without graphics.

---

# Rule 16 — Gameplay Lives In The Game Module

Engine code provides systems.

Game code provides behaviors.

The engine should never contain project-specific gameplay.

---

# Rule 17 — Features Should Compose

New systems should strengthen existing ones.

Examples:

Hot Reload works with:

- Time Travel
- Snapshots
- ECS
- Behaviors

Avoid isolated features that cannot integrate with the rest of the engine.

---

# Rule 18 — Measure Before Optimizing

Do not optimize because something "might" be slow.

Measure first.

Then optimize.

Examples:

Parallel scheduler.

Binary snapshots.

Delta snapshots.

Only optimize after identifying a real bottleneck.

---

# Rule 19 — Documentation Is Part Of The Feature

A feature is not complete until:

- README is updated.
- ROADMAP is updated.
- Requirements are updated (if needed).
- Rules are updated (if needed).

Documentation should describe architecture, not just usage.

---

# Rule 20 — Leave The Engine Better Than You Found It

Every change should improve at least one of:

- Simplicity
- Maintainability
- Performance
- Debuggability
- Testability
- Developer Productivity

Avoid changes that merely increase feature count.

---

# Rule 21 — Runtime Systems Must Not Own Hidden Authoritative State

Any runtime state that affects gameplay, determinism, replay, hot reload, or time
travel must either:

- live in ECS components, **or**
- be **fully reconstructible from serialized ECS state**.

The distinction is **authoritative state vs derived state**:

- **Authoritative state** — what gameplay logic reads and acts on. Must survive
  snapshots / time travel / hot reload. Belongs in ECS (or must be serializable).
- **Derived state** — caches recomputed from authoritative state (e.g. an animation
  graph cache, an RmlUi layout cache). May be rebuilt freely; need not be serialized.

This protects the guarantees already built — snapshots, replay, hot reload, stable
IDs, time travel. A runtime system that hides authoritative state in private fields
reintroduces exactly the class of bug those systems eliminated:

```
Animator { float currentTime; }   // hidden authoritative state
        ↓ snapshot → restore
animation jumps                    // regression, hard to reason about later
```

Applies to **everything**: animation, runtime UI, particles, navigation, AI. When
adding a runtime subsystem, first classify its state as authoritative or derived, and
route authoritative state through ECS / serialization.

---

# Decision Checklist

Before merging any major feature, ask:

- Does it make developers faster?
- Does it improve the architecture?
- Does it reduce complexity?
- Is it testable?
- Is it deterministic?
- Does it fit the existing philosophy?
- Does it introduce unnecessary dependencies?
- Is documentation updated?
- Can future contributors understand it?

If multiple answers are "no", reconsider the design.

---

# Final Principle

SuGar Engine is not trying to become the largest engine.

It is trying to become one of the cleanest, easiest-to-extend, and fastest-to-iterate modern C++ game engines.

Every architectural decision should move the engine closer to that goal.