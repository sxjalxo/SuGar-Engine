# SuGar Engine

**SuGar Engine** is a custom C++17 Vulkan-based 3D game engine built with GLFW and CMake.
It is being built around one bet: **win the inner development loop** — instant
iteration and debuggable systems — rather than chasing feature parity with the
big engines. See [ROADMAP.md](ROADMAP.md) for the full vision and plan.

---

## Current Status

**M1 + M2 done; M3 (Engine Platform Complete) in progress** ("the iteration engine")

The engine is a working runtime with a full editor on top: a clean **Edit → Play
→ Stop** cycle with snapshot/restore and a fixed-timestep loop; a behavior +
input system; hand-rolled physics with collision events; prefabs & glTF import;
quaternion transforms; a hand-rolled audio mixer; and an editor with scene
picking, gizmos, undo/redo, duplicate/delete, multi-select, hierarchy
reparenting, and component management — all on top of the Vulkan renderer, asset
pipeline, hot reload, and shadow mapping. **M2 (the iteration wedge) is complete**:
a hardened editor command system (transactions, id-stable recreate, compression),
**time-travel debugging** (snapshot ring-buffer + timeline scrubbing + frame
stepping) with in-place restore that keeps your selection/undo, an ECS query
console, and — the headline — **code hot reload**: a layered
`Editor -> Engine -> Core` split with gameplay in a DLL that hot-swaps live when
recompiled, state preserved. **Next (M3): Runtime UI (RmlUi)** — the platform's
missing player-facing half.

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
  collision (uniform-grid broadphase → narrowphase → impulse resolution),
  restitution + Coulomb friction, on the fixed step. Deterministic; the grid keeps
  the broadphase near-linear (2000 bodies ≈ 1.9 ms vs ~13 ms all-pairs)
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
* **In-place restore** (Phase 14A) — restoring a snapshot (scrub, or Stop) patches
  component data into the existing entities instead of rebuilding them, so entity
  ids are preserved and **editor selection, inspector focus, and undo history
  survive** a scrub or Stop (falls back to a full rebuild only on structural change)
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

### Validate (one command)

`SUGAR_VALIDATE=1` runs every correctness gate — self-tests **and** stress tests —
and exits nonzero if any fail, so it drops straight into CI:

```powershell
$env:SUGAR_VALIDATE = "1"; build\Release\SuGarEngine.exe; $env:SUGAR_VALIDATE = ""
# ... [validate] === 19/19 checks passed, 0 failure(s) ===
```

Benchmarks are intentionally excluded — they're measurements, not pass/fail gates
(run them separately under `SUGAR_BENCH`). The individual harnesses below still run
standalone when you want just one.

### Self-tests

Each subsystem has a quick headless confidence test. Run them (no window/Vulkan)
before launching the editor:

```powershell
$env:SUGAR_SELFTEST = "1"; build\Debug\SuGarEngine.exe; $env:SUGAR_SELFTEST = ""
```

Prints a per-test PASS/FAIL table (with timings) for CoreBoundary, CommandHistory,
EntityIdRecycling, EntityQuery, SnapshotStorage, Physics, PhysicsBroadphase,
SystemScheduler, ComponentAccess, SnapshotPatch, RuntimeUI, Serializer,
BehaviorRegistry, and RegistryGraph.

### Stress / QA harness

Where the self-tests check each subsystem is *sane*, `SUGAR_STRESS=1` hammers the
load-bearing ones at scale and at edge inputs — most notably validating the physics
grid broadphase against a brute-force O(n²) oracle, plus determinism, in-place
restore over many cycles (no id drift/leak), id recycling churn, and ring overflow:

```powershell
$env:SUGAR_STRESS = "1"; build\Release\SuGarEngine.exe; $env:SUGAR_STRESS = ""
```

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

### Profiling

Headless profiler over a representative scene: snapshot size, 600-frame ring
memory, save time, patch restore, query, physics step, scheduler overhead.
`SUGAR_BENCH_ENTITIES=N` scales the scene. Build Release for honest timings
(memory is config-independent):

```powershell
$env:SUGAR_BENCH = "1"; build\Release\SuGarEngine.exe; $env:SUGAR_BENCH = ""
```

Baseline (Release, ~636 B/entity/frame): 50 ent → 18 MiB ring / 0.6 ms save;
500 → 182 MiB / 5.6 ms; 2000 → 730 MiB / 26 ms. Per-frame **save cost** grows into
the 60 Hz budget before memory does — the evidence gating binary/delta snapshots.
Hot-reload swap latency logs live (`[GameModule] hot reload complete (N ms swap)`).

For regression tracking over time, emit machine-readable output:
`SUGAR_BENCH_FORMAT=csv|json` (+ `SUGAR_BENCH_OUT=benchmarks/2026-08-14.json` to
write a file), then diff runs across commits.

---

## Roadmap

Full plan in **[ROADMAP.md](ROADMAP.md)**; architectural constraints in
**[RULES.md](RULES.md)**; dependency boundaries in
**[REQUIREMENTS_AND_SCOPE.md](REQUIREMENTS_AND_SCOPE.md)**. Milestone summary:

* **M1 — Engine Foundation** *(done)* — Vulkan renderer + shadows, ECS, editor, asset
  pipeline, physics, audio, prefabs + glTF, serialization.
* **M2 — Developer Iteration** *(done)* — time travel (snapshot ring + scrubbing +
  bookmarks), ECS query console, native code hot reload (`Editor → Engine → Core` +
  hot-swappable `SuGarGame` DLL), deterministic scheduler + access enforcement,
  in-place restore, stable entity recreation (id-remap layer *deleted*), uniform-grid
  physics broadphase, and the self-test / stress / benchmark harnesses
  (`SUGAR_VALIDATE`).
* **M3 — Engine Platform Complete** *(in progress)* — the platform is "complete" when
  a developer can build a typical indie game **without extending the engine**. The
  missing floor: **Runtime UI (RmlUi)** — its ECS-authoritative *model layer* (16A)
  is done and snapshot-safe; the RmlUi *view* (16B) is next — plus Animation,
  Navigation, Asset-pipeline maturity, Packaging, Build pipeline. Explicitly *not* required: AAA rendering,
  networking, console ports, world streaming, marketplace.
  * **The platform's missing half:** SuGar has a complete *developer* UI (Dear ImGui,
    permanently reserved for tooling) but intentionally **no *player* UI**. Runtime UI
    begins with RmlUi — it completes half the engine, so it leads M3.
* **M4 — Dogfood** — build real games (sandbox → platformer → shooter) as validation;
  after this, engine work is driven by real projects, not speculation.

---

## Floating point & world scale

SuGar uses **32-bit floating point** for world coordinates (positions, transforms,
collider math). At very large coordinates, floating-point *precision* — not the
engine — becomes the limiting factor: near `X = 8,000,000` a `float`'s spacing
(ULP) is already ~0.5 units, so sub-unit colliders degenerate and collisions/
transforms lose accuracy. This is expected **IEEE-754** behavior, not an engine
bug; every 32-bit engine (including commercial ones) has the same wall, which is
why open-world engines rebase the origin.

Practically: keep gameplay within roughly ±100,000 units of the origin and
precision is a non-issue. The physics broadphase is hardened against extreme/NaN
coordinates (it won't crash or corrupt), but it can't manufacture precision the
`float` type doesn't have.

Future large-world support could add, in rough order of cost:
- **origin rebasing** — periodically shift the world so the camera stays near 0
- **double-precision transforms** — 64-bit positions, floats for rendering
- **hierarchical / sector coordinates** — integer sector + local float offset

Deferred until a real project needs it (see the roadmap's evidence-first stance).

---

## Project Goal

SuGar Engine is being developed as a **final-year project (FYP)** and an
**open-source engine** demonstrating low-level graphics (Vulkan), engine
architecture (ECS, resource systems), real-time rendering, and — its core
differentiator — **iteration speed and runtime debuggability**.

---

## License

Apache 2.0 License
