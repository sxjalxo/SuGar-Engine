# SuGar Engine — Roadmap

From the current **renderer + editor** to a **fully functional, hand-rolled,
open-source game engine** — built around one bet, not feature parity.

---

## North Star: win the inner loop, not the feature list

We will **not** beat Unity/Unreal on graphics, asset ecosystems, or
marketplaces. Those are resource wars we lose. The wedge is the thing both
engines neglect once a project grows:

> **The inner loop: change → run → see result → repeat.**
> Ideal is 1–2s. Unity drifts to 10–30s at mid-scale; Unreal to minutes.
> Whoever keeps that loop *instant and debuggable* wins the hearts of indie devs.

**Positioning:** *"A Vulkan engine designed for instant iteration and
debuggable systems — not just rendering power."*

**The decision lens (Track B onward):** for every feature, ask *"does this make
developers faster?"* Iteration speed / debuggability is the identity and the
tie-breaker — time travel, query console, hot reload, self-tests, bookmarks all
point the same way.

Success = indie devs adopt it because iterating in SuGar feels faster and
clearer than the big engines. Open-source, community-driven, dev-led.

### Why SuGar is well-positioned to make this bet
Our existing foundation is *unusually* aligned with this goal:
- **Authoritative, data-oriented ECS** → state is flat, serializable, and
  inspectable (the opposite of hidden coupling).
- **Handle-based resources + asset hot reload** → reload primitives already exist.
- **Snapshot/restore (Phase 5A)** → already the core primitive for both
  time-travel debugging *and* state-preserving hot reload.

---

## The five DX pillars (our actual product)

1. **Hot reload that actually works** — reload only affected systems/components,
   live-patch running objects (React Fast Refresh, but for game objects). No
   Unity-style domain reload, no Unreal-style coffee-break recompiles.
2. **Deterministic incremental builds** — dependency tracking at system/component
   granularity; rebuild only what changed.
3. **First-class runtime introspection** — inspect *actual* live state, not editor
   guesses; snapshot ring-buffer → time-travel scrubbing; a query layer over the ECS.
4. **Opinionated architecture** — enforce the ECS/component-graph, declare system
   dependencies, make bad patterns hard to write. "You can't easily write bad
   architecture here."
5. **Async-first core** — no blocking systems (asset load, physics, scripting);
   everything schedulable. *(Sequenced carefully — async fights determinism; see notes.)*

### Design principles baked into EVERY phase from here on
So the DX pillars are cheap to add later instead of expensive retrofits:
- **No hidden state.** All gameplay state lives in serializable components — never
  in private fields of a behavior object. (This is what makes hot reload + time
  travel possible at all.)
- **Systems are pure-ish functions** `(World, dt) → mutations`, with declared
  read/write sets. No system reaches into another's internals. *(Enforced since
  Phase 13B: the ECS records every component access and the scheduler flags
  anything a system didn't declare — Warn on by default in Debug / editor Systems
  panel, `SUGAR_STRICT=1` for fail-fast CI.)*
- **Everything round-trips** through serialization. If it can't be snapshotted,
  it can't exist in gameplay state.
- **Every subsystem has a confidence self-test.** Not exhaustive — one quick
  "is this sane?" check each, aggregated into a single headless run
  (`SUGAR_SELFTEST=1` → `src/SelfTests.h`) that prints a per-test PASS/FAIL table
  **with timings** before the editor even launches. Covered today: CommandHistory,
  EntityIdRecycling, EntityQuery, SnapshotStorage, Physics, PhysicsBroadphase,
  SystemScheduler, ComponentAccess, SnapshotPatch, Serializer (save),
  BehaviorRegistry, RegistryGraph, CoreBoundary. Pending a small device harness: Serializer round-trip,
  ResourceManager (need Vulkan).

---

## Where we are

A polished scene viewer + editor **plus a working runtime (Phase 5 done):**
- Vulkan forward renderer, offscreen viewport → ImGui dockspace; shadow mapping (PCF)
- Authoritative ECS, handle-based `ResourceManager` + asset hot reload
- ImGui editor (hierarchy / inspector / asset browser), JSON serialization, cameras
- **Play/Pause/Stop with in-memory snapshot/restore + fixed-timestep update loop**
- **Cross-platform texture loading via stb_image** (WIC/Windows lock-in removed)

---

## Track A — Gameplay core (table stakes for "a game engine")

These must exist for SuGar to be usable at all. We build each one **to the
design principles above**, so the DX superpowers in Track B drop in cleanly.

### Phase 5 — Runtime foundation (Play mode)  (DONE)
- 5A snapshot/restore · 5B fixed-step update loop · 5C Play/Pause/Stop toolbar.
- *Note: 5A's snapshot is the seed of time-travel debugging (Pillar 3).*

### Phase 6 — Behavior system + input mapping  (DONE)
Built **reload-ready, split later** (the Phase 12 DLL split stays mechanical):
- 6A: `Behavior` (stateless, `onStart`/`onUpdate`) + `BehaviorRegistry`
  (name → shared instance); `ScriptComponent` stores only the behavior name +
  per-entity `started` flag (all state in components). `updateSystems()` drives
  behaviors; built-in `Spinner` replaced the hardcoded spin; serialized via an
  optional `"script"` field so snapshot/restore + save/load preserve behaviors.
- 6B: `InputActions` — named actions/axes over raw `Input` (arrow keys → move,
  Space → Jump by default); behaviors never touch GLFW key codes.
- 6C: built-in `PlayerController` + a free-standing "Player" cube proving the
  full Play → input → gameplay → Stop cycle.

### Phase 7 — Physics (hand-rolled)  (DONE)
- 7A: `RigidBodyComponent` + `ColliderComponent` (box/sphere); `PhysicsWorld`
  semi-implicit Euler + gravity on the fixed step; serialized; falling-box demo.
- 7B: collision — all-pairs broadphase → narrowphase (AABB/sphere combos) →
  positional correction + restitution impulse; static ground.
- 7C: Coulomb friction (clamped tangential impulse); rigid-body/collider/script
  inspector panels; bouncy-box demo (restitution + friction).
- Notes / future work: ~~O(n²) broadphase~~ (replaced by a uniform grid in
  Phase 15); boxes are axis-aligned (rotation ignored); physics bodies should be
  top-level.

### Phase 8 — Prefabs & 3D model import  (DONE)  *(requested)*
- 8A: prefab core — `SceneSerializer` refactored to share per-entity write/parse;
  `savePrefab` (subtree → `.prefab`) / `instantiatePrefab` (additive spawn);
  editor "Save as Prefab" + "Instantiate".
- 8B: **glTF import via tinygltf**, fully isolated in the loader (parse-only;
  no tinygltf type leaks into the engine); `ResourceManager` dispatches by extension.
- 8C: real asset→prefab pipeline — glTF **nodes → ECS hierarchy with transforms**
  (quaternion rotations, no Euler conversion), per-node mesh via `<path>#<meshIndex>` keys (round-trips),
  **materials from glTF PBR factors** + base-color texture path; **"Import to
  Scene" auto-generates a prefab**; `PrefabInstanceComponent` link + "Revert to
  Prefab".
- Known limits (future): embedded glTF textures not decoded (external URI only);
  multi-primitive meshes use the first material; overrides are implicit and
  "Revert" respawns from the prefab (no per-field override tracking yet);
  nested-prefab *links* are flattened into the parent prefab.

### Phase 9 — Audio  (DONE)
- 9: **hand-rolled mixer over a thin device backend** — miniaudio (vendored,
  `external/miniaudio`) is confined to the audio layer and used *only* as the
  playback device (`AudioEngine`) + file decoder (`AudioLoader`); the voice
  summing / per-voice pitch resampling (linear interpolation) is our own code.
  PIMPL keeps the huge header out of the rest of the build. `AudioListenerComponent`
  (gain) + `AudioSourceComponent` (clip/volume/pitch/loop/playOnStart/spatial);
  authored fields round-trip via the serializer, runtime fields don't — mirrors
  `ScriptComponent`. `AudioSystem` is a pure-ish `(World, AudioEngine)` function
  driven from `updateSystems`: playOnStart triggers, live param sync, and
  **distance attenuation** to the active listener for spatial sources. Lifecycle:
  Pause freezes the mixer, Stop/scene-replace silences all voices. Editor:
  inspector panels + "Add Audio Source/Listener" + drag-drop of
  `.wav/.mp3/.flac/.ogg` onto an entity.
- 9A: **`AudioClip` is a first-class `ResourceManager` asset** — just like
  Mesh/Texture: `AssetHandle` on the component, handle→key in the serializer,
  caching + ref counting + hot reload, asset-browser consistency. **Collision
  events**: `PhysicsWorld::step` accumulates `CollisionEvent`s (a, b, point,
  normal, impulse); `updateSystems` dispatches them to `Behavior::onCollision`
  on each involved entity — one event primitive that also unlocks footsteps,
  destruction, particle spawning, gameplay triggers. The built-in `CollisionSfx`
  behavior fires a one-shot on its entity's `AudioSource` above an impulse
  threshold. **This realizes M1**: the FallingBox plays a landing sound when it
  hits the ground.
- Known limits (future): clips are fully decoded into memory (no streaming for
  long music); the mixer takes a short mutex per callback (a lock-free command
  queue is the later optimization); spatial audio is distance attenuation only
  (no stereo panning / Doppler); rolloff distances are global constants, not
  per-source fields; collision contact point is approximated as the pair midpoint
  (fine for sfx/triggers; refine if a use-case needs exact contacts).

### Phase 10 — Editor UX  (DONE)
The "pleasant to use every day" track. Built incrementally — roughly **3
interrelated features per session**. Groupings below are a guide, not a contract;
reorder freely. **Finishing this phase closes M1 / Track A.**

- **10A — Select & organize**  (DONE)
  - **Scene picking** — click in the viewport to select the entity under the
    cursor: a camera ray (built from the inverse view matrix, so it's
    mode-agnostic and dodges the Vulkan projection Y-flip) tested against each
    mesh entity's world AABB; nearest hit wins.
  - **`EditorTransformCache`** — inspector Euler jitter fixed. The displayed
    Euler is cached alongside the quaternion it came from and only re-derived
    when the selection changes or the quaternion is modified externally
    (detected via a dot-product threshold) — never every frame mid-drag.
  - **Hierarchy drag-and-drop reparenting** — drag an entity onto another to
    reparent (drop on the empty panel area to unparent); the reparent is applied
    after the tree walk (no mid-iteration mutation) and `setParent`'s cycle guard
    is caught and surfaced as a status message.
- **10B — Manipulate & history**  (DONE)
  - **Gizmos** — translate / rotate / scale handles in the viewport on the
    selected entity via **vendored ImGuizmo** (`external/ImGuizmo`; solved,
    non-differentiating problem — engine logic stays ours). Toolbar switches
    Move/Rotate/Scale + World/Local. The manipulated *world* matrix is converted
    back to a *local* transform (parent-aware) and decomposed; the Vulkan
    projection Y-flip is undone for ImGuizmo's screen mapping. Picking is
    suppressed while the cursor is over / using the gizmo.
  - **Undo/Redo command system** — `EditorCommand` + linear `CommandHistory`
    (`src/editor/`). Commands record already-applied changes:
    `TransformCommand` (gizmo + inspector, captured per drag start..release),
    `ReparentCommand` (hierarchy DnD), `CreateSubtreeCommand` (duplicate).
    Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) + toolbar buttons. History is cleared on
    scene replace (ids become invalid).
  - **Entity duplicate** — Ctrl+D / toolbar; deep-copies the subtree by
    round-tripping it through `SceneSerializer::savePrefabToString` +
    `instantiateFromString` (so resource ref-counts are handled exactly like
    prefabs), parents the copy as a sibling, and selects it.
  - ~~Known limit: a `TransformCommand` recorded against a *duplicated* entity
    won't survive "undo past the duplicate, then redo" — the entity's id is
    reassigned on re-instantiate.~~ *(Fixed in 11A via remapping, then more
    fundamentally in 14B: recreate preserves the original ids, so the reassignment
    never happens.)*
- **10C — Assets, components & prefabs**  (DONE)
  - **Multi-select** — Ctrl-click in the hierarchy or viewport extends the
    selection; `selectedEntity` stays the "primary" (inspector + gizmo target)
    while batch ops act on the whole set. Stale members are pruned each frame.
  - **Entity delete** — Del key / toolbar; destroys the selected subtrees as one
    undo step (`DeleteSubtreeCommand` + `CompositeCommand`). Duplicate is now
    multi-select aware too.
  - **Inspector component add/remove** — "Add Component" popup + per-panel Remove
    for the value-only components (RigidBody, Collider, Script, AudioListener),
    each a single undo step via `LambdaCommand`; the Script behavior name is now
    editable. (Resource-bearing components — Mesh/Material/AudioSource — stay
    drag-drop assigned. *Reordering is N/A:* components live in separate
    storages, so order is not a data property and has no runtime effect.)
  - **Prefab overrides UI** — instance shows its source prefab with **Revert**
    (pull from prefab) and **Apply to Prefab** (push the instance's current state
    back to the `.prefab`). *Per-field* override tracking is still deferred.
  - **Asset thumbnails** — color-coded tile grid in the asset browser
    (mesh/image/audio/prefab), drag-drop to assign, double-click a prefab to
    instantiate / a model to import. *Live image/mesh render previews* are
    deferred (they intersect the renderer + texture hot-reload; better as a
    graphics-side Phase 14 item).

**Phase 10 complete → M1 / Track A is done.**

---

## Track B — The wedge (what makes SuGar worth choosing)

Built on Track A's clean state model. **This is the differentiator; it ships
before we chase graphics.**

### Phase 11 — Live introspection & state hot reload  (Pillars 1 + 3)  (IN PROGRESS)

#### Phase 11A — Editor infrastructure polish  (DONE)
Hardened the editor command system (`src/editor/`) before building live
introspection on top of it. Resolves the id-stability limitation from 10B/10C.
- **Transactional command history** — `CommandHistory::begin/commit/abort
  Transaction`; commands pushed inside a transaction accumulate into one atomic
  undo step, and abort rolls them back. Multi-select duplicate/delete now use it
  (replacing the ad-hoc `CompositeCommand`).
- **Persistent command IDs** — each stored history entry gets a stable
  per-session id (for introspection / future history serialization).
- **Entity remapping** — *(superseded, then deleted in Phase 14B.)* Originally,
  when a subtree command re-instantiated a destroyed subtree (duplicate/delete
  undo↔redo) it built an old→new id map and the history rewrote every other
  command's stored ids ("undo past a duplicate, then redo"). Phase 14B removed the
  need: recreation now restores the subtree into its **original** ids
  (`createEntityWithId`), so references stay valid and the entire remap layer
  (`EntityRemap`, `EditorCommand::remap`, id-zipping, history rewrite) was deleted.
- **Command compression** — `EditorCommand::tryMerge`; `push` lets the top entry
  absorb a same-target follow-up (e.g. consecutive `TransformCommand`s on one
  entity) to keep history granular-but-not-noisy.
- Verified by an opt-in self-test (run with `SUGAR_SELFTEST=1`): transactions +
  compression (`CommandHistory`), and id recycling (`EntityIdRecycling`).

#### Phase 11B — Live introspection & time travel  (DONE)
- **Snapshot ring-buffer + time-travel scrubbing** (DONE) — a `std::deque` of
  full-scene snapshots (`saveToString`) captured each fixed step in Play (capacity
  ~600 frames / 10 s, oldest evicted). New editor **Timeline** panel: a scrubber
  restores any past frame (pauses + `loadFromString`), frame **stepping**
  (`|< Step` / `Step >|`) — within the ring while scrubbing, or advancing the sim
  one fixed step at the live edge — **Resume Live**, and a **seconds-behind-live**
  readout (`Time: +.2f s`) alongside the frame index. `SuGarApp` owns the ring +
  `scrubCursor` (-1 = live); the main loop only advances (and captures) while
  live-playing. This is the basic time-travel debugging behind M2.
- **Live state view / hot-patch** (already works) — the inspector edits the live
  registry directly, including while playing, so component data is hot-patched
  with no restart. A dedicated "live vs authored" view is a later refinement.
- **ECS query/inspector console** (DONE) — `EntityQuery` (engine-side, unit-tested)
  parses `<component> [where <field> <op> <value>]` (e.g. `rigidbody where vel.y < 0`)
  over the authoritative ECS; the "Query" panel lists matches and click-selects
  them. Curated numeric fields per component; ops `< <= > >= == !=`.
- Known limits (deferred by design → see 11C / Phase 12): snapshots are full-scene
  JSON every frame (memory: 600 frames x scene size); scrubbed edits aren't kept
  (inspection only — a "fork from here" branch is a later nicety). *(The scrub-clears-
  selection limit is fixed in Phase 14A: restore now patches in place, ids preserved.)*

#### Phase 11C — Snapshot backend abstraction  (IN PROGRESS)
The Timeline must not know *how* frames are stored. An `ISnapshotStorage`
interface (`src/core/SnapshotStorage.h`) now sits behind the ring so the encoding
can evolve without touching the UI or `SuGarApp`'s time-travel logic. `SuGarApp`
holds a `unique_ptr<ISnapshotStorage>` and works only through it.
- `JsonSnapshotStorage` (DONE) — full-JSON-per-frame baseline; owns the ring +
  eviction and exposes stable `frameNumber(index)` (survives eviction shifts).
- **Timeline bookmarks** (DONE) — tag the current frame with a label ("physics
  exploded"), jump Previous/Next, add/update/remove. Bookmarks key off stable
  frame numbers and are pruned when their frame scrolls off the window.
- `BinarySnapshotStorage` / `DeltaSnapshotStorage` (later) — compact binary
  encoding / frame-deltas + keyframes. **Now evidence-gated, not assumed** — see
  the Phase 14C measurements below: warranted past a few hundred entities, where
  the primary driver turns out to be per-frame *save cost*, not just memory.

#### Phase 11D — Query language growth  (later)
The `EntityQuery` parser is a simple tokenizer, deliberately structured to grow:
- **Compound predicates** — `transform where pos.y > 5 and scale.x > 2` (and/or).
- **String comparison** — `script where behavior == "PlayerController"`.
- **Ordering** — `rigidbody order by vel.y desc`.
Not needed now; noted so the grammar is extended rather than rewritten.

### Phase 12 — Code hot reload  (Pillar 1, the hard one)  (CORE DONE)

**Architecture (decided): layered `Editor -> Engine -> Core`.** Core is a shared
library (so its singletons like `BehaviorRegistry` are shared); the hot-swappable
**game module (DLL) links only against Core**; **no executable exports**. Larger
refactor than an exe-exports DLL split, but a cleaner API boundary, simpler
cross-platform hot reload, and it matches the engine's dependency-inversion
direction.
- 12A — **Core extraction** (DONE):
  - **Dependency inversion of `Registry`** — the ECS no longer calls the
    Vulkan-coupled `ResourceManager`; it releases handles through an injected
    `onReleaseAsset` hook the Engine wires. *Refinement (later, not now):* evolve
    the `std::function` hook into an `IAssetReleaseService` interface — interfaces
    are easier to mock in tests than raw callbacks.
  - **`SuGarCore` library** — ECS, component data, math, `Behavior` +
    `BehaviorRegistry`, `InputActions`, `CollisionEvent`, `AssetHandle`,
    `Material`. Compiles with **no Vulkan** (self-sufficient glm/glfw includes),
    which is what enforces the boundary; the Engine links it.
- 12B — **Game module DLL** (DONE):
  - Core flipped to **SHARED** (`WINDOWS_EXPORT_ALL_SYMBOLS`) so `BehaviorRegistry`
    lives in exactly one module, shared by the engine and the game DLL.
  - Concrete behaviors (Spinner/PlayerController/CollisionSfx) moved out of Core
    into **`SuGarGame` (DLL) — `src/game/`** — which links *only* Core and exports
    `registerGameBehaviors` (verified: building `SuGarGame` pulls in Core, not the
    engine). Core's `BehaviorRegistry` keeps only the mechanism.
  - **`GameModuleLoader`** (Windows `LoadLibrary`, confined to one `.cpp`) loads
    `SuGarGame.dll` at startup and calls the entry point; unload clears the
    registry before `FreeLibrary` so DLL-owned behavior instances never dangle.
  - **`CoreBoundary` self-test** added — constructs Registry / BehaviorRegistry /
    InputActions headless; a tripwire for boundary regressions.
- 12C — **Reloadable game module** (DONE): recompile the DLL while the engine
  runs and it hot-swaps live. The loader loads a **uniquely-named copy** of the
  DLL each time (the original stays free for the build to overwrite); on reload it
  clears the registry, `FreeLibrary`s, and loads the fresh copy. Triggered by a
  **debounced file-watch** on the source DLL (waits for the build to finish
  writing) plus manual **F8 / "Reload Scripts"**. The copy retries briefly to
  ride out post-build locks, and only advances its "loaded" timestamp on success
  so a failed attempt keeps retrying. State survives because it lives in
  components (Phase 6 design) and ScriptComponents reconnect by name.
  *Verified end-to-end:* rebuilt `SuGarGame.dll` with the engine running → auto
  `hot reload complete`, no crash, no dangling pointers (see the pre-flight audit:
  the only DLL-owned code pointers are the behavior vtables, destroyed by
  `clear()` before `FreeLibrary`; every `std::function`/callback is Engine/Core-owned).
- Remaining Phase 12 refinements (later): reload **only affected systems** (not a
  full re-register).
- **In-place state restore** — done in **Phase 14A** (below). Snapshot restore now
  patches component data into the existing entities instead of destroying and
  rebuilding, so editor selection / inspector / undo survive a scrub or Stop.

### Phase 13 — Opinionated scheduling & architecture  (Pillars 2 + 4 + 5)  (IN PROGRESS)

#### Phase 13A — System abstraction + deterministic scheduler  (DONE)
The fixed-step gameplay pipeline stops being a hardcoded call sequence and
becomes *declared* systems with explicit data dependencies.
- **`System` + `SystemScheduler`** (Core, `src/ecs/SystemSchedule.h`) — a
  `ComponentType` enum + `ComponentMask` bitset over Registry's 11 storages; each
  `System` carries its declared `reads`/`writes` masks and a `run(dt)` closure.
  The scheduler runs systems in **deterministic registration order** (determinism
  is the default — it's in tension with async time-travel). Lives in Core: depends
  only on component-storage *identity*, never the renderer.
- **`updateSystems` refactored** — the script driver, physics step, collision
  dispatch, and audio are now four registered `System`s (built lazily, identical
  order/behavior) instead of an inline sequence. Each declares an honest
  read/write set; unconstrained script/dispatch systems declare broad writes, so
  they correctly stay ordered against physics/audio rather than falsely parallel.
- **Independence analysis** — `systemsConflict` (write-write / read-write / write-read
  hazards) + `SystemScheduler::stages()` greedily groups mutually-independent
  systems into ordered stages. This is the **Pillar 5 foundation**: stages *could*
  run in parallel, but the engine still executes sequentially via `run()` until
  parallelism is opted in per-system. It's also the seam for **Pillar 4** lints (a
  system touching an undeclared component is a boundary violation to catch later).
- Verified by the `SystemScheduler` self-test (`SUGAR_SELFTEST=1`): deterministic
  order, hazard detection, and stage grouping on synthetic systems.

#### Phase 13B — Access enforcement & architecture guard rails  (DONE)  *(Pillar 4)*
13A let systems *declare* their read/write sets; nothing checked the declarations
were true. 13B makes the ECS report what each system actually touches and flags
anything it didn't declare — the "you can't easily write bad architecture here"
pillar, and the prerequisite for trusting the masks enough to parallelize on them.
- **`ComponentAccess`** (Core, `src/ecs/ComponentAccess.{h,cpp}`) — `ComponentType`
  / `ComponentMask` moved here; a `ComponentTraits<T>` map from component struct →
  type bit (specialized in `Registry.h`, in lockstep with the storages); a
  `ComponentAccessTracker` accumulating **touched** and **mutated** masks; and a
  scoped, DLL-shared active-tracker slot (a function-local `thread_local` in Core's
  `.cpp` — a header inline variable would give the exe and each DLL their own copy
  under MSVC COMDAT folding, the same trap `BehaviorRegistry` avoids).
- **`ComponentStorage` instrumented** — const paths (`get() const`, `has`,
  `getAll() const`) record a **read**; mutating paths (`add`/`remove`/`clear`/
  non-const `get`/`getAll`) hand out a mutable reference and record a **write**.
  This is what makes reading through a `const Registry&` load-bearing: it's how a
  read-only system *proves* it is one.
- **`SystemScheduler` enforcement** — `setEnforcement(Warn)` runs each system inside
  a tracker scope and reports `touched & ~(reads|writes)` (undeclared coupling) and
  `mutated & ~writes` (declared read-only, mutated anyway). Warn, not halt: a wrong
  declaration shouldn't kill the session you're debugging. Each distinct violation
  is reported once per system, not every fixed step. Enabled with `SUGAR_STRICT=1`.
- **Zero release cost** — all recording is behind `SUGAR_ACCESS_TRACKING`, set by
  CMake on Core as `PUBLIC` for Debug only (PUBLIC because `ComponentStorage` is a
  header template also instantiated in the engine and the game DLL; all three
  modules must agree). Release keeps the bare storage ops; enforcement is inert.
- **It immediately found a real bug in 13A's declarations**: the Audio system
  resolves spatial positions via `getWorldPosition`, which walks parent transforms
  — an undeclared `Hierarchy` read. Fixed, along with const read paths in
  `PhysicsWorld` (colliders), `AudioSystem` (listeners), and collision dispatch
  (script names) that were reporting reads as writes.
- Verified by the `ComponentAccess` self-test: read/write recording, a compliant
  system reporting nothing, undeclared-access and mutated-read-only violations each
  caught with the exact storages at fault, once-per-signature reporting, and the
  **real `PhysicsWorld::step` staying inside the Physics system's declared masks**.

#### Phase 13C — Editor Systems panel + always-on guard rail  (DONE)  *(Pillar 4)*
Makes the 13B guard rail *visible*: the enforcement result becomes an editor view
instead of a stderr line, and the pipeline's structure (order, access, parallel
stages) is inspectable at a glance — the "make devs faster" tie-breaker.
- **`Systems` panel** (`Renderer::drawSystemsPanel`) — three collapsing sections:
  *Order & access* (each system with its `R:`/`W:` masks via `describeComponentMask`),
  *Parallel stages* (`SystemScheduler::stages()`, a stage of >1 highlighted as
  parallelizable — today each stage is one system, so the payoff of narrowing
  declarations is visible as it lands), and *Access guard rail* (green when clean,
  the exact undeclared storages per system when not; says "compiled out" in Release).
- **Enforcement on by default in Debug** — `setupSystemSchedule` enables
  `AccessEnforcement::Warn` whenever tracking is compiled in, so the panel
  populates with no env var. Violations feed a bounded log on the scheduler
  (`violationLog()`); `SUGAR_STRICT=1` additionally mirrors them to stderr for
  headless/CI. Release keeps the bare storage ops (inert).
- **Schedule built at construction** — `SuGarApp` builds the pipeline in its ctor
  (was lazy on first update) so the editor can introspect it before Play; the
  Renderer gets a `const SystemScheduler*` view.

#### Phase 13D — Strict fail-fast enforcement  (DONE)  *(Pillar 4)*
Completes the guard rail: Warn (editor default, panel-only) now has a fail-fast
sibling for CI.
- **`AccessEnforcement::Strict`** — the first undeclared access throws
  `AccessViolationError` (a `std::logic_error` carrying the offending system +
  storages), after the stderr report. In the engine it propagates out of the
  fixed-step loop to `main`, surfacing as a nonzero exit. Enabled with
  `SUGAR_STRICT=1`; the editor keeps Warn so a bad declaration never kills a live
  session. Verified by the `ComponentAccess` self-test (rogue system throws,
  compliant system doesn't).

#### Phase 13E+ — remaining scheduling work  (later, low priority)
- **Parallel execution (Pillar 5) — deferred by design, not just unbuilt.** The
  four gameplay systems all conflict (Audio *reads* Transform, Physics *writes* it;
  Script/CollisionDispatch declare broad writes because behaviors are
  unconstrained), so `stages()` yields one system per stage — parallelism buys
  nothing until behaviors can be split into narrowed-declaration systems. And async
  is in direct tension with the time-travel wedge (see determinism note). So a
  thread-pool executor is real infrastructure with no current payoff; revisit only
  when a genuinely independent, hot system pair exists to justify it.
- **Dependency-aware incremental rebuilds** at system granularity (Pillar 2) — the
  C++ build is external (CMake/MSBuild) and the game DLL already rebuilds fast, so
  system-granular native rebuild is low-value here; revisit if build times grow.
- *Determinism note: async + time-travel are in tension. Default to a deterministic
  ordered schedule; opt into parallelism per-system where it's provably independent.*

**Phase 13 core (Pillars 4 + the Pillar 5 foundation) complete.** Systems are
declared, enforced, introspectable, and independence-analyzed; parallel execution
and incremental rebuilds are deferred with rationale above.

### Phase 14 — In-place state restore  (Pillar 3 / editor-runtime integration)  (IN PROGRESS)

Not scheduler work — editor/runtime integration. The single biggest day-to-day DX
win left: today Play-mode restore (Stop, and every time-travel scrub) **destroys
and rebuilds the registry**, reassigning entity ids and wiping editor selection,
inspector focus, and undo history. That's why scrubbing time backward loses your
selection and why Stop drops your undo stack.

#### Phase 14A — Patch snapshots into the live entities  (DONE)
- **`SceneSerializer::patchFromString`** — restores a snapshot by *patching
  component data into the existing entities* instead of recreating them. Entities
  are matched by serialization order (sorted entity id), which is stable within a
  Play session (behaviors mutate components, they don't create/destroy), so **ids
  are preserved** — and therefore editor selection, inspector state, and undo
  history survive a restore. Component patching mirrors the load path's add /
  update / remove semantics; **resource-backed components (mesh, material texture,
  audio clip) reload only when their key changed**, so a frame-by-frame scrub
  doesn't churn the ResourceManager's ref counts.
- **Structural-mismatch fallback** — the patch requires the same entity count as
  the snapshot; on a mismatch (an entity spawned/destroyed since) it returns false
  *without mutating*, and the caller (`SuGarApp::restoreSnapshot` / `stop`) falls
  back to the old destroy-and-rebuild (`loadFromString` + `onSceneReplaced`, which
  clears editor state). So correctness never depends on the fast path.
- **`SuGarApp` split** — `refreshSceneVisualsKeepEditor()` (draw list, orbit, GPU
  resources, audio silence) is now separate from the editor-state wipe;
  `onSceneReplaced()` = that + `clearEditorState()`. In-place restore calls the
  former, so selection/undo carry through Stop and scrub.
- Verified by the `SnapshotPatch` self-test (headless): after a simulated step,
  patching frame 0 back restores transforms / rigid-body velocity / hierarchy and
  resets the script `started` latch, **with the same entity ids**, and a structural
  mismatch declines without touching the registry.

#### Phase 14B — Delete the remap machinery  (DONE)
The 11A entity-remap system existed because the *editor-command* recreate path
(duplicate / delete undo↔redo) re-instantiated subtrees with **fresh** ids,
forcing every other command's stored ids to be rewritten. 14B recreates the
subtree into its **original** ids instead, so references stay valid and the whole
remap layer was **deleted outright** — a new capability removing old complexity.
- **`EntityManager::createEntityWithId(id)`** (+ `Registry` passthrough) — recreates
  a specific id: claims it from the free list, or advances the id counter to it and
  banks the skipped ids. Sound under linear-history semantics: a subtree's original
  ids are always free at recreate time, because the counterpart destroy always
  precedes re-allocation (verified by the `EntityIdRecycling` self-test).
- **`SceneSerializer::instantiateFromStringWithIds`** — recreates a serialized
  subtree assigning caller-supplied ids (one per object, same DFS order) instead of
  fresh ones. `CreateSubtreeCommand` / `DeleteSubtreeCommand` capture their ids once
  at first creation and recreate into them on every undo/redo.
- **Deleted:** `EntityRemap`, `EditorCommand::remap` (+ every override),
  `CommandHistory::applyRemap` and its call sites, `buildRemap` / `remapped`, the
  `TransactionEntry` / `CompositeCommand` remap plumbing, and the `RemapEmitter`
  self-test. `undo()` / `redo()` now return `void`. Net: **more deleted than added.**
- Because ids are stable across recreate, `LambdaCommand` (component add/remove)
  capturing raw ids in its closures is now correct by construction — the old
  "not remap-aware" caveat is gone.

#### Phase 14C — Measure, don't assume  (DONE)
14A/14B removed enough restore/remap complexity to leave a stable baseline, so
before building `BinarySnapshotStorage` we *measured* whether it's needed. Opt-in
headless profiler (`SUGAR_BENCH=1` → `src/Benchmarks.h`, `SUGAR_BENCH_ENTITIES=N`
to scale) over a representative device-free scene (transform + rigidbody + collider
+ script + hierarchy): snapshot size, 600-frame ring memory, save time, patch
restore, query, physics step, scheduler overhead. Hot-reload swap latency is
instrumented live instead (`reloadGameModule` logs "N ms swap").

Findings (Release; memory is config-independent, ~636 B/entity/frame, linear):

| Scene | 600-frame ring | Save / frame | Patch restore |
|------:|---------------:|-------------:|--------------:|
| 50    | **18 MiB**     | 0.6 ms       | 1.8 ms        |
| 500   | 182 MiB        | 5.6 ms       | 27 ms         |
| 2000  | 730 MiB        | 25.8 ms      | 138 ms        |

Query 0.017 ms, physics step 0.8 ms (500 ent, O(n²) broadphase), scheduler run
~0 (tracking compiled out in Release). Conclusions:
- **Small scenes (≤~50 entities): JSON is fine** — 18 MiB, sub-2 ms. Binary would
  be premature. (The "18 MB?" hypothesis was right on the nose.)
- **The first thing that breaks is per-frame *save cost*, not memory.** Capture
  runs every fixed step during Play; at 500 entities 5.6 ms is a third of the
  16.6 ms 60 Hz budget, and at 2000 entities (25.8 ms) capture alone exceeds a
  whole frame — so you can't sustain 60 Hz Play *while recording*.
- **Binary/delta snapshots are warranted past a few hundred entities**, and the
  target is encode/decode speed (save + parse) as much as RAM. Delta also cuts
  save cost by encoding only changed components. Revisit `BinarySnapshotStorage`
  (11C) with this as the acceptance criterion — measure the same table again.
- Nothing else needs attention yet: query, scheduler, and physics are all cheap at
  these sizes. *(The physics O(n²) flagged here was addressed next — Phase 15's
  uniform grid.)*

---

## Track C — Catch-up (only after the wedge is real)

### Phase 15 — Physics broadphase: uniform grid  (DONE)  *(Track A follow-up)*
Evidence-driven (see Phase 14C): the profiler flagged the physics step's all-pairs
broadphase as the O(n²) that would dominate first as scenes grow. Replaced it with
a **uniform-grid spatial hash** in `PhysicsWorld` — a solved, non-differentiating
problem kept deliberately simple (not a BVH / SAP / dynamic AABB tree), which suits
SuGar: no persistent state, deterministic, nothing to serialize.
- Each shape is bucketed into the grid cells its AABB overlaps; only shapes sharing
  a cell become candidate pairs (then AABB-rejected before narrowphase). Cell size
  derives from the largest shape, so occupancy stays low. Grid is rebuilt every
  step and owns no state.
- **Deterministic:** candidate pairs are emitted as `a<b` and **sorted**, so
  narrowphase + resolution run in a stable order — actually *more* deterministic
  than the old code, which paired in the collider map's unordered iteration order.
- Narrowphase and impulse resolution are untouched, so contact behavior is
  unchanged; only which pairs reach narrowphase differs.
- Measured (Release, dense touching-box scene — near worst case): **500 entities
  0.81 → 0.40 ms; 2000 entities 1.9 ms** (all-pairs would be ~13 ms at that count).
- Verified by the `PhysicsBroadphase` self-test: exactly the overlapping pairs are
  found (no spurious, no missed) across separated clusters + an isolated body, and
  a repeated step yields identical event order.

### Phase 16+ — Graphics, ecosystem, packaging, platforms
- ~~stb_image (kill WIC / Windows lock-in)~~ (done early). Remaining: full
  cross-platform build (Mac/Linux), glTF PBR pipeline, more lighting, standalone
  game packaging, tests + CI, docs for contributors.

---

## Deferred architecture notes

Small, deliberate "later, not now" items so they aren't lost:

- **`Transform::getWorldMatrix()`** — `Transform` currently owns only its *local*
  matrix; `Registry` walks the hierarchy to compose world matrices
  ([Registry.h](src/ecs/Registry.h) `getWorldMatrix`). Eventually, as `Transform`
  grows richer, it can own more of its own math (cached world matrix, dirty
  flags). Not now — the free-function approach is fine until it isn't.

- Transaction groups (CompositeCommand) for logically grouping multi-step editor operations. 
  Deferred to Track B alongside state/history infrastructure.

- **Physically relocate Core-owned files under `src/core/`** — the Core *library*
  boundary is enforced by CMake/compilation, but the files still live in their
  original folders. Eventually move them into a `src/core/{ecs,math,assets,
  components,...}` tree to make the layer legible on disk. Notably `Material.h`
  is Core (it's data: `AssetHandle` + PBR floats, not rendering) and would sit at
  `src/core/rendering/Material.h` — a `rendering` namespace that means
  "rendering-independent data", not Vulkan. Communicates intent; not technically
  required, so deferred.

## Milestones

- **M1 — "It's a game engine" (end of Track A):** **DONE — Track A complete.**
  Press Play → a behavior-driven entity falls under gravity, hits the ground,
  plays a sound → Stop reverts. Authorable, serializable, reloadable — with a
  full editor on top: select/picking, gizmos, undo/redo, duplicate/delete,
  multi-select, hierarchy reparenting, component add/remove, prefabs, and an
  asset browser. Next: **Track B (the wedge)** — live introspection + hot reload.
- **M2 — "It's *the* iteration engine" (end of Track B):** Edit a behavior's code
  while the game runs and see it apply live with state preserved; scrub time
  backward to inspect what happened. This is the demo that wins indie devs.
  *(Progress: time-travel scrubbing + live component hot-patch shipped in 11B;
  live **code** hot reload with state preserved now works in Phase 12 — rebuild
  the game DLL and it auto-swaps. The full M2 demo is essentially in place.)*
- **M3 — Open-source launch:** M2 + docs + examples + contributor on-ramp.
