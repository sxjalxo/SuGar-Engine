This document should answer three questions for every dependency:

Why are we using it?
What is it allowed to do?
What is it NOT allowed to do?

# Requirements & Scope

This document defines every external library, framework, and major subsystem used by SuGar Engine.

Its purpose is to prevent architectural drift.

Every dependency must have:

- A clearly defined responsibility.
- A clearly defined boundary.
- A reason for existing.
- A defined replacement strategy (if temporary).

No library should expand beyond its intended scope.

---

# Core Philosophy

SuGar Engine follows one principle:

> **Use external libraries for solved problems.**
>
> **Build engine-specific systems ourselves.**

Examples:

- Rendering architecture → Hand-rolled
- ECS → Hand-rolled
- Physics → Hand-rolled
- Scheduler → Hand-rolled
- Serialization → Hand-rolled

Examples of solved problems:

- Window creation
- Vulkan loader
- Image decoding
- Audio backend
- UI rendering

These are delegated to well-established libraries.

---

# Rendering

## Vulkan

### Responsibility

Low-level graphics API.

### Scope

Allowed:

- GPU resource creation
- Rendering
- Synchronization
- Memory management

Not allowed:

- Scene management
- ECS
- Gameplay
- Editor logic

### Binding

The **C API** (`#include <vulkan/vulkan.h>`) is used directly. Vulkan-Hpp is **not
currently adopted** and should not be listed as a dependency until it actually is.

---

## GLFW

### Responsibility

Platform abstraction.

### Scope

Allowed:

- Window creation
- Input events
- Surface creation
- Monitor handling

Not allowed:

- UI
- Scene logic
- Rendering architecture

---

# Editor

## Dear ImGui

### Responsibility

Engine editor.

### Scope

Allowed:

- Hierarchy
- Inspector
- Asset Browser
- Timeline
- Query Console
- Systems Panel
- Play Controls
- Debug windows
- Profiler
- Engine tools

Not allowed:

- Runtime game UI
- Player HUD
- Main menu
- Inventory
- Dialogue UI
- Pause menu

Reason

Immediate-mode UI matches real-time engine editing.

Dear ImGui is considered a permanent editor dependency.

---

## ImGuizmo

### Responsibility

Viewport transform gizmos.

### Scope

Allowed:

- Move
- Rotate
- Scale
- Local/world transform editing

Future

Temporary dependency.

Eventually replaced with a native SuGar gizmo system.

Reason:

- Better quaternion workflow
- Better ECS integration
- Better snapping
- Better Play/Edit support

---

# Runtime UI

## RmlUi

### Responsibility

Player-facing user interface.

### Scope

Allowed:

- HUD
- Main menu
- Inventory
- Dialogue
- Pause menu
- Loading screens
- Settings
- Multiplayer UI

Not allowed:

- Editor
- Engine tools
- Debug windows

Reason

Provides HTML/CSS authoring while remaining renderer-independent.

State ownership (RULES.md Rule 21)

Authoritative UI state — what game logic reads (menu open, selected slot, health
value) — must live in ECS components (or be snapshot-serializable), so the UI is
correct after time travel / hot reload / snapshot restore. RmlUi's derived state —
computed layout, style cache — may be rebuilt and need not be serialized. RmlUi must
never become a second, hidden home for authoritative gameplay state.

## FreeType

### Responsibility

Font rasterization for RmlUi.

### Scope

Allowed:

- Back RmlUi's font engine.
- Rasterize glyphs for runtime UI text.

Not allowed:

- Own UI state.
- Own rendering architecture.
- Enter SuGarCore.

Reason

RmlUi requires a font engine during initialisation. FreeType is solved
infrastructure, kept behind RmlUi in the engine layer, and static-linked with the
player-facing UI scaffold.

---

# Asset Pipeline

## tinygltf

### Responsibility

glTF parsing only.

### Scope

Allowed:

- Read glTF/glb files
- Parse scene data
- Parse materials
- Parse animations

Not allowed:

- Runtime ownership
- Rendering
- Scene representation

Requirement

All parsed data must be copied into SuGar Engine types.

tinygltf objects never survive loading. Concretely (Phase 17B): animation channels
and samplers become `AnimationClip` / `TransformTrack` inside `GltfLoader.cpp`, and
glTF node *indices* are resolved to node *names* on the way out — no tinygltf type,
and no glTF numbering, appears in any header or anywhere else in the engine.

---

## stb_image

### Responsibility

Image decoding.

### Scope

Allowed:

- Decode PNG
- Decode JPG
- Decode HDR

Not allowed:

- Texture ownership
- GPU upload
- Asset management

---

# Audio

## miniaudio

### Responsibility

Audio device backend.

### Scope

Allowed:

- Audio device
- File decoding

Not allowed:

- Audio mixing
- Voice management
- ECS integration
- Spatial audio logic

Reason

SuGar owns the mixer.

miniaudio only feeds samples to the OS.

---

# Mathematics

## GLM

### Responsibility

Math library.

### Scope

Allowed:

- Vector math
- Matrix math
- Quaternions
- Transform decomposition

Not allowed:

- Engine logic

---

# Animation

## Animation System

### Responsibility

Runtime animation playback.

### Scope

Status

**Implemented** (Phase 17, complete). The model layer (17A): clip/track data,
keyframe sampling, `AnimationClipRegistry`, `AnimationPlayerComponent`, and the
fixed-step `AnimationSystem` — all in Core, all headless-tested. glTF clip + skin
import (17B, 17C.1) is implemented in `GltfLoader.cpp`. Skinning's CPU side (17C.1)
is `Skin` + `SkinRegistry` + `Skinning::computeJointMatrices`, also in Core: the ECS
hierarchy *is* the skeleton (joints are entities), so joint matrices are derived and
the renderer is a pure consumer. GPU skinning (17C.2) adds skinned scene/shadow
pipelines in the Engine layer, fed poses through the `DrawList`. Still to come: blend
trees / state machines (17D).

Implementation

Hand-rolled — animation remains an engine subsystem.

External libraries may import animation data but never own playback. Concretely:
tinygltf parses keyframes and they immediately become SuGar types; sampling,
interpolation, blending, and graph evaluation are the engine's own code.

Layering

Clip data, sampling, playback state, and the animation system live in **SuGarCore** —
playback is pure math over plain data, so it needs no GPU and stays headless-testable
(Rule 9, Rule 15). glTF import and skinning live in the Engine layer.

Future

Support:

- Skeletal animation
- Blend trees
- State machines
- Animation graphs

Playback, blending, graph evaluation, and runtime state remain owned by SuGar Engine.

State ownership (RULES.md Rule 21)

Authoritative playback state — current time, active state, playback speed — must live
in ECS components (or be snapshot-serializable), so animation survives time travel,
hot reload, and snapshot restore. Derived data — evaluated poses, graph caches — may
be rebuilt each frame and need not be serialized.

The full classification (including the gray areas: why a transition mid-blend is
authoritative, why blend weights are not, why animation events need explicit
already-fired state) is the architecture record: **[docs/DESIGN_ANIMATION.md](docs/DESIGN_ANIMATION.md)**.

---

# ECS

## SuGar ECS

### Responsibility

Authoritative runtime world.

### Scope

Owns:

- Entities
- Components
- Hierarchy
- Systems

Never replaced by an external ECS library.

---

# Physics

## SuGar Physics

### Responsibility

Physics simulation.

### Scope

Owns:

- Collision
- Rigid bodies
- Solver
- Events

No Bullet.

No PhysX.

No Jolt.

Physics remains hand-rolled.

---

# Serialization

## SuGar Serializer

### Responsibility

Scene persistence.

### Scope

Owns:

- Scene files
- Prefabs
- Snapshots
- Time-travel data

Never tied to rendering.

---

# Scheduler

## SuGar Scheduler

### Responsibility

System execution.

### Scope

Owns:

- Dependency analysis
- Access validation
- Scheduling
- Future parallel execution

Never delegated to a job framework.

---

# Runtime Code

## SuGarCore

### Responsibility

Engine-independent runtime.

Owns:

- ECS
- Components
- Scheduler
- Behaviors
- Math
- Input
- Core data structures

Must remain independent of:

- Vulkan
- Rendering
- Editor

---

## SuGarGame

### Responsibility

Game-specific behaviors.

Scope

Contains:

- Gameplay scripts
- Behaviors
- Game rules

Must never depend on:

- Renderer
- Editor

Only depends on:

SuGarCore

---

# Developer Tooling

## Self-Test Framework

Responsibility

Confidence testing.

Every major subsystem should have a deterministic headless self-test.

---

## Time Travel

Responsibility

Runtime debugging.

Must always preserve:

- Editor state
- Entity identity
- Determinism

---

## Hot Reload

Responsibility

Native gameplay code reload.

Scope

Reloads:

- Game behaviors
- Script registrations

Does not reload:

- Renderer
- Core
- Editor

---

# General Dependency Rules

Before adding any dependency, ask:

1. Is this solving a problem that is already solved well?
2. Does it replace engine architecture?
3. Does it increase developer iteration speed?
4. Can it remain isolated behind a clear boundary?

If the answer to (2) is yes, the dependency should usually not be added.

---

# Long-Term Vision

SuGar Engine should remain:

- Hand-rolled where architecture matters.
- Library-powered where infrastructure is already solved.
- Focused on developer productivity.
- Easy to understand.
- Easy to extend.
- Easy to debug.

# Explicit Non-Goals

The following are intentionally not part of SuGar Engine.

- Qt for the runtime editor.
- Bullet Physics.
- PhysX.
- EnTT ECS.
- BGFX.
- Ogre3D.
- irrKlang.
- FMOD (unless professional licensing becomes necessary).
- Mono/C# gameplay scripting.
- Unreal-style reflection macros.

These technologies solve different problems or replace engine systems that SuGar intentionally owns.