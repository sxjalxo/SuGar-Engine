# SuGar Engine — Features

What the engine does today, in detail. [`README.md`](README.md) is the short version;
[`ROADMAP.md`](ROADMAP.md) is what is planned and why, including the M4 friction log of
every change a real game forced. Architectural constraints are in
[`DevDocs/RULES.md`](DevDocs/RULES.md), dependency boundaries in
[`DevDocs/REQUIREMENTS_AND_SCOPE.md`](DevDocs/REQUIREMENTS_AND_SCOPE.md).

---

## Core Features

### Runtime / Play Mode

* `EngineState` machine: **Edit / Play / Paused**
* **Scene snapshot/restore** — Play snapshots the live scene to memory; Stop
  restores it, discarding all gameplay mutations
* **Fixed 60 Hz gameplay update loop** (`updateSystems`) — deterministic,
  frame-rate independent; rendering stays uncapped
* **Declared system schedule** (Phase 13A) — the gameplay pipeline (script →
  animation → physics → collision dispatch → audio → runtime UI) is a
  `SystemScheduler` of `System`s that
  each declare their read/write component sets; runs in deterministic order, with
  independence analysis (`stages()`) as the foundation for future parallelism
* **Enforced system access** (Phase 13B) — the ECS records every component read
  and write, and the scheduler flags any storage a system touched but never
  declared (or mutated while declaring read-only). Hidden coupling becomes a
  message, not a mystery. Debug-only, on by default; zero release cost
* **Runtime UI** (Phase 16B) — player-facing UI via RmlUi, rendered through a
  hand-written Vulkan `RenderInterface` into the game viewport. All authoritative UI
  state (screen stack, focus, text buffer, caret) lives in **ECS**, so it snapshots,
  time-travels and hot-reloads like any other component; hover/layout/rendering are
  derived. Design: `DevDocs/DESIGN_RUNTIME_UI.md` — rationale
  and lessons: `DevDocs/RUNTIME_UI_LESSONS.md`
* **RmlUi effects compositor** (M4 #14) — the runtime UI renders in its own pass after
  the scene pass, and the Vulkan `RenderInterface` implements RmlUi 6's layer path
  (offscreen colour layers + a fullscreen Gaussian-blur composite + save-layer-as-texture),
  so `box-shadow`/`blur` work. Stencil clip masks are a deliberate not-yet-needed gap
* **Player input** — `InputActions` maps named actions/axes to keys **and mouse buttons**
  (one flat code space, so `"Fire"` binds Space *or* mouse-left); the engine publishes the
  cursor as a world-space ray each frame, and `Input::getMouseWorldOnPlane(point, normal)`
  turns it into a world point on any plane (Rule 21b: the ray is stored, the point derived)
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
  the broadphase near-linear (2000 bodies ≈ 1.9 ms vs ~13 ms all-pairs).
  **Collision layers/masks** filter which colliders interact; **trigger (sensor)**
  colliders report overlaps without a physical response; collision events carry a
  real per-shape contact point. `PhysicsQuery::raycast` (ray vs box/sphere,
  layer-filtered) gives behaviors hitscan / ground checks / line-of-sight / picking —
  all from `SuGarCore`, no solver access needed
* **Cascade destroy** — `destroyEntityTree` removes an entity and its whole subtree
  (plain `destroyEntity` orphans children to the world, the primitive the editor's
  delete-then-undo relies on); a built-in `builtin://cube` mesh is the guaranteed,
  file-free fallback for a missing mesh
* **Prefabs** — save an entity subtree to `.prefab`, instantiate additively,
  revert instances to source
* **3D model import** — glTF/glb via tinygltf (parse-only, isolated); nodes →
  ECS hierarchy with **quaternion** transforms, PBR factors + base-color texture
* **Quaternion transforms** — `Transform` rotations are quaternions (gimbal-free,
  native to glTF); the inspector edits Euler degrees

### Animation (Phase 17 — complete)

* **Hand-rolled playback** — clip data, keyframe sampling, and interpolation are
  the engine's own (external libraries may *import* animation data; they never own
  playback). Lives in Core, so it needs no GPU and is fully headless-testable
* **Playback state is ECS state** — `AnimationPlayerComponent` (clip name, time,
  speed, playing, loop) is authoritative and serialized, so animation survives
  snapshot restore / time travel / hot reload with **no animation-specific code**.
  The pose is never stored: `Pose = f(clip data, playback state)`, recomputed each
  fixed step. This is RULES.md Rule 21's worked example, built the right way round
* **Deterministic** — time advances only on the fixed 60 Hz step by `dt * speed`;
  loops wrap modularly, so extreme speeds and rewinds (negative `speed`) stay in
  range. Scrub to a frame twice and the pose is bit-identical
* **Clips are assets, not state** — `AnimationClipRegistry` maps names → immutable
  clip data (the `BehaviorRegistry` pattern), so components hold a string, not a
  pointer, and a re-imported clip can be swapped under a running animation
* Tracks target entities **by name**, resolved against the player's subtree;
  channels are independent (a clip that only rotates leaves scale alone)
* **glTF clip import** (17B) — `animations` + samplers are parsed into engine clips
  (tinygltf stays parse-only). glTF's channel-per-property is regrouped into one
  track per node, and node **indices become node names** at the boundary, so a
  re-export that reorders nodes can't invalidate a saved scene. Clips register as
  `"<path>#<clipName>"`, mirroring the `"<path>#<meshIndex>"` mesh key; importing
  attaches a *stopped* player for the first clip (the importer registers assets, it
  doesn't decide gameplay). STEP + LINEAR are evaluated; CUBICSPLINE keys are read
  and interpolated linearly (exact at each keyframe, less smooth between)
* **Skinning** (17C.1) — **the ECS hierarchy *is* the skeleton**: joints are ordinary
  entities that the animation system already poses, so a `Skin` stores only what ECS
  can't know (joint names in joint-index order + inverse bind matrices). Joint
  matrices are **derived** — `Skinning::computeJointMatrices` is not a system, writes
  nothing, and is recomputed on demand, so `Skinning = f(mesh, skeleton pose)` and the
  renderer stays a pure consumer. `SkinnedMeshComponent` is a reference, not state
* **GPU skinning** (17C.2) — `JOINTS_0`/`WEIGHTS_0` attributes, skinned scene **and
  shadow** pipelines (without the latter a character animates while its shadow stands
  in bind pose), joint matrices uploaded per draw through a dynamic UBO. The renderer
  owns buffers, descriptors and bindings — and no animation state: poses arrive on the
  `DrawList` as plain matrices, so the pass transports a pose it never owns
* **Blend trees + state machines** (17D) — an `AnimationGraph` is a data asset: states
  play one clip or a **1D blend tree**, transitions fire on a parameter comparison or
  when a one-shot finishes, and cross-fades blend two poses. The authoritative half
  (active state, phase, transition target + elapsed) is ECS, so a character saved
  **mid-cross-fade** scrubs back mid-cross-fade. Parameters live in their own component
  because they are *gameplay's* state that the animator only reads
* **Phase, not seconds** — a state's position is normalized `DevDocs/DESIGN_ANIMATION.md` — the
  authoritative/derived split, and why an animation transition is authoritative
  where the identical-looking UI tween is derived

### Navigation (Phase 18 — complete)

* **The navmesh is an asset** — convex polygons over a shared vertex array with
  per-edge adjacency *derived from the geometry* (never stored in the file, so no
  asset can carry a stale neighbor table). `NavMeshRegistry` maps names → immutable
  meshes, the `AnimationClipRegistry` / `BehaviorRegistry` pattern
* **Deterministic A\*** over polygons, costed between **portal midpoints** (a centroid
  measure over-charges long thin polygons and picks visibly silly routes). Ties break
  on polygon index — a *total* order, so the priority queue's instability can't pick a
  different equally-optimal route between runs
* **Funnel string-pulling** turns the polygon corridor into the shortest actual line
  through its portals. Without it agents walk polygon-center to polygon-center: the
  classic zig-zag that makes a correct search look broken. Corridor and waypoints stay
  separate — local avoidance (18D) will need to steer *within* the corridor
* **The path is authoritative, not a cache** — the call this subsystem exists to get
  right. A path *looks* like `f(navmesh, position, goal)`, but it is a function of
  where the agent stood **when it planned**: at a corridor fork, an agent that took the
  left route and an agent replanning from halfway down it can legitimately disagree, so
  a "derived" path would quietly break replay. `NavAgentComponent` therefore serializes
  the plan, the progress, the goal it was planned for, and the status — and a restored
  agent *continues its journey* instead of re-deciding it
* **`status` remembers that a plan was attempted** — an agent with an impossible goal
  would otherwise re-run A* over the whole mesh every fixed step, forever
* **Baking** (18B) — scene geometry becomes a navmesh. `NavMeshSourceComponent` names
  the navmesh a mesh contributes to, so **the scene carries its own bake inputs** and a
  loaded scene rebuilds its navmesh from the entities it just created (RULES.md Rule
  21a). The split is the design: `buildNavMesh` (Core) takes a world-space **triangle
  soup and nothing else** — no `Mesh`, no `ResourceManager`, no Vulkan — while
  `NavMeshBaker` (Engine) is the only navigation code that knows those exist
* **Welding is load-bearing** — adjacency matches edges by *vertex index*, so triangles
  that merely touch share no edge until welded. An unwelded bake makes every triangle an
  island and every path `Unreachable`, which reaches a user as "pathfinding is broken";
  `NavBakeStats::isolatedPolygons` exists to name the cause of that symptom
* **Editor** (18C) — a Navigation panel with live bake statistics, warnings that name
  *causes* (isolated polygons → raise `weldEpsilon`; everything rejected → check winding
  or slope), and an **explicit Rebake**. Rebaking is manual on purpose: automatic
  rebuilds on every geometry edit are a performance trade nobody has measured, so the
  button keeps the decision evidence-driven (Rule 18). Navmesh and agent paths overlay
  the viewport through ImGui's draw list — no Vulkan pipeline, because the editor is
  ImGui (Rule 11) and the overlay reads state without owning any
* **Erosion before planning, avoidance after** (18D) — agent-radius erosion changes the
  *traversable space*, so it happens at bake time and the planner sees its result; local
  avoidance responds to *transient* conditions, so it happens during steering. The rule
  that falls out: **avoidance changes _how_ an agent traverses its corridor, never
  _which_ corridor it chose** — an agent stepping aside for a moving crate is still on
  its planned route and rejoins it. The test asserts the plan is byte-identical at every
  step of a detour
* **Repulsion is not avoidance** — a purely radial push means an obstacle squarely
  between an agent and its waypoint gives `desired + push == 0`, so the agent stops dead
  a clearance-width short and never arrives. Each obstacle also contributes a
  **tangential** term sided toward the goal; the tangent is what turns a standoff into
  an orbit
* Design: `DevDocs/DESIGN_NAVIGATION.md` — including the rule
  the three design records converge on: *a cache is derived only if it is a function of
  the **current** state; a value computed once from a past state is a function of
  history, and history is authoritative*

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
* **Snapshot capture policy** (`SnapshotCapturePolicy`) — gates *when* a snapshot is
  taken, not how it's stored: full per-step capture for scenes that fit a per-step
  time budget, auto-paused (with a Timeline reason) once a scene is too large, and off
  entirely in packaged builds. A survivors-like at 1000 entities forced this — the
  full-scene JSON serialize was ~160 ms/frame (6 fps); gating it restored ~420 fps.
  Because only the *policy* changed, a future binary/delta backend still drops in

### Rendering

* Vulkan-based forward renderer
* Offscreen rendering with ImGui viewport integration
* Depth testing and proper render pass separation
* Multi-light system (ambient + diffuse + specular)
* Physically-inspired materials (metallic + roughness) with a **flat-colour tint**
  (`Material::baseColor` over `builtin://white`) for 2D/sprite work
* **Per-material blend modes** — Opaque / Masked (alpha cutout) / Translucent / Additive;
  the draw list buckets opaque-first and sorts the blended tail back-to-front, one pipeline
  per blend state (the Unreal/Unity render-queue seam)
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
  `AudioListener`, `UIScreen`, `Focus`, `TextInput`, `AnimationPlayer`, `SkinnedMesh`,
  `AnimationState`, `AnimationParameters`, `NavAgent`, `NavMeshSource`, `NavObstacle`
* Hierarchical transforms with parent-child relationships
* Deterministic draw list generation

### Resource System

* Handle-based `ResourceManager`
* Cached mesh + texture loading
* GPU resource lifetime management
* Hot reload (in-place resource updates)
* **Cross-platform texture loading via stb_image** (no Windows/WIC lock-in)
* **`AssetGateway` (Core) — game-safe asset acquire/create** (M4 L3): a game module (which
  links only `SuGarCore`) acquires meshes/textures/audio by key, or **creates a mesh from
  generated vertices** (`RuntimeMeshData` → a derived `runtime://` GPU mesh), through injected
  callbacks — only `string`+`AssetHandle`+POD cross the boundary; ResourceManager keeps sole
  ownership and refcounting. Runtime meshes are non-source (never scanned/cooked/packaged)

### Game Camera

* **`CameraComponent` (Core)** — a game defines the view by placing it on an entity; the
  renderer frames from that entity's world transform (pose derived, not stored). Absent ⇒ the
  editor orbit/free camera. Serialized as an optional `camera` block

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

## Verification & tooling

Every harness below is headless — no window, no GPU — and runs from the repository
root. `SUGAR_VALIDATE` is the gate every change must pass; see
[`CONTRIBUTING.md`](CONTRIBUTING.md#testing) for what that means for a pull request.

### Validate (one command)

`SUGAR_VALIDATE=1` runs every correctness gate — self-tests **and** stress tests —
and exits nonzero if any fail, so it drops straight into CI:

```powershell
$env:SUGAR_VALIDATE = "1"; build\Release\SuGarEngine.exe; $env:SUGAR_VALIDATE = ""
# ... [validate] === 68/68 checks passed, 0 failure(s) ===
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
SystemScheduler, ComponentAccess, SnapshotPatch, RuntimeUI, Animation,
AnimationImport, Skinning, SkinImport, AnimationGraph, Navigation, NavMeshBake,
NavAvoidance, ViewportOverlay, Serializer, SceneLoad, BehaviorRegistry,
RegistryGraph, MalformedInput, SaveData, ScriptSystem, SnapshotPolicy, ColliderFilter,
Raycast, ContactPoint, DestroyEntityTree, and BuiltinCubeMesh. A test that *throws* is
reported as `FAIL ... threw: <message>` and the run continues — one broken subsystem
shouldn't hide the others.

`Serializer` is a **golden test**: it pins the byte-exact scene text for an entity
carrying every optional component. Deliberately brittle — the on-disk format is what
snapshots and time travel ride on, so it should only change on purpose. Round-trip
tests can't cover this: they prove the writer and parser *agree*, and both can drift
together. See [DevDocs/RULES.md](DevDocs/RULES.md) Rule 9a (a test must be shown to fail) and Rule 9b
(round-trips are necessary, not sufficient).

`MalformedInput` is the **input trust boundary** gate: it feeds hostile bytes to the
three deserializers that take external input — a deeply nested JSON bomb (would overflow
the parser stack), a glTF whose accessor runs past its buffer (would read out of bounds),
and a cooked `.sgc` claiming an absurd element count (would abuse `reserve` / overflow a
size calc). Each must be *rejected cleanly*, so malformed-input handling is a permanent
regression, not an ad-hoc fuzz. Added in the 2026-07-28 hardening pass; the trust
boundary it guards is mapped in [SECURITY.md](SECURITY.md) (the asset-pipeline design
record that covers the same ground is development-local for now).

RmlUi also has a separate headless integration smoke test for the engine-only view
scaffold. It initialises RmlUi with FreeType, loads a bundled Lato font, creates a
context, loads a memory document, verifies the DOM, and renders through a no-op
interface:

```powershell
$env:SUGAR_UITEST = "1"; build\Debug\SuGarEngine.exe; $env:SUGAR_UITEST = ""
```

### Stress / QA harness

Where the self-tests check each subsystem is *sane*, `SUGAR_STRESS=1` hammers the
load-bearing ones at scale and at edge inputs — most notably validating the physics
grid broadphase against a brute-force O(n²) oracle, plus determinism, in-place
restore over many cycles (no id drift/leak), id recycling churn, ring overflow, and
**400 animated characters over 600 steps** (players + state machines + blend trees:
bit-identical determinism across two runs, and snapshot survival at scale):

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

### Snapshot capture cost

Three knobs instrument the time-travel snapshot path. All are **off by default**, dev-only,
and documented in full in `DevDocs/DESIGN_SNAPSHOT_CAPTURE_COST.md`.

| Knob | Effect |
| --- | --- |
| `SUGAR_SNAPDBG=1` | per-capture phase breakdown to **stderr** — `total`, `null_sink`, `materialize`, `bytes`, `entities`, `ns_per_byte` |
| `SUGAR_SNAP_BUDGET=<ms>` | overrides `SnapshotCapturePolicy`'s 4 ms budget, so a measurement run captures every step instead of latching off at the first over-budget frame |
| `SUGAR_SNAP_CORPUS=<path>` | dumps the serialized snapshot bytes to disk on every capture |

`SUGAR_SNAP_CORPUS` writes from inside the region `SUGAR_SNAPDBG` times, so a run with both
set reports an inflated `total`. **Corpus capture and timing capture are separate runs** — the
two knobs are independent for exactly that reason.

The phase split is obtained by **differential substitution**, not by timers inside the writer:
`writeSceneJson` walks the ECS and formats tokens in one pass, and a timer per call would cost
the same order as the work it measures. `SUGAR_BENCH` reports the same split headlessly
(`snapshot_traversal`, `snapshot_format`, `snapshot_materialize`, `snapshot_store`) alongside
`snapshot_sink_bytes_delta`, the instrument's self-check: the discarding sink and the real save
are independent paths over the same scene, so their byte counts must agree.

Note `snapshot_phase_identity_pct` is exactly that — an **algebraic identity**, not a validity
check. Two of the four phases are derived by subtraction from the same measurements, so the sum
equals the total by construction and the figure can never fail. It is retained only because it
detects arithmetic tampering.

### Crash reports

An unhandled exception (Windows) writes a timestamped pair to `./crashes/`: a **minidump**
(`.dmp` — open in Visual Studio / WinDbg with the matching `.pdb`) and a **human-readable
`.txt`** with the engine version + git commit, OS / CPU / GPU, the loaded scene and package,
the exception code, and a symbolized stack trace. It's installed automatically at startup;
nothing to enable. Deliberately not an engine subsystem — just enough to make a crash after
hours of play *actionable* instead of a lost afternoon (`src/CrashHandler.cpp`, exe-only,
no-op on other platforms). `crashes/` is gitignored.

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

**In-game FPS overlay (windowed):** `SUGAR_FPSLOG=1` prints FPS + drawn-entity + draw
count to stderr each second — a lightweight, capturable stand-in for a profiler HUD, used
to measure the L3 per-block-vs-chunk scaling decision from evidence rather than intuition.

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


---

## Branding

The artwork lives in `assets/branding/` and is used in three places:

| File | Used by |
| --- | --- |
| `sugar_icon.ico` | the executable's shell icon, embedded through `src/platform/SuGarEngine.rc` |
| `sugar_cube.png` | the window icon (`glfwSetWindowIcon`), in the editor and in a packaged game |
| `sugar_logo.png` | the full lockup in the editor's **Editor** panel |

The icon is the cube alone — a wordmark is unreadable at 16x16. The in-editor lockup keeps
its alpha and blends over the theme background, so no background is baked into the art.
Regenerate the derived files from a new master with `scripts/make_branding.py`.
