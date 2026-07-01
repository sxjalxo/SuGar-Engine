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
  read/write sets. No system reaches into another's internals.
- **Everything round-trips** through serialization. If it can't be snapshotted,
  it can't exist in gameplay state.

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
- Notes / future work: O(n²) broadphase (uniform grid / SAP later); boxes are
  axis-aligned (rotation ignored); physics bodies should be top-level.

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
  - Known limit: a `TransformCommand` recorded against a *duplicated* entity
    won't survive "undo past the duplicate, then redo" — the entity's id is
    reassigned on re-instantiate. Acceptable edge case for a command-based
    history; an id-remapping or snapshot layer is the eventual fix.
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
  per-session id (for introspection / future history serialization);
  `undo`/`redo` return an `EntityRemap` so identity is tracked across recreate.
- **Entity remapping** — `EditorCommand::remap(old→new)`. When a subtree command
  re-instantiates a destroyed subtree (duplicate/delete undo↔redo), it builds an
  old→new id map (zipping serialization order) and the history rewrites every
  other command's stored ids. Fixes "undo past a duplicate, then redo."
- **Command compression** — `EditorCommand::tryMerge`; `push` lets the top entry
  absorb a same-target follow-up (e.g. consecutive `TransformCommand`s on one
  entity) to keep history granular-but-not-noisy.
- Verified by an opt-in self-test (`EditorCommandSelfTest`, run with
  `SUGAR_SELFTEST=1`) covering all three behaviours on a throwaway registry.
- Known limit: `LambdaCommand` (component add/remove) captures ids in closures
  and isn't remap-aware — fine since those entities aren't recreated by other
  commands; revisit if that changes.

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
  JSON every frame (memory: 600 frames x scene size); restoring reassigns entity
  ids, so a scrub clears editor selection; scrubbed edits aren't kept (inspection
  only — a "fork from here" branch is a later nicety).

#### Phase 11C — Snapshot backend abstraction  (NEXT)
The Timeline must not know *how* frames are stored. Introduce an `ISnapshotStorage`
interface behind the ring buffer so the encoding can evolve without touching the
UI or `SuGarApp`'s time-travel logic:
- `JsonSnapshotStorage` — today's full-JSON-per-frame (correct, simple baseline).
- `BinarySnapshotStorage` — compact binary encoding of the same state.
- `DeltaSnapshotStorage` — store frame deltas + periodic keyframes; the big memory
  win (the current 600 x full-scene cost is fine for M2, not for production).
- **Timeline bookmarks** — tag a frame with a label ("physics exploded") and
  jump Previous/Next bookmark. Tiny feature, big debugging usability.

### Phase 12 — Code hot reload  (Pillar 1, the hard one)
- Reloadable **game module**: compile gameplay/behaviors into a hot-swappable unit;
  on reload, migrate state via the serializer (state lives in components, so it survives).
- Reload **only affected systems**, not the whole scene/domain.
- **In-place state restore (patch, don't rebuild).** Scene/snapshot restore today
  is destroy → reload → rebuild, which reassigns entity ids and wipes editor
  selection / inspector / undo history. Move to *patching component data into the
  existing entities* (no entity recreation) so selection, inspector state, editor
  windows, and command history all survive a restore/hot-reload/scrub. This also
  removes the id-reassignment behind the 11A remap machinery and the 11B scrub
  selection loss.
- *This is the headline feature and the hardest. The Phase 6 behavior architecture
  decides whether this is easy or impossible.*

### Phase 13 — Opinionated scheduling & architecture  (Pillars 2 + 4 + 5)
- Systems **declare read/write component sets**; a scheduler orders them and can
  run independent systems in parallel (async-first, Pillar 5).
- **Dependency-aware incremental rebuilds** at system granularity (Pillar 2).
- Architecture **lints / guard rails** that reject hidden-coupling patterns early (Pillar 4).
- *Determinism note: async + time-travel are in tension. Default to a deterministic
  ordered schedule; opt into parallelism per-system where it's provably independent.*

---

## Track C — Catch-up (only after the wedge is real)

### Phase 14+ — Graphics, ecosystem, packaging, platforms
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
  live **code** hot reload with state preserved is Phase 12.)*
- **M3 — Open-source launch:** M2 + docs + examples + contributor on-ramp.
