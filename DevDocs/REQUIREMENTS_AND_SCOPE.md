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

## Lighting

### Responsibility

Which lights reach a surface, and how a game controls them.

### Scope

Owns:

- `LightComponent` on an entity — `Directional` (sun/moon), `Point` (torch, with a range),
  `Ambient` (sky term). **Position and direction are derived from the entity's world
  transform**, never authored on the component (Rule 21b; the `CameraComponent` precedent).
- The scene-level `lights` array, unchanged and still supported; both forms reduce to one
  render-list `Light`.
- Per-frame selection of at most `MAX_LIGHTS` (8): the shadow-casting directional light
  first, then the point lights nearest the camera. Derived, not state.
- Range-limited falloff (`clamp(1 − d/range)²`); `range 0` means unlimited, which is what
  every light authored before this seam expected.

Not owned (not forced by any game yet):

- More than one shadow caster, shadow cascades, per-light bias
- Clustered / deferred lighting, light cookies, area lights, baked GI

See `DevDocs/DESIGN_LIGHTING.md`.

---

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

Rendering boundary (SuGar owns Vulkan)

RmlUi is a **consumer** of SuGar's renderer, never a driver of it. It creates no Vulkan
device, swapchain or render loop; it calls a hand-written `Rml::RenderInterface`
(`RmlVulkanRenderer`) that SuGar owns entirely. That interface implements RmlUi 6's effect
path — offscreen colour layers, a fullscreen Gaussian-blur composite, and save-layer-as-texture
(so `box-shadow`/`blur` work) — as SuGar-owned Vulkan passes. The runtime UI renders in its
**own render pass after the scene pass**, so the compositor can open offscreen passes a live
scene pass would forbid. Effects are added only when a game forces one (Rule 22 seam, Rule 8
scope): stencil clip masks are a documented, not-yet-built gap.

State ownership (RULES.md Rule 21)

Authoritative UI state — what game logic reads (menu open, selected slot, health
value) — must live in ECS components (or be snapshot-serializable), so the UI is
correct after time travel / hot reload / snapshot restore. RmlUi's derived state —
computed layout, style cache — may be rebuilt and need not be serialized. RmlUi must
never become a second, hidden home for authoritative gameplay state.

The ECS→document channels are deliberately few, and each is a *projection*, not markup:

- `UILabelComponent{element, text}` — what an element **says**.
- `UIElementStateComponent{element, classes, style}` — how it currently **looks**: the
  classes it carries and inline declarations for continuous values (a health bar's width).
  The view syncs an element to exactly the requested set, removing what it applied before,
  and never touches classes written in the document. Forced by M4 L3's hotbar / health bar
  / inventory panel; the alternative was gameplay code generating RML strings, which drags
  styling out of the RCSS and back into C++.

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

## Engine-owned (Phase 19)

Asset *identity*, *settings* and *staleness* are engine logic, not library territory —
see `DevDocs/DESIGN_ASSET_PIPELINE.md`.

- `AssetPath` — the identity function. Normalizing a path into an asset key happens in
  exactly one place, because every scene, prefab and save file on disk already contains
  its output. Changing it is a migration, not a refactor.
- `AssetHash` — content hashing (FNV-1a) plus `CookerVersion`. Any change to a cooked
  format, to what is hashed, or to the hash algorithm bumps that one counter.
- `AssetMeta` — `.meta` import-settings sidecars. Committed source, not cache;
  deterministic bytes (fixed key order, `\n` endings, no timestamps) because the cook
  key hashes them.
- `AssetDatabase` — the catalog: what exists, its settings, its cook key, its
  dependency edges, and the problems worth reporting (non-ASCII names, case-only key
  collisions, malformed `.meta`). Owns no loaded resource and no GPU object — that is
  `ResourceManager`. It *owns* dependency metadata but never *discovers* it: only the
  cooker can parse a source format, so the cooker reports edges here.

- `CookedAsset` — the `.sgc` container and the cooked payload layouts (mesh, texture,
  audio). Explicit little-endian scalars, never struct dumps. The reader trusts the
  payload length, not the header's claimed counts: element counts and `width·height` are
  validated against — or derived from — the actual payload size before any allocation, so a
  truncated or tampered container fails cleanly instead of driving a huge `reserve()`
  (throws → terminates) or overflowing a size calc before GPU upload (2026-07-28 hardening).
- `AssetCooker` — source formats to cooked artifacts, and the `build/assetcache`
  directory. The **only** place glTF/OBJ/image/audio decoding happens; `ResourceManager`
  reads cooked artifacts and nothing else. Run headless with `SUGAR_COOK=1`.
- `AssetReimport` — the one implementation of "this asset changed, bring everything back
  in line". The file watcher and the editor's Reimport button both call it; they differ
  only in the `force` flag. The editor must never import by another route.
- `AssetManifest` — `resourceKey -> artifact hash`, written at package time, read by a
  shipped runtime so it resolves keys with no source tree (DevDocs/DESIGN_PACKAGING.md).
  Core, headless, no format dependencies.
- `Packager` — the standalone export: reachability walk over scenes + dependency edges,
  cook, copy, write the manifest, copy the exe + DLLs, and verify the result resolves
  source-free. Engine layer, headless (`SUGAR_PACKAGE=1`). `ResourceManager` never learns
  a package exists. `scripts/build_release.ps1` chains `cmake --build` + packaging into
  the release pipeline (Phase 21, DevDocs/DESIGN_BUILD_PIPELINE.md).

The first four are Core. `CookedAsset` and `AssetCooker` are Engine-layer, because
cooking needs the parsing libraries Rule 15 keeps out of Core and produces
`Mesh`/`Texture`/`AudioClip`. **All six are device-free**: cooking, CI and packaging must
never need a Vulkan device.

## CrashHandler (platform, executable-only)

### Responsibility

Capture a crash report on an unhandled exception. Explicitly *not* an engine subsystem —
infrastructure, added pre-freeze (2026-07-28) because its payoff is in M4 dogfooding.

### Scope

Allowed:

- Install `SetUnhandledExceptionFilter`; write a minidump (`.dmp`) + text report to
  `./crashes/` with version/commit, OS/CPU/GPU, loaded scene/package, and a stack walk.
- Depend on Windows platform APIs (`dbghelp`) — which is exactly why it lives in the
  executable and **never** in `SuGarCore` (Rule 15 keeps Core free of platform code).

Not allowed:

- Any role in normal engine control flow. It is passive until a fault occurs.
- Heap allocation inside the handler (the process may already be corrupt): context is held
  in fixed buffers, the report is written with Win32 file APIs.
- Cross-platform ownership: it is a no-op stub off Windows, not a portability layer.

## M4 game-runtime additions (dogfood-forced)

Landed during M4 Level 1 because a real game needed them; each is a small boundary feature,
not a subsystem. See `ROADMAP.md` friction log and the games' `Level 1\Report.md`.

- **`SaveData` (Core).** A `key=value` persistence store for game state that outlives a run
  (high scores, progress). In Core so game behaviours, which link only `SuGarCore`, can call
  it. Not the engine's real persistence (scene serialization) — deliberately minimal.
- **`UILabelComponent` (ECS / runtime UI).** A read-only "text bound to a document element by
  id" component, distinct from the editable `TextInputComponent`; the general HUD hook a game
  writes score/lives into. Synced into the RmlUi document by `RuntimeUIView`.
- **Game boot + shipped-game view.** `SUGAR_GAME=<dir>` boots an external game (scene +
  assets + `Game.dll`) without a chdir — engine resources stay exe/repo-anchored, only game
  content moves. A packaged standalone (`SUGAR_PACKAGE` + `SUGAR_GAME`) ships every runtime
  file (shaders, font, UI docs, the game DLL) and runs viewport-only via `Renderer::
  setGameView` (no editor chrome). The game's C++ never enters the engine repo.
- **Editor theme (`src/editor/EditorTheme.h`).** One flat dark ImGui theme + a default
  DockBuilder layout, replacing `StyleColorsDark` and the floating-panel default. Editor
  chrome only; never linked into Core or shipped to a game.

### M4 Level 3 additions (voxel game — three seams)

Forced by the first real game; each was designed as a record before code (the `AssetGateway`
and `CameraComponent` designs; `DevDocs/DESIGN_RUNTIME_MESH.md`). See the `ROADMAP.md` friction log
(#16–#19) and `Level 3\Report.md`.

- **`CameraComponent` (Core, `src/rendering/CameraComponent.h`).** A game defines the view by
  placing this on an entity (`fovDegrees`/`nearPlane`/`farPlane`/`active` — *lens only*). The
  eye pose is **derived** from the entity's world transform each frame (Rule 21b), never stored,
  so there is no second owner to desync on restore. The engine renders from the lowest-id active
  camera (`CameraMode::SCRIPTED`); absent ⇒ the editor orbit/free camera (back-compat).
  Serialized as an optional `camera` block. *Boundary:* Core owns the component data; the engine
  renderer reads it — no engine `Camera` type crosses into Core.
- **`AssetGateway` (Core, `src/assets/AssetGateway.{h,cpp}`).** The *acquire* counterpart to the
  existing `Registry::onReleaseAsset` release hook. The engine installs a backend of callbacks
  at startup; a game module acquires assets **by key** (`acquireMesh/Texture/AudioClip`) or
  **creates a mesh** (`createMesh`) and gets back an opaque `AssetHandle`. *Boundary invariant:*
  only `std::string`, `AssetHandle`, and POD/glm `RuntimeMeshData` ever cross Core→Engine — no
  `Mesh`, `Vertex`, `VkBuffer`, `VkDevice`, staging buffer, or renderer object. **Ownership stays
  in `ResourceManager`** (sole owner + refcounter): acquire/create incref, entity-destroy decrefs
  via `onReleaseAsset` — balanced, no clone, no second owner.
- **Runtime meshes (`RuntimeMeshData` → `ResourceManager::createRuntimeMesh`).** Game-generated
  vertices become a **derived** GPU mesh under a synthetic `runtime://mesh/<id>` key. The engine
  validates (lengths, index range, cap), copies, and uploads — the caller keeps its CPU data
  (engine retains no pointer). *Scope:* a runtime mesh is **non-source** — never scanned by
  `AssetDatabase`, cooked by `AssetCooker`, packaged by `Packager`, or serialized as a source
  asset (`runtime://` is excluded like `builtin://`). It is a derived resource owned by the
  gameplay/chunk lifetime and rebuilt from its authoritative data on load (Rule 21a). Persisting
  that authoritative data (e.g. voxel edits) is a *separate* future game-data seam, deliberately
  out of scope here.

### M4 Level 3 additions (streaming arc — GPU memory and measurement)

Forced by turning the voxel game into a *streaming* one. See friction log #30–#36 and
`DevDocs/PLATFORM_AUDIT.md`.

- **`DeviceMemoryPool` (engine, `src/rendering/DeviceMemoryPool.{h,cpp}`).** Suballocates
  device-local **buffer** memory from 32 MiB blocks (first fit, coalescing on release,
  dedicated blocks past half a block). Forced by measurement: every runtime mesh owned two
  whole `vkAllocateMemory` allocations, and a streaming run made 20 740 of them. *Scope, kept
  narrow on purpose:* device-local buffers only — staging memory has a different lifetime (one
  reused host-visible buffer) and images have different placement rules
  (`bufferImageGranularity`), so neither goes through it. *Ownership is unchanged:* the pool
  owns blocks and bookkeeping, a `Mesh` still owns its `VkBuffer`s and returns its placement on
  destroy, `ResourceManager` remains the resource owner. The placement bookkeeping is separated
  from Vulkan (`DeviceMemoryPool::detail`) so it is tested headless — that half is the half
  that can be silently wrong.
- **`MeshUploadProfile` (engine, `src/rendering/MeshUploadProfile.{h,cpp}`).** Splits a runtime
  mesh upload into validate / translate / bufferCreate / mapCopy / command / submitWait /
  destroy, plus allocation counts and the driver's `maxMemoryAllocationCount`. Always
  accumulating (a few clock reads against a millisecond operation); printing is opt-in
  (`SUGAR_UPLOADLOG=1`). Exists because "uploading is slow" is not actionable and the fix for a
  queue stall is not the fix for an allocation storm.
- **`NavMesh::lookupGrid` (Core).** A derived XZ uniform grid of polygon indices, rebuilt by
  `buildAdjacency` beside the neighbour table, replacing the linear scans in
  `findContainingPolygon` / `findNearestPolygon`. Derived, never stored in an asset — same rule
  as adjacency, and for the same reason.
- **Measurement surface (extended by the arena's adversarial pass).** `SUGAR_FPSLOG` also
  reports live `meshes` / `textures` / `clips` and the GPU **retirement queue depth**, because
  "did 15 000 spawn/destroy cycles leak anything?" is answered by those staying flat and by
  nothing a headless test can observe. `SUGAR_PHYSDBG=1` prints the broadphase's shape count,
  cell size, bucket count, oversized-shape count and pair count — the numbers that identified
  #41, kept for the same reason `SUGAR_UPLOADLOG` was. `PhysicsWorld::lastBroadphaseCandidateCount()`
  exposes AABB tests performed so the gate can assert the *cost*, not only the answers.
- **Measurement surface.** `SUGAR_FPSLOG` reports `items` (what the scene asked to draw) and
  `drawCalls` (what the GPU was told) **separately**, because instancing lives in the gap and a
  run reporting only the first cannot tell whether batching happened. `Renderer::
  submittedDrawCalls()` is the accessor. Benchmarks gained `navmesh_bake_112` plus its
  weld/adjacency split, so a future change cannot hide which half moved.

### M4 Level 3 additions (combat arena — no new seam, one lifetime rule)

The arena forced no new subsystem. It did settle one ownership question and sharpen one
existing boundary. See friction log #37–#40 and
`E:\Sugar Engine - Games\Level 3\CombatArena\Report.md`.

- **GPU retirement (`ResourceManager`, `DevDocs/DESIGN_GPU_RETIREMENT.md`).** Dropping the last
  reference to a mesh or texture no longer destroys it: the resource is retired for
  `framesInFlight` frames and destroyed by `ResourceManager::endFrame()`, which the renderer
  calls once a frame after waiting on that frame's fence. *Scope:* meshes and textures — audio
  clips are CPU data whose `shared_ptr` a playing voice already holds. *Ownership is unchanged:*
  `ResourceManager` is still the sole owner; retirement only delays the destructor. **New
  engine invariant:** something must call `endFrame()` once per frame, or GPU memory is never
  freed — a leak, deliberately, rather than the use-after-free the queue exists to prevent.
  Headless (no device) destroys immediately, because nothing is in flight.
- **A resource key is a name, not a path (`AssetCooker::sourcePath`).** Keys are anchored at the
  `assets/` segment so the same key identifies an asset wherever its content root is; the
  catalog owns the mapping to a real file. Every cooked type already resolved that way
  internally. It is now public for the one consumer that must read a *source* file directly —
  `ModelImporter`, reconstituting a scene's clips and skins on load. *Scope:* resolution only;
  registered clip/skin names are still built from the key's spelling, so a name a scene wrote
  still matches the name the registry holds.
- **Projections state their depth convention.** `Camera::getProjectionMatrix` and the shadow
  pass use `glm::perspectiveRH_ZO` / `glm::orthoRH_ZO`, never GLM's OpenGL-range defaults,
  because Vulkan keeps only `0 <= z <= w`. Pinned by the `ProjectionDepth` self-test rather than
  by comment. *Not adopted:* a project-wide `GLM_FORCE_DEPTH_ZERO_TO_ONE`, which would change
  header-inline function bodies across the engine and the game DLL — an ODR hazard for a game
  module that includes glm through its own include path.

### M4 Level 3 additions (deferred-backlog pass — three items, promoted by evidence)

Not new subsystems: three long-deferred items whose forcing workload finally appeared. See
friction log #43/#44 and `DevDocs/DESIGN_REPLAN_BACKOFF.md`, `DESIGN_UI_CLIP_MASK.md`.

- **`WorldLabelComponent::occluderMask` (Core).** Collision layers that hide a world label
  when they stand between camera and anchor, tested by a raycast
  (`rendering/WorldLabelVisibility.h`). *Scope:* a **mask, not a bool**, because "solid" is a
  game's word — an arena wall should hide a nameplate and another enemy should not, and only
  the game knows which layer is which. **0 = never occlude, and 0 is the default**, so every
  scene predating it is unchanged; the field is written to disk only when set, so existing
  scene bytes are byte-identical.
- **`NavAgentComponent::replanCooldown` / `failedReplanInterval` (Core).** A per-agent
  backoff on the *retry* path only. *Scope, and the split matters:* the cooldown is
  **authoritative history** (a snapshot restored mid-cooldown that forgot it would fire a
  fresh search storm on the frame you scrubbed to — Rule 21b), while the interval is per-agent
  policy for the same reason `speed` is. Decremented by the **fixed step**, never wall-clock:
  a backoff on OS time would make the simulation non-reproducible. A destination that
  *changes* is never throttled — a new decision is not a retry.
- **`NavPath::searchesPerformed()` / `PhysicsWorld::lastBroadphaseCandidateCount()`
  (diagnostics).** Both added *before* the fixes they justify, so the acceptance criterion is
  the work rather than the frame rate. Never serialized, never snapshotted; resetting them
  changes nothing any agent or body does.
- **Clip masks (`src/ui/`, engine).** `RmlVulkanRenderer` implements `EnableClipMask` /
  `RenderToClipMask`, and `ClipMaskPolicy.h` holds the operation→coverage table as pure data
  so it can be gate-tested. *Scope, deliberately narrow:* the mask is an `R8_UNORM` coverage
  texture owned by the UI renderer — **not** a stencil buffer, because the UI pass's depth
  attachment is `D32_SFLOAT` (no stencil aspect) and offscreen effect layers carry no depth,
  so stencil would have forced a depth-format change on the **scene** pass. Nothing outside
  `src/ui/` changed. RmlUi 6.3 defines three operations, so three are implemented. Not built:
  a mask stack, cross-frame mask caching, masking in the editor's ImGui pass, or any
  game-facing masking API — RCSS remains the authoring surface.

### M4 Level 3 additions (runtime UI, #42 — the interactive half)

- **`data-intent` on a document element (engine, `src/ui/`).** Content declares what a button
  does in the engine's existing vocabulary (`open:<screen>`, `pop`, `focus:<element>`,
  `unfocus`, `text:<s>`, `backspace`, `caretleft/right`), parsed once at load into a
  `UIIntent`. *Scope:* click only; one string argument; no widget library; no directional
  focus traversal. An unparseable value is **reported and ignored**, never guessed into a
  button that does something the author did not write.
- **Active screen as a `screen-<id>` class on the document body.** The game's RCSS decides
  what a screen means, because a screen is rarely "one panel appears" — it also dims the HUD
  and greys a button, which is RCSS's job. Exactly one such class at a time, so a game never
  unsets anything.
- **Text fields render verbatim and escaped.** No engine-chosen prefix (the view used to
  render `"Name: "`/`"Tag: "` by matching the *demo document's* element ids), and the buffer
  is escaped before `SetInnerRML` — it is player-typed text going into markup, so an
  unescaped `<` would let a run name inject elements into the document.

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

Bounds validation is SuGar's job, not tinygltf's (2026-07-28 hardening pass). tinygltf
validates the *syntax* of the file; it does **not** cross-check that an accessor's
`bufferView`/`buffer` indices are in range or that its `byteOffset + count·stride` fits
the backing buffer. Every accessor read in `GltfLoader.cpp` therefore goes through
`validateAccessorSpan` (overflow-safe) and refuses an out-of-range accessor rather than
reading heap out of bounds; index values are also checked against the vertex count to keep
malformed geometry off the GPU. Malformed models are a real surface — a developer imports
third-party art at cook time. Pinned by the `MalformedInput` self-test.

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
pipelines in the Engine layer, fed poses through the `DrawList`. Blend trees and state
machines (17D) are `AnimationGraph` + `AnimationGraphRegistry` with
`AnimationStateComponent` holding the authoritative half (active state, phase,
transition target and elapsed) — state positions are stored as a normalized **phase**
rather than seconds, so a blend tree mixing clips of different lengths keeps foot
contacts aligned.

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

Not built, deliberately (each waits on a real asset or game asking for it, per Rule 18):

- 2D directional blending (1D covers idle/walk/run)
- Transition interruption (needs a second outgoing pose; "queue vs. interrupt" is a
  real design question no character here has posed)
- Animation **events** — which additionally need explicit "already fired" ECS state,
  since a private `lastFiredTime` is Rule 21's anti-pattern under another name
- Full CUBICSPLINE evaluation (17B approximates it linearly at the real keyframes)

Playback, blending, graph evaluation, and runtime state remain owned by SuGar Engine.

State ownership (RULES.md Rule 21)

Authoritative playback state — current time, active state, playback speed — must live
in ECS components (or be snapshot-serializable), so animation survives time travel,
hot reload, and snapshot restore. Derived data — evaluated poses, graph caches — may
be rebuilt each frame and need not be serialized.

The full classification (including the gray areas: why a transition mid-blend is
authoritative, why blend weights are not, why animation events need explicit
already-fired state) is the architecture record: **`DevDocs/DESIGN_ANIMATION.md`**.

---

# Navigation

## Navigation System

### Responsibility

Navmesh generation, pathfinding, and agent movement.

### Scope

Status

**Implemented** (Phase 18, complete). The model layer (18A): `NavMesh` (convex
polygons with adjacency derived from the geometry), `NavMeshRegistry`, deterministic
A* over polygons, funnel string-pulling, `NavAgentComponent`, and the fixed-step
`NavigationSystem` — all in Core, all headless-tested. Baking (18B) splits
`buildNavMesh` (Core, triangle soup in) from `NavMeshBaker` (Engine, harvests scene
meshes); `NavMeshSourceComponent` names the navmesh a piece of geometry feeds. Editor
tooling (18C) adds the Navigation panel, viewport overlays, and an explicit Rebake.
Agent-radius erosion and local obstacle avoidance (18D) sit either side of planning.

Implementation

Hand-rolled — navigation is an engine subsystem. No Recast/Detour.

Layering

Navmesh data, queries, planning, the bake *algorithm*, and the navigation system live
in **SuGarCore** — planning is pure math over plain data, so it needs no GPU and stays
headless-testable (Rule 9, Rule 15). Only triangle *harvesting* (which reads `Mesh`
through `ResourceManager`) lives in the Engine layer, and only the panel and overlays
live in the Editor.

This split is deliberate and load-bearing: a bake that took `Mesh` — which includes
`vulkan.h` — would be untestable headlessly. Testability is a property of where the
boundary goes, not something added to an algorithm afterwards.

Pipeline

```
agent-radius erosion ──► A* ──► corridor ──► local avoidance ──► steering
   (bake time)                                  (per step)
```

Erosion happens **before** planning because it changes the traversable space itself;
avoidance happens **after** because it responds to transient conditions. The invariant
that follows: **avoidance changes _how_ an agent traverses its corridor, never _which_
corridor it chose.** An agent that steps aside for a moving obstacle is still on its
planned route and rejoins it.

Future

Not built, deliberately (each waits on a real game asking for it, per Rule 18):

- Off-mesh links (jumps, ladders, doors) — asset data plus a status, no state-model change
- Crowd simulation and agent-to-agent avoidance
- Hierarchical / portal-graph search for large worlds
- True polygon-offset erosion (current erosion is polygon-granular; the real answer
  when that is insufficient is voxelization, not a more elaborate offset)
- Automatic navmesh invalidation when scene geometry changes (Rebake is manual on
  purpose, so the trade-off stays evidence-driven)

Planning, baking, steering, and runtime state remain owned by SuGar Engine.

State ownership (RULES.md Rule 21 / 21b)

Authoritative state — the destination, **the planned path**, progress along it, the
goal that path was planned for, and the agent's status — must live in ECS components,
so navigation survives time travel, hot reload, and snapshot restore. Derived data —
search scratch, the polygon corridor, containment queries, steering and avoidance
vectors — is recomputed and never serialized.

The path being authoritative is the non-obvious one, and it is **Rule 21b**: a path is
a function of where the agent stood *when it planned*, so recomputing it from the
present can legitimately produce a different valid route. The full classification
(including why `status` must record that a plan was *attempted*, and why avoidance is
derived while the path is not) is the architecture record:
**`DevDocs/DESIGN_NAVIGATION.md`**.

Asset reconstitution (RULES.md Rule 21a)

`NavAgentComponent::navMesh` is a name, so something must rebuild that navmesh from
the name alone on scene load. `NavMeshBaker::ensureSceneNavMeshes` does it — but as a
**post-load step**, not a per-entity one, because a navmesh is derived from the whole
scene and every source entity must exist and be parented first. This is the first case
in the engine where Rule 21a's reconstitution cannot happen per component.

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
- **Game-defined per-entity state** (`GameDataComponent`): a string-keyed map of numbers
  and strings the engine stores, serializes, snapshots and inspects but never *reads*.
  Keys belong to the game. It exists because a game module links only Core and cannot add
  a component type, while `Behavior`'s contract requires per-entity state to live in
  components — see `DevDocs/DESIGN_GAME_DATA.md`.

Never replaced by an external ECS library.

Not owned (deliberately): a reflection system or game-registered component *types*. The
untyped map is the narrow build (Rule 8); nothing has yet needed types, only somewhere to
put per-entity values.

---

# Physics

## SuGar Physics

### Responsibility

Physics simulation.

### Scope

Owns:

- Collision (box/sphere; uniform-grid broadphase → narrowphase → impulse resolution)
- Collision **filtering** — per-collider `layer`/`mask` bitmasks
- **Trigger (sensor) colliders** — report overlaps, apply no physical response
- Rigid bodies
- Solver (semi-implicit Euler, restitution + Coulomb friction, on the fixed step)
- Events (carry a real per-shape contact point)
- **Queries** — `PhysicsQuery::raycast` (ray vs box/sphere, layer-filtered), in Core so
  gameplay behaviors can call it (hitscan, ground checks, line-of-sight, world picking)

No Bullet.

No PhysX.

No Jolt.

Physics remains hand-rolled. Deliberately out of scope until a game forces it: rotational
dynamics / angular velocity (boxes collide axis-aligned), continuous collision detection,
capsule/convex/mesh shapes, multi-iteration solver stacking.

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

Capture is **policy-gated** (`SnapshotCapturePolicy`), separate from storage
(`ISnapshotStorage`): full per-step capture while a scene fits a per-step time budget,
auto-paused when it does not, and off in packaged builds. This keeps time-travel an
editor-only, affordable-by-default affordance without touching the snapshot format — a
survivors-like at 1000 entities forced it (full-scene JSON per step was ~160 ms/frame).
A binary/delta storage backend remains future work, unblocked by this separation.

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