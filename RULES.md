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

## Rule 9a — A test must be shown to fail

A green test proves nothing until you have seen it go red for the intended reason.

Before trusting a new test, break the thing it covers — temporarily — and confirm the
test fails, and fails *because of that*:

```
neuter AnimationSystem::update  → Animation test FAIL   ✔ measures behavior
delete a field from the writer  → Serializer test FAIL  ✔ measures behavior
```

A test that stays green through a deliberate break measures nothing; it merely
compiles next to the code. This costs one build cycle and is the difference between a
suite you trust and a suite you hope.

The failure this catches is usually the *test*, not the code. Worked example (17C.1):
reversing the skinning multiplication order left the `Skinning` test **passing** —
every case used translation-only matrices, and translations **commute**, so
`world * inverseBind` and `inverseBind * world` are identical. The test could not see
the one thing it existed to pin. Rotating a joint (rotation does not commute with
translation) gave it teeth.

The general shape: **a test can be blind to the property it claims to check, and
being green tells you nothing about which.** Only the break tells you.

## Rule 9b — Round-trip tests are necessary, not sufficient

A round trip proves the writer and the reader **agree with each other**. It does not
prove either is *right* — both can drift together and stay mutually compatible:

```
write → read → compare      ✔ writer and parser agree
                            ✘ says nothing about the format itself
```

Where an external contract exists — an on-disk format, a wire protocol, a file
snapshots and time travel ride on — pin it with a **golden test**: exact expected
bytes, derived **independently of the implementation under test**.

Deriving the expectation by running the new code and capturing its output proves only
that the code equals itself:

```
new impl → generate expected → compare against itself    ✘ circular
old rules → hand-derive expected → compare              ✔ real evidence
```

Golden tests are deliberately brittle. That is the feature: the format then changes
only on purpose, in a commit that says so.

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

## Rule 21a — A name in a component is a promise to reconstitute the asset

Asset-backed components store a **stable identifier**, never a pointer: a mesh key, a
clip name, a skin name, a behavior name, a graph name. That is what makes them
serializable, hot-reloadable, and safe across a snapshot.

The identifier is only half the contract. The other half:

> If authoritative state references an asset by name, **something must deterministically
> reconstruct that asset from the name alone — on scene load**, not only on the path
> that happened to create it.

Miss the second half and the failure is *silent*. Not a crash, not a dangling pointer:
the component round-trips perfectly, looks correct in the inspector, and does nothing.

```
save   → "clip": "hero.gltf#Run"      ✔ serialized
load   → "clip": "hero.gltf#Run"      ✔ deserialized
play   → registry lookup misses       ✘ animation silently dead
```

This is exactly what happened in 17C.2: clips and skins were registered as a side
effect of *import*, and a scene loaded from disk never runs the importer
(`ModelImporter::ensureModelAssets` is the fix). It generalizes to every asset-backed
component — materials, audio clips, fonts, meshes, behaviors, animation graphs. When
adding one, ask: *what rebuilds this from the name when the scene is loaded cold?*

## Rule 21b — A cache is derived only if it is a function of the *present*

The authoritative/derived split is not decided by how expensive a value is to
recompute. It is decided by **whether recomputing it needs information that still
exists**.

> A value computed once from a *past* state is a function of **history**, not of the
> current state — and history is authoritative.

The trap is that history-dependent values look exactly like caches. Navigation found
the sharpest case (Phase 18A). A path *appears* to be `f(navmesh, position, goal)`, so
"recompute it after a restore" sounds right — until an agent stands at a corridor fork:

```
run A:  planned at t=0 from P0 → took the left corridor → now halfway down it
run B:  restored at t=5, replans from that same point → takes the right corridor
```

Both routes are optimal, both are legal, the planner is deterministic — and the two
runs have diverged. The path was a function of where the agent stood **when it
planned**, and the present cannot reconstruct that.

Apply the test to the candidate, not to its shape:

```
Pose         = f(clip, time)            <- present      -> derived
Blend weight = f(parameters)            <- present      -> derived
Path         = f(navmesh, start, goal)  <- *past* start -> authoritative
Transition   = f(start time, now)       <- *past* start -> authoritative
"already attempted" / "already fired"   <- past event   -> authoritative
```

The rule earned its place by arriving three times, from three subsystems, with three
different-looking symptoms — which is why it is stated as history rather than as
"remember to serialize this":

| State | What it actually encodes |
|---|---|
| `AnimationStateComponent::transitionElapsed` | progress through a transition that *started* at some past moment |
| Which animation events have fired | past occurrences |
| `NavAgentComponent::path` + `status` | a planning decision *made* at a past position |

The common thread is **not serialization**. Serialization is the remedy; history is
the diagnosis. Stated as "serialize paths" the rule teaches nothing transferable and
has to be rediscovered per subsystem. Stated as history, the next case is answerable
before it is written:

> **If recomputing a value from the present can legitimately produce a different valid
> answer, the original value is part of the simulation state.**

Note "legitimately" and "valid". This is not about nondeterminism or floating-point
drift — the recomputation may be perfectly deterministic and its answer perfectly
correct. It is about the answer being *a different correct one*, because the input it
was originally computed from is gone.

This is why [docs/DESIGN_ANIMATION.md](docs/DESIGN_ANIMATION.md) makes an animation
transition authoritative while the identical-looking UI tween is derived, why animation
events need explicit "already fired" state, and why a navigation agent's `status` must
record that a plan was *attempted* — otherwise a stuck agent re-plans forever and a
restore silently restarts the attempt. One rule, discovered three times.

**The failure mode is why this is a rule and not a note.** Getting it wrong does not
crash, dangle, or corrupt: every value stays individually valid and plausible, and the
system looks like it works. Only scrubbing to the same frame twice and comparing shows
the divergence — which is exactly the guarantee snapshots and time travel exist to
provide, silently withdrawn.

---

# Decision Checklist

Before merging any major feature, ask:

- Does it make developers faster?
- Does it improve the architecture?
- Does it reduce complexity?
- Is it testable — and has its test been shown to fail (Rule 9a)?
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