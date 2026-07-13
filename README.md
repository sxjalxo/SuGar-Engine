# SuGar Engine

**SuGar Engine** is a custom C++17 Vulkan-based 3D game engine built with GLFW and CMake.
It is being built around one bet: **win the inner development loop** — instant
iteration and debuggable systems — rather than chasing feature parity with the
big engines. See [ROADMAP.md](ROADMAP.md) for the full vision and plan.

---

## Current Status

**M1 / Track A done; Track B underway** ("the iteration engine")

The engine is a working runtime with a full editor on top: a clean **Edit → Play
→ Stop** cycle with snapshot/restore and a fixed-timestep loop; a behavior +
input system; hand-rolled physics with collision events; prefabs & glTF import;
quaternion transforms; a hand-rolled audio mixer; and an editor with scene
picking, gizmos, undo/redo, duplicate/delete, multi-select, hierarchy
reparenting, and component management — all on top of the Vulkan renderer, asset
pipeline, hot reload, and shadow mapping. **Track B (the wedge) is well underway**:
a hardened editor command system (transactions, entity remapping, compression),
**time-travel debugging** (snapshot ring-buffer + timeline scrubbing + frame
stepping), an ECS query console, and — the headline — **code hot reload**: a
layered `Editor -> Engine -> Core` split with gameplay in a DLL that hot-swaps
live when recompiled, state preserved.

> Positioning: *"A Vulkan engine designed for instant iteration and debuggable
> systems — not just rendering power."* Open-source, dev-led, aimed at indie devs.

---

## Core Features

### Runtime / Play Mode

* `EngineState` machine: **Edit / Play / Paused**
* **Scene snapshot/restore** — Play snapshots the live scene to memory; Stop
  restores it, discarding all gameplay mutations
* **Fixed 60 Hz gameplay update loop** (`updateSystems`) — deterministic,
  frame-rate independent; rendering stays uncapped
* **Declared system schedule** (Phase 13A) — the gameplay pipeline (script →
  physics → collision dispatch → audio) is a `SystemScheduler` of `System`s that
  each declare their read/write component sets; runs in deterministic order, with
  independence analysis (`stages()`) as the foundation for future parallelism
* **Enforced system access** (Phase 13B) — the ECS records every component read
  and write, and the scheduler flags any storage a system touched but never
  declared (or mutated while declaring read-only). Hidden coupling becomes a
  message, not a mystery. Debug-only, on by default; zero release cost
* **Editor Systems panel** (Phase 13C) — a live view of the gameplay pipeline:
  each system's declared read/write masks, the computed parallel stages, and any
  access violations (green when every system stays within its declaration)
* Editor **Play / Pause / Stop** toolbar with a viewport state tint

### Gameplay (Track A)

* **Behavior system** — stateless, name-registered behaviors (`onStart`/
  `onUpdate`); all per-entity state lives in components (reload-ready)
* **Input mapping** — named actions/axes over raw input; behaviors never touch
  GLFW key codes
* **Hand-rolled physics** — semi-implicit Euler integration, gravity, box/sphere
  collision (broadphase → narrowphase → impulse resolution), restitution +
  Coulomb friction, on the fixed step
* **Prefabs** — save an entity subtree to `.prefab`, instantiate additively,
  revert instances to source
* **3D model import** — glTF/glb via tinygltf (parse-only, isolated); nodes →
  ECS hierarchy with **quaternion** transforms, PBR factors + base-color texture
* **Quaternion transforms** — `Transform` rotations are quaternions (gimbal-free,
  native to glTF); the inspector edits Euler degrees

### Audio

* **Hand-rolled mixer** over a thin device backend (miniaudio used only as the
  device layer + file decoder; voice mixing/pitch resampling is our own)
* `AudioSource` (clip / volume / pitch / loop / playOnStart / spatial) and
  `AudioListener` (master gain) components
* **`AudioClip` is a `ResourceManager` asset** like Mesh/Texture — handle-based,
  cached, ref-counted, hot-reloadable
* **Distance attenuation** for spatial sources relative to the active listener
* **Collision-triggered sounds** via a general collision-event system (see below)
* Pause freezes the mix; Stop silences all voices; drag-drop `.wav/.mp3/.flac/
  .ogg` onto an entity in the editor

### Collision events

* `PhysicsWorld` emits `CollisionEvent { a, b, point, normal, impulse }` per
  contact each step
* Dispatched to `Behavior::onCollision` on the involved entities — one primitive
  for landing/footstep sounds, destruction, particle spawning, gameplay triggers
* Built-in `CollisionSfx` behavior plays an entity's `AudioSource` on impact

### Time-travel debugging (Track B)

* **Snapshot ring-buffer** — a full-scene snapshot is captured every fixed step
  during Play (rolling ~10 s window)
* **Timeline panel** — scrub backward to restore and inspect any recorded frame,
  with a seconds-behind-live readout
* **Frame stepping** — step through history frame-by-frame, or advance the live
  sim one fixed step at a time; **Resume Live** to return to play
* **Timeline bookmarks** — tag a frame with a label and jump Previous/Next
* **Live hot-patch** — the inspector edits the running scene directly, so
  component data changes apply while playing with no restart
* **Code hot reload** — gameplay behaviors live in a `SuGarGame` DLL; rebuild it
  while the engine runs and it auto-swaps (debounced file-watch, or F8 / "Reload
  Scripts"). Behaviors reconnect by name and component state is preserved
* **ECS query console** — `<component> [where <field> <op> <value>]`
  (e.g. `rigidbody where vel.y < 0`); click a match to select it
* **Pluggable snapshot backend** (`ISnapshotStorage`) — the Timeline is decoupled
  from how frames are stored (JSON today; binary/delta later)

### Rendering

* Vulkan-based forward renderer
* Offscreen rendering with ImGui viewport integration
* Depth testing and proper render pass separation
* Multi-light system (ambient + diffuse + specular)
* Physically-inspired materials (metallic + roughness)
* Directional shadow mapping with PCF filtering
* Gamma correction for improved visual output

### Engine Architecture

* **Layered architecture** — `Editor -> Engine -> Core`, where `SuGarCore` is a
  Vulkan-free shared library (ECS, components, math, behaviors) and gameplay lives
  in a **`SuGarGame` DLL that links only Core** and is loaded at runtime. The same
  Core could back multiple games.
* Fully authoritative **Entity Component System (ECS)**
* Components: `Transform` (quaternion rotation), `Mesh`, `Material`, `Hierarchy`,
  `Name`, `Script`, `RigidBody`, `Collider`, `PrefabInstance`, `AudioSource`,
  `AudioListener`
* Hierarchical transforms with parent-child relationships
* Deterministic draw list generation

### Resource System

* Handle-based `ResourceManager`
* Cached mesh + texture loading
* GPU resource lifetime management
* Hot reload (in-place resource updates)
* **Cross-platform texture loading via stb_image** (no Windows/WIC lock-in)

### Editor (ImGui)

* Dockable editor layout (ImGui docking)
* Hierarchy panel (ECS-driven) with **drag-and-drop reparenting** (cycle-safe)
* Inspector panel (live component editing) with **jitter-free quaternion rotation
  editing** (`EditorTransformCache`)
* Viewport panel (render-to-texture) with **click-to-select scene picking**
  (camera ray vs entity AABB) and **translate/rotate/scale gizmos** (ImGuizmo)
* **Multi-select** (Ctrl-click in hierarchy/viewport), **duplicate** (Ctrl+D),
  and **delete** (Del)
* **Undo/redo** command history (Ctrl+Z / Ctrl+Y) for transform edits, reparent,
  duplicate, delete, and component add/remove
* **Component management** — add/remove components + editable script behavior
* **Prefab instance** controls — Revert and Apply-to-Prefab
* Asset browser with **color-coded thumbnail tiles** + drag-and-drop
* Play Controls panel (runtime state)

### Asset Pipeline

* Filesystem-based asset registry
* Drag & drop: `.obj` → mesh, `.png / .jpg / .jpeg` → texture
* Live asset updates without restarting the engine

---

## Controls

| Input | Action |
|-------|--------|
| `W A S D` | Move camera (FREE mode) |
| `Mouse` | Look around |
| `1` / `2` / `3` | FREE / ORBIT / FOLLOW camera |
| `F5` / `F9` | Save / reload scene |
| `F6` | Play / Stop |
| `F7` | Pause / Resume |
| `F8` | Hot-reload the game module (behaviors) |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo |
| `Ctrl+D` / `Del` | Duplicate / Delete selected |
| `Ctrl+Click` | Add to selection (hierarchy / viewport) |
| `Esc` | Exit |

---

## Build & Run

### Requirements

* Vulkan SDK
* CMake 3.21+
* Visual Studio 2022 (C++)

### Build

```powershell
cmake -S . -B build
cmake --build build --config Debug --target SuGarEngine --parallel 1
```

### Run

```powershell
# Run from the project root so assets resolve correctly
build\Debug\SuGarEngine.exe
```

### Self-tests

Each subsystem has a quick headless confidence test. Run them (no window/Vulkan)
before launching the editor:

```powershell
$env:SUGAR_SELFTEST = "1"; build\Debug\SuGarEngine.exe; $env:SUGAR_SELFTEST = ""
```

Prints a per-test PASS/FAIL table (with timings) for CoreBoundary, CommandHistory,
EntityQuery, SnapshotStorage, Physics, SystemScheduler, ComponentAccess,
Serializer, BehaviorRegistry, and RegistryGraph.

### System access enforcement

Debug builds verify that every gameplay system only touches the component
storages it declared. It's on by default there (Warn mode), surfaced in the editor
**Systems** panel (green when clean, the offending storages named when not).
`SUGAR_STRICT=1` escalates to fail-fast: the first undeclared access throws and the
process exits nonzero — for headless/CI runs:

```powershell
$env:SUGAR_STRICT = "1"; build\Debug\SuGarEngine.exe; $env:SUGAR_STRICT = ""
```

Release builds compile the tracking out entirely, so this costs nothing to ship.

---

## Roadmap

Full plan in [ROADMAP.md](ROADMAP.md). Summary:

* **Phase 1–4** *(done)* — Vulkan setup, renderer (swapchain/depth/materials),
  ECS, editor, asset pipeline, hot reload, advanced lighting + shadows, optimization
* **Phase 5** *(done)* — Runtime foundation (Play mode: snapshot/restore + update loop)
* **Phase 6–9** *(done)* — behaviors + input mapping, hand-rolled physics, prefabs +
  glTF import, quaternion transforms, hand-rolled audio
* **Phase 10** *(done)* — editor UX (10A picking + jitter-free rotation + reparenting;
  10B gizmos + undo/redo + duplicate; 10C multi-select + delete + component
  management + prefab Revert/Apply + thumbnails). **M1 / Track A complete.**
* **Track B** *(in progress)* — the wedge: 11A editor command infrastructure;
  11B time-travel scrubbing + frame stepping + ECS query console; 11C snapshot
  backend abstraction + timeline bookmarks *done*. **Phase 12 code hot reload**:
  12A/12B/12C *done* — layered `Editor -> Engine -> Core` (Vulkan-free `SuGarCore`
  shared lib) with gameplay behaviors in a **`SuGarGame` DLL that links only Core**,
  loaded at runtime and **hot-reloaded live** when recompiled. **Phase 13A–D**
  *done* — opinionated scheduling: the gameplay pipeline is declared `System`s with
  read/write sets behind a deterministic `SystemScheduler`, those declarations are
  **enforced** by the ECS (Warn in-editor, `SUGAR_STRICT` fail-fast for CI), and an
  editor **Systems** panel shows the order, parallel stages, and live violations.
  Parallel execution + incremental rebuilds deferred by design (nothing independent
  to parallelize yet; async fights time-travel). Also later: reload only affected
  systems, in-place state restore, binary/delta snapshots, query growth
* **Track C** — graphics, cross-platform, packaging, ecosystem

---

## Project Goal

SuGar Engine is being developed as a **final-year project (FYP)** and an
**open-source engine** demonstrating low-level graphics (Vulkan), engine
architecture (ECS, resource systems), real-time rendering, and — its core
differentiator — **iteration speed and runtime debuggability**.

---

## License

Apache 2.0 License
