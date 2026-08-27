# SuGar Engine — Roadmap

The **what** and **when** — milestones and their phases. Vision and positioning live in
**[README.md](README.md)**; the architectural law in **[RULES.md](RULES.md)**; per-dependency
scope in **[REQUIREMENTS_AND_SCOPE.md](REQUIREMENTS_AND_SCOPE.md)**.

**Decision lens for every item:** *does it make developers faster?* A feature earns its place
if it reduces iteration time, improves correctness, removes complexity, or increases
production readiness — otherwise it belongs later.

---

## Runtime layers: Editor, Gizmos, Runtime UI

The engine has three UI-adjacent layers with strictly separate responsibilities.
They must never be mixed.

```
SuGar Engine
│
├── Editor        → Dear ImGui        (developer UI, permanent)
├── Gizmos        → ImGuizmo          (viewport handles, temporary → native)
└── Runtime UI    → RmlUi (planned)   (player UI, HTML/CSS)
```

> ### Current State — the platform's missing half
> SuGar has a **complete developer interface** (Dear ImGui) but **intentionally has
> no player-facing interface**. Dear ImGui is permanently reserved for engine
> tooling and must never render game UI. **Runtime UI begins with RmlUi.**
>
> This is why RmlUi isn't "just another feature" — it completes *half of the
> engine*. Build a game today and you either hack around menus/HUDs or misuse ImGui
> for runtime UI (explicitly forbidden by [RULES.md](RULES.md) Rule 11). Neither is
> acceptable, which is why Runtime UI leads M3.

- **Editor (Dear ImGui)** — hierarchy, inspector, viewport, timeline, query console,
  systems panel, asset browser, profiler, debug windows, play controls. Immediate-mode
  is chosen deliberately: it matches real-time rendering, ECS inspection, hot reload,
  time travel, and live editing. **Not** responsible for runtime game UI.
- **Gizmos (ImGuizmo → native)** — move/rotate/scale, world/local. A *temporary*
  dependency; eventually replaced by an engine-native gizmo (quaternion-first, better
  ECS + Play/Edit + time-travel integration). Long-term quality, **not** a priority.
- **Runtime UI (RmlUi, planned)** — HUD, menus, inventory, dialogue, pause, settings,
  loading, multiplayer UI. Renderer-independent HTML/CSS. **Never depends on ImGui.**

---

## Development principles (roadmap-facing summary)

The authoritative constraints live in **[RULES.md](RULES.md)**; this is the short
version that shapes roadmap decisions:

1. **Runtime state lives in ECS.** Behaviors are disposable; components own data.
2. **Architecture over shortcuts.** Deleting complexity is progress.
3. **Every subsystem gets a deterministic headless self-test.**
4. **Editor tooling is a first-class feature** (live edit, undo, time travel, queries,
   hot reload, profiling).
5. **Rendering and tooling evolve independently** — neither blocks the other.

---

## Milestones

### M1 — Engine Foundation (done)
Vulkan renderer, ECS, editor, asset pipeline, physics, audio, prefabs, serialization.
*(Detail in the appendix.)*

### M2 — Developer Iteration (done)
Time travel, snapshot system, query console, self-tests, native code hot reload,
scheduler + architecture enforcement, in-place restore, stable entity recreation,
uniform-grid physics broadphase, benchmark + stress harnesses. *(Detail in the appendix.)*

### M3 — Engine Platform Complete  (DONE)

> **The platform is complete when a developer can build a typical indie game
> without first extending the engine.**

This is an objective exit criterion, not a vibe — "is M3 done?" should have a
yes/no answer. It is bounded on both sides:

**Required (the platform floor):**

| Capability      | State |
|-----------------|-------|
| Editor          | done |
| Hot Reload      | done |
| Debugging (time travel / query / profiler) | done |
| Physics · Audio · ECS · Rendering | done |
| **Runtime UI (RmlUi)** | done (Phase 16) |
| **Animation** (skeletal, blend trees, state machines) | done (Phase 17) |
| **Navigation** | done (Phase 18) |
| **Asset Pipeline** (maturity: cooking, importers) | done (Phase 19) |
| **Packaging / standalone export** | done (Phase 20) |
| **Build Pipeline** | done (Phase 21) |

**Explicitly *not* required for M3** (so the milestone can't expand forever):

- AAA rendering features (Nanite/Lumen-class)
- Networking / multiplayer
- Console ports
- Massive-world streaming
- Plugin marketplace

**Deliverable at the M3 → M4 hand-off:** publish the Runtime UI design
(`DevDocs/DESIGN_RUNTIME_UI.md` + `DevDocs/RUNTIME_UI_LESSONS.md`) as a standalone article —
integrating a retained-mode UI library (RmlUi) with an ECS/deterministic gameplay model.
Validate against a real game first.

### M4 — Dogfood: build real games (ACTIVE)

Games are probes, not products — engine work is driven by what a game forces, never
speculation. Every forced change is recorded in the **M4 friction log** below.

**Rules**
- Add an engine feature only when **a game forces it** (can't proceed, or is genuinely
  miserable, without it) — never because another engine has it.
- Forbidden until forced (rabbit holes): scripting language, networking, ECS rewrite,
  asset-db redesign, renderer rewrite, plugin architecture, reflection, prefab overhaul,
  job-system rewrite. If forced, log which game forced it and why first.
- A game = `scene.json` + `assets/` + a `Game.dll` of behaviours (built against
  `SuGarCore`), booted by `SUGAR_GAME=<dir>`, shipped by `SUGAR_PACKAGE`.
- Games live **outside** the repo, in `E:\Sugar Engine - Games\Level {1,2,3}\`; only
  *engine* changes a game forces land in this repo.
- Acceptance per game: plays; ships as a standalone that runs with no source tree
  (`Packager::verify` green); `SUGAR_VALIDATE` stays green; friction log captures every
  forced change.

**Level 1 — Tiny (ergonomics baseline). COMPLETE.**
- Games: Pong, Breakout, Flappy Bird, Asteroids — all playable + shipped as standalones.
- Result: 10 engine boundary features (all forced by Pong), zero architecture rewrites,
  gate held 38/38. Write-up: `E:\Sugar Engine - Games\Level 1\Report.md`; forced changes in
  the friction log below.
- Open: ~~#11 no flat-colour `Material` tint~~ (RESOLVED in L2); ~~`box-shadow` broken in the
  RmlUi backend~~ (RESOLVED by #14, the RmlUi effects compositor — see friction log); no
  camera-visible-bounds query (2D games hand-fit their field — caused the fixed Breakout
  side-escape bug).
- **HUD polish (content, no engine change):** all four HUDs upgraded from plaintext to a
  shared RCSS look — rounded translucent chips with a caption + big value, per-game accent
  colour, accent message text, soft box-shadow (via #14). Element ids the behaviours write are
  preserved; all four repackaged, `verify` OK.

**Level 2 — Small (scale + content). COMPLETE.**
- Top-down shooter: DONE (forced #11 flat-colour material + #12 mouse input; else reused the
  L1 surface).
- Content pass: Asteroids (L1) retrofitted with a sprite pack → forced #13, the transparency
  **blend-mode seam** (Opaque/Masked/Translucent/Additive; Rule 22).
- 2D platformer: DONE (single-screen). First game to use gravity + dynamic-vs-static resolution;
  ground checks via `PhysicsQuery::raycast`. **Forced zero engine changes.**
- Survivors-like: DONE. Hundreds of pooled chasing enemies. Forced **one** fix — a time-travel
  **snapshot-capture policy** (#15): full-scene JSON per fixed step collapsed 1000-entity scenes
  to 6 fps; budget-gating capture (and disabling it in packaged builds) restored ~420 fps. The
  sim itself scales (headless: 3000 enemies = 5.2 ms/step). See friction log.
- Between the shooter and the platformer, a **review-driven hardening pass** landed confirmed
  correctness/safety fixes + three small physics capabilities the games demonstrated (collision
  layers/masks, trigger colliders, `PhysicsQuery::raycast`). Gate **40 → 47/47**.
- Write-up: `E:\Sugar Engine - Games\Level 2\Report.md`.

**Level 3 — Core game mechanics, at real-game scale. IN PROGRESS.**

L3 is not one game. It is the tier that asks whether the **core mechanics a game is built
from** — world representation, persistence, navigation and AI, streaming, combat, UI,
audio, animation — can each be built on the engine as it stands. A game is finished when it
has answered that for its mechanics; the *tier* finishes when the mechanics stop producing
new answers. First game: the voxel/Minecraft-like, **done**. Next: a combat arena, chosen by
the platform audit below, because animation, audio and collision have never been driven by a
game.

**L3 game 1 — voxel / Minecraft-like. DONE.**
- First slice runs: first-person, a chunked voxel world, gravity + game-side voxel collision,
  raycast break/place. Forced **three architectural seams** — each designed as a record first
  (`DevDocs/DESIGN_RUNTIME_MESH.md`, the `AssetGateway` + `CameraComponent` designs), never
  "add a function":
  - **Camera as a component** (`CameraComponent`, Core): a game drives the view by placing it
    on an entity; the renderer reads that entity's world transform (pose derived, Rule 21b).
    Editor orbit camera unchanged when absent.
  - **Asset-acquire seam** (`AssetGateway`, Core): the symmetric *acquire* half of the existing
    `onReleaseAsset` release hook — a Core-only game turns an asset key into an increfed handle
    (`acquireMesh/Texture/AudioClip`) without seeing ResourceManager. Killed the illegal
    handle-clone place-block did.
  - **Runtime-mesh seam** (`AssetGateway::createMesh(RuntimeMeshData)`): game-generated vertices
    → a derived `runtime://` GPU mesh (non-source: never scanned/cooked/packaged/serialized).
    Only `string`+`AssetHandle`+POD cross the boundary; the engine copies + uploads and owns
    the lifetime.
- One engine defect found + fixed: an all-runtime-spawned scene built zero texture descriptors
  at load → first chunk threw `texture descriptor set was not created`; the renderer now lazily
  rebuilds descriptors when the draw list references a never-seen texture.
- **Measurement decided the deferred perf debate with evidence** (chunk-as-entity vs per-block,
  25 600-block world): entities 25 600 → **102**, draws 25 600 → **100**, FPS 1.2 → **445**,
  snapshot 410 ms → **under budget**. Conclusion: **C24 instancing, A4 storage rewrite, and a
  binary/delta snapshot backend are all NOT forced** — the wrong *representation* was the wall,
  not any subsystem. The game forced a chunk representation; it was not a decision to add one.
- Also driven: the review-driven fix pass (16 confirmed defects — shadow-NaN, DrawList sort UB,
  scene non-finite floats, entity id-gap OOM, audio voice-steal/OOB, determinism, hot-reload
  use-after-free, `SaveData` caps, …) and a `SUGAR_FPSLOG` measurement overlay.
- **Second arc — the full game** (biomes, mobs, UI, lighting, particles, packaging), which is
  what put M4's remaining target features under load. Forced **five** more seams, each designed
  first: **#20** per-texture sampler filter as an import setting (the block atlas),
  **#21 `GameDataComponent`** — the A3 gap, forced by mobs (`DevDocs/DESIGN_GAME_DATA.md`),
  **#22 lights as components** with Directional/Point/Ambient, derived pose, ≤8 selection and
  range falloff (`DevDocs/DESIGN_LIGHTING.md`), **#23 `UIElementStateComponent`** (a HUD needs
  classes and inline style, not only text), **#24 cursor capture** as a request the engine
  grants only in Play. Gate **48 → 52/52**, Debug + Release (+`GameData`, `Lighting`, `NavLinks`, `WorldLabels`).
  - **Navigation held up, with one instructive failure.** A first navmesh built from flat
    per-column quads plus vertical "bridges" had 10 318 of 27 310 triangles rejected as too
    steep — a navmesh welds by vertex, so every one-block step was a cliff. Rebuilt game-side as
    a smoothed heightfield: 9 879 of 9 880 triangles accepted; at 128 wide, 13 767 polygons in
    **73.5 ms** (Release). 24 agents all `following`; hostile mobs kill the player. **Off-mesh
    links (step-up / jump) are the real gap** — recorded, unforced.
  - **Particles need no engine system yet:** a 96-entity pool of cube meshes with velocity and
    lifetime in game data does break bursts and torch embers at 443 FPS. The price is one draw
    call per pooled particle — the argument for instancing, when a game needs thousands.
  - **Packaging bug found only by shipping:** the package walk starts from scenes, and a game
    that acquires assets *by key* at runtime (chunks are created after load) shipped **zero**
    cooked assets. An external game now packages everything its asset folder scanned;
    `verify: OK`, and the standalone runs the full game at **471 FPS**.
  - **Serialization split two ways:** per-entity gameplay state in `GameDataComponent` (so it
    snapshots), the world's *edits* in the game's own `SaveData` (terrain is `f(seed, dim)` and
    is regenerated, so a save is hundreds of bytes rather than 750 KB). Verified across
    restarts. Cost: the snapshot policy now pauses time travel in this scene (13.3 ms > 4 ms) —
    a JSON snapshot per fixed step does not survive a particle pool.
- **The full mob taxonomy went in with ZERO engine changes** (2026-08-11): passive (6 kinds),
  neutral (5, anger-driven), hostile (5, undead burn at noon), a boss with a HUD boss bar and
  minion summons, tamed pets that follow and sit, 15 villager professions x 7 biome outfits with
  job-site blocks and a trade panel, and "similar entities" (armour stands, dropped items with no
  AI and no navmesh agent). One data table, one behaviour switched on the category. Every piece
  landed on seams the earlier arcs built — `GameDataComponent`, `NavAgentComponent` (55 agents
  planning at once), `UIElementStateComponent` (boss bar width, trade panel visibility) and the
  instanced draw path (~330 entities, 92 submitted draws). Release: **340 FPS**, packaged
  standalone **461 FPS**, `nav[... following=55 unreachable=0]`. *That a whole feature area needed
  no engine work is the M4 threshold's actual test, and it passed.* Noted, unforced: the engine has
  **no world-space text**, so mob nameplates are not expressible (the game uses a centre-screen
  prompt).
- Write-up: `E:\Sugar Engine - Games\Level 3\Report.md`.
- **"HUD clipped under display scaling" was NOT an engine bug** — it was the screenshot
  method. On a 125 % display a DPI-unaware capture process gets a *logical* 1536x792 rect and
  `PrintWindow` then copies only that corner of the physical 1920x991 window; sizing the bitmap
  from `GetClientRect` compounded it, since `PrintWindow` draws the whole window (title bar
  included) and pushes the content down. Made the capture per-monitor-DPI-aware and
  window-rect-sized: crosshair sits at exactly 50 %/50 %, and the hotbar, health bar and status
  panel are all fully on screen in the editor **and** in the packaged standalone. Instrumenting
  the extents (`swapChainExtent` / `viewportExtent` / ImGui display size, all consistent at
  1920x991) is what ruled the engine out — the same instrument-don't-guess loop the earlier
  measurement work used. Game-side, `#hotbar` gained an explicit height so an absolutely
  positioned row anchored by `bottom` cannot depend on content height.
- **Third arc — streaming, and the shift from feature-testing to failure-hunting** (2026-08-12/13).
  The world went from 128 voxels that all existed at once to **512x512 with a player-centred 7x7
  chunk residency**, which is what turned the runtime-mesh seam from "create once" into a real
  lifecycle. That plus four hostile workloads produced **seven engine defects (#30–#36)**, all
  found by the game rather than by review:
  - **#30** navmesh `VertexWelder` keyed its spatial hash on a formatted `std::string` in a
    `std::map` — 104–115 ms of a 130 ms bake. Cell triple in an `unordered_map`: 9.7–11 ms.
  - **#31** `findNearestPolygon` / `findContainingPolygon` were linear scans, so resolving 39
    link endpoints against 18 000 polygons cost 24 ms *per bake* — and the same scan runs twice
    per path request. Derived XZ `lookupGrid`: 2.78 ms.
  - **#32** navigation never noticed player edits (streaming rebaked, a block edit did not), so
    mobs walked through anything the player built. Fixed game-side with a debounced rebake; the
    engine gap (tiled navmesh, dirty-tile rebuild) is named and **not** built.
  - **#33** the welder probed 27 cells because its cells were exactly `weldEpsilon`; cells of
    2·ε make an 8-cell probe provably exact. Weld 9.57 → 3.37 ms.
  - **#34** runtime-mesh upload was 47 % of a chunk crossing. Decomposed first (`MeshUploadProfile`,
    `SUGAR_UPLOADLOG=1`): 91 % was Vulkan object churn and queue stalls; the memcpy of 1.17 GB was
    **1.6 %**. Reused staging buffer + `DeviceMemoryPool` suballocation: **1.05 → 0.27 ms/mesh**,
    `vkAllocateMemory` **20 740 → 2**. The batched-submit/fence change was deliberately **not**
    made — `createMesh` still means "renderable when it returns".
  - **#35** `DrawList` gathered zero-scaled entities: 16 000 *parked* pooled particles cost ~17 ms
    a frame drawing nothing. Skipping them: 60 → 126 FPS.
  - **#36** `setDestination` reset `status` unconditionally, so an agent already following threw
    its path away and ran A\* again every tick — 58 agents took a 243 FPS scene to **0.89**.
    Fixed; the failed-replan **backoff** is a policy question and stays deferred.
  - Net: a chunk crossing went **60 → 19.6 ms**. Gate **52 → 56/56**.
- **What the streaming lifecycle proved (no defect):** 3 507 chunk loads / 7 659 runtime meshes
  created and released with resident chunks pinned at 49 and live meshes equal to live chunk
  entities on *every* sample; 96 long-distance teleports (4 395 loads) and 2 318 forced remeshes
  with no drift; a Debug run under the validation layers over 5 093 mesh creates **clean**. The
  `AssetGateway → ResourceManager → runtime mesh → entity destroy` chain holds under churn.
- **Persistence proved too:** a deterministic serpentine over the whole world, verified by a
  *second process* walking the identical path — 5 996/5 996 edits intact through eviction and
  regeneration, and 197 272 edits (2.5 MB) load at 413 FPS. Eight hostile save files (truncated,
  random bytes, empty, bad block id, wrong seed) never crashed; one real defect found —
  out-of-range coordinates were accepted and `editKey` *aliased* them (y packed in 16 bits:
  99 999 → 34 463), so a corrupt save moved an edit to a different voxel. Rejected at both doors.
- **Off-mesh links validated end to end, engine unchanged:** a moat too wide to jump is
  Unreachable without links and Success with exactly one link step, one path segment crosses open
  water, and the agent actually swims it; a one-way drop is Success downhill, Unreachable uphill,
  and Unreachable again with links removed. The swim rule is the *game's*. **Census: 0 of 120
  ordinary routes use a link** — natural terrain does not need them, only the constructed island
  does, which is the honest answer to whether the feature earns its place.
- Open, not fixed (engine limitations, not defects — features when a game forces them): tiled
  navmesh dirty-rebuild; failed-replan backoff; batched-submit upload; greedy face-merge; depth
  occlusion for world labels. Each has a measurement attached and none has a workload demanding it.

**L3 game 2 — combat arena. NEXT.** Third-person arena, 5–10 enemies, melee + ranged,
projectiles, a boss, hit reactions, audio, save/load, packaged. Deliberately small and
deliberately *orthogonal* to the voxel game: it is chosen not for what it adds to a game
library but for which engine surface it drives. `DevDocs/PLATFORM_AUDIT.md` found that
**animation/skinning, audio and collision are implemented, self-tested, and have never been
used by a game** — a projectile needs a collider, a hit needs a sound, a swing needs a clip.
The question it answers is the one under all of M4: *can SuGar carry a materially different
game without being rewritten for it?*

### Level 4 — Advanced systems and quality (NOT STARTED)

**L3 and L4 are two branches of M4, not two phases of it.** They are separated by the
*kind of question a game asks*, not by the order the games arrive in:

- **L3 asks "can SuGar build serious games?"** — capability. A game that cannot express a
  mechanic at all is an L3 problem.
- **L4 asks "can SuGar run serious games to production standards?"** — quality, scale and
  platform sophistication. A game that renders correctly at 1080p and misses frame time at
  4K is an L4 problem, and it is one *whether or not L3 has run out of questions*.

So **L4 does not wait for L3 to finish.** A workload opens it, exactly as a workload opens
anything else here. The L4 column:

- rendering quality beyond the current forward path (post-processing, better materials/PBR,
  GI or its approximations, shadow quality);
- resolution and display integration — high-DPI and 4K, dynamic resolution, and vendor
  features (FreeSync / G-Sync, DLSS / FSR, frame generation);
- advanced optimization: GPU-driven culling, async upload/compute, multithreaded scene
  submission, streaming budgets;
- whatever the L3 games measured and deliberately deferred (tiled navmesh rebuild,
  batched-submit upload, async chunk generation) if a workload finally forces it.

The same rule holds across the boundary: **a game forces it, or it waits.** L4 items are
allowed to be motivated by measurement rather than by a blocked game — a frame that is too
slow at 4K is a real forcing function — but never by a feature list. DLSS is the clearest
case: it goes in when a measured workload shows the rendering path needs another
performance/quality mechanism, and not because modern engines have one.

---

## M4 friction log

Append-only. One entry per forced change. Format:

> **[game] — [what was blocked]**
> *Forced:* what the game could not do. *Change:* smallest engine edit that unblocked it.
> *Verdict:* fix now / defer / workaround. *Ref:* files / self-test pinning it.

**Pong (L1) — #1 can't boot an external game scene.** *Forced:* games live outside the repo
(`E:\Sugar Engine - Games\...`); the engine hardcoded a demo scene in `initScene()` and only
loaded `scene.json` via the F9 hotkey, so there was no way to *run a game*. *Change:*
`SUGAR_GAME=<dir>` → boot `<dir>/scene.json` and scan `<dir>/assets` instead of the demo; no
chdir, so engine resources (shaders, fonts, DLL) stay exe/repo-anchored while only game
*content* moves. Worked cleanly because `AssetPath` anchors every key at the `assets/`
segment, so an absolute game-asset path spells the same key a scene references. *Verdict:*
fixed. *Ref:* `SuGarApp::run/initScene`, `SuGarApp.h` `gameDirectory`.

**Pong (L1) — #2 no camera for a game.** *Forced:* the demo uses an ORBIT camera around a
scene entity; a booted game has no such target and Pong is 2D. *Change:* `Renderer::
setCameraPose`; on game boot, a FREE camera at `(0,0,12)` looking down −Z frames the XY play
plane (the default yaw −90° already faces −Z). *Verdict:* fixed for now; **scene-authored
cameras** are the real answer and are deferred until a game needs a non-default framing.
*Ref:* `Renderer::setCameraPose`, `SuGarApp::initRenderer`.

**Pong (L1) — #3 game behaviours can't live outside the repo.** *Forced:* Pong's
`PaddlePlayer`/`PaddleAI`/`BallController` are game code; the repo's `SuGarGame` is the wrong
home (games stay external). *Change:* `GameModuleLoader::load(name, directory)` — a booted
game loads its own `Game.dll` from `<gameDir>` instead of `SuGarGame`; the DLL is built
against `SuGarCore` by the game's own CMake, and `SuGarCore.dll` still resolves from the exe
dir (always on the Windows loader path). *Verdict:* fixed — the real test of the
Editor→Engine→Core layering, and it held. *Ref:* `GameModuleLoader`, `SuGarApp::initScene`,
`E:\...\Pong\CMakeLists.txt`.

**Pong (L1) — #4 no save data.** *Forced:* high score must survive a run; scene serialize
is the wrong tool. *Change:* `SaveData` (Core, `key=value` file, `SaveData` self-test in the
gate); engine points it at `<gameDir>/save.dat` on boot. *Verdict:* fixed. *Ref:*
`src/core/SaveData.{h,cpp}`, `testSaveData`.

**Pong (L1) — #6 game booted paused in the editor.** *Forced:* behaviours only run in Play;
a launched game should just run. *Change:* auto-enter Play on the first frame when
`SUGAR_GAME` is set (also what makes headless verification possible). *Verdict:* fixed.
*Ref:* `SuGarApp::mainLoop` / `play()`.

**Pong (L1) — #7 no game-authored UI / no way to push text to it.** *Forced:* an on-screen
score needs the game's own RmlUi document and a way for a behaviour (Core-only) to write
text into it; the runtime UI hardcoded the engine's demo HUD (doc path, font, name/tag
sync). *Change:* (a) `RuntimeUIView` loads `<SUGAR_GAME>/assets/ui/hud.rml` when a game is
booted; (b) a new `UILabelComponent{element,text}` — a read-only ECS→element-text primitive
(distinct from editable `TextInputComponent`), synced by `RuntimeUIView`. Pong's
`BallController` creates label entities and writes the score; the game's `hud.rml` displays
them. *Verdict:* fixed — the general HUD hook, not a Pong special-case. *Ref:*
`UIComponents.h`, `Registry`/`ComponentAccess` (+UILabel), `RuntimeUIView::syncLabelsFromEcs`.

**Pong (L1) — #8 a "standalone" wasn't runnable.** *Forced:* packaging shipped cooked
assets + scene + binaries but NOT the shaders, runtime-UI font, or the game's UI docs, and
skipped the game's own `Game.dll`. *Change:* `Packager::Spec::extraFiles` (loose files copied
to an explicit relative path) + the `SUGAR_PACKAGE` step (now `SUGAR_GAME`-aware) ships
`build/shaders/*.spv`, the font, `assets/ui/*.rml`, and `<gameDir>/Game.dll`; scenes from an
absolute path land flat at the package root. *Verdict:* fixed. *Ref:* `Packager`,
`SuGarApp` package branch.

**Pong (L1) — #9 packaged mode ran the demo, not the game.** *Forced:* a shipped build has
no `SUGAR_GAME`; boot only loaded a scene file / applied the 2D camera + auto-play when
`SUGAR_GAME` was set, so a standalone showed the built-in demo. *Change:* unified boot on a
`runningGame` flag — load `scene.json` (and load `Game.dll` from beside the exe, apply the
game camera + auto-play) whenever a game scene is present, whether external (editor) or
packaged (manifest + `scene.json` beside the exe). *Verdict:* fixed. *Ref:*
`SuGarApp::initScene/initRenderer/mainLoop`.

**Editor UI (dogfood, workflow) — restyle + default layout.** *Found:* dogfooding made the
editor's look/ergonomics obvious problems — default `StyleColorsDark` + bitmap font, and
panels opened as a floating, overlapping pile (no default dock layout; stale `imgui.ini`).
Exactly the *tooling* polish M4 was expected to surface, not an engine gap. *Change:*
`src/editor/EditorTheme.h` — one flat near-black neutral dark theme (inspired by an ImGui
reference + the Claude dark UI): `WindowBg ~#0A0A0B`, subtle borders, soft rounding, a single
blue accent for selection; Lato TTF (16px) replaces the bitmap font; a one-time DockBuilder
default layout under a fresh dockspace id (left: Hierarchy/Editor/Timeline/Inspector; centre:
Viewport; right: Systems / Query+Navigation; bottom: Play Controls+Assets), so the editor
opens organised. *Ref:* `EditorTheme.h`, `Renderer::initImGui/buildEditorUi`.

**Pong (L1) — #10 a packaged standalone showed the editor chrome.** *Forced:* a shipped game
(packaged, no `SUGAR_GAME`) rendered the Hierarchy/Inspector/Systems panels over the game; a
shipped game must be viewport-only + HUD. *Change:* `Renderer::setGameView(bool)` — set true
for a packaged standalone (`AssetCooker::hasManifest()`); when on, `buildEditorUi` skips the
dockspace + every panel + the gizmo/pick, and draws the Viewport fullscreen-borderless (the
runtime HUD still overlays). An editor-run game (`SUGAR_GAME`, no manifest) keeps full chrome
for debugging. *Verdict:* fixed. *Ref:* `Renderer::buildEditorUi`, `SuGarApp::initRenderer`.

**Pong: DONE.** Playable (Up/Down player, ball-tracking AI, wall/paddle reflection with
english + speed ramp), on-screen score + persisted high score, and a **verified runnable
standalone** (`<gameDir>/dist`, runs with no source tree / no repo). Engine gate held at
**38/38** throughout. Friction #5 (a real quad/sprite mesh) never bit — a flattened box
sufficed. First L1 game complete; the engine grew nine boundary features, zero architecture
rewrites — the pattern M4 was meant to produce.

### Level 2 — Small (scale + content). In progress.

**Top-Down Shooter (L2) — #11 no flat-colour material tint.** *Forced:* the game has three
entity kinds on screen at once (player, enemies, bullets); with `Material` albedo being a
texture handle only, every one shared `builtin://checkerboard` and was unreadable — you
could not tell player from enemy from projectile. Asteroids dodged this (everything was a
"rock") but a shooter cannot. This was the #11 item L1 left open, now forced. *Change:* a
flat-colour tint multiplied into the sampled albedo. `Material::baseColor` (glm::vec3,
default white → texture unchanged); pushed to the shader via the existing per-object push
constant (added `vec4 baseColor`, 96 B total, under the 128 B floor) in `basic.vert/frag`
and `skinned.vert`; new `builtin://white` 1×1 texture so `white × tint` gives a solid flat
colour; serialized as an optional `material.baseColor` (absent ⇒ white, so every pre-tint
scene still loads); editor Inspector `ColorEdit3`. No cook change (material lives in the
scene, not the `.sgc`). *Verdict:* fixed. *Ref:* `rendering/Material.h`,
`BasicTrianglePass.cpp` `ObjectPushConstants`, `shaders/basic.*`, `ResourceManager`
`WhiteTextureId`, `SceneSerializer` (write/read/apply/snapshot), Renderer Inspector,
`SelfTests.h` golden. Gate 38/38.

**Top-Down Shooter (L2) — #12 no mouse input.** *Forced:* the canonical shooter aims with
the mouse, but `InputActions`/`Input` were keyboard-only — no buttons, no cursor position, no
way to turn the cursor into a world point. (The game shipped a keyboard auto-fire first, but
mouse aim is the natural control and user-directed as needed by many future 2D/3D games.)
*Change:* three layers, each minimal. **Core `Input`:** mouse-button state (down +
pressed-this-frame, mirroring keys), a `MouseRay{origin,direction}` the engine writes each
frame, and `getMouseWorldOnPlane(point, normal)` — pure ray-plane math, no camera. Stores the
**ray, not a world point**: the point is derived against whatever plane the game cares about
(Z=0 today, Y=0 or terrain tomorrow), so it isn't state (Rule 21b). **Core `InputActions`:**
one flat code space — mouse buttons are codes ≥ `MouseButtonBase` (512; GLFW keys end at
348), so `bindAction("Fire", MouseLeft)` and `bindAction("Fire", Space)` compose with no
special-casing. **Engine:** `SuGarApp` wires the GLFW mouse-button callback; `Renderer`
factors its existing pick-ray unprojection into `cameraRayThroughPixel` and pushes the
cursor's world ray to `Input` each viewport frame (viewport-local pixel coords → correct in
editor panel *and* fullscreen shipped game; same camera the pass renders). *Verdict:* fixed.
*Ref:* `core/Input.{h,cpp}` (`MouseRay`, buttons, `getMouseWorldOnPlane`),
`core/InputActions.{h,cpp}` (`MouseButtonBase`/`MouseLeft`, `codeDown`/`codePressed`),
`SuGarApp` button callback, `Renderer` `cameraRayThroughPixel` + per-frame `setMouseRay`,
`SelfTests.h` `testMouseInput` (headless: binding composition + ray-plane cases). Gate 39/39.

**Top-Down Shooter: DONE.** Playable **true twin-stick** — arrow-key movement, **mouse aim +
left-click to fire** (bullets go to the cursor's world point on the play plane), with a
keyboard/auto-target fallback under headless autoplay. Enemy AI chases the player, a
spawn-rate difficulty ramp, player health + game-over/restart, readable green/red/yellow via
#11. Enemy + bullet pools authored in `scene.json` (L1 pooling, reused unchanged). Verified:
editor Play (colours + **bullets fire toward the cursor**, confirmed by driving the OS
cursor), autoplay loop (deterministic, monotonic score, difficulty ramp), packaged standalone
(`Packager::verify` OK). **Two engine changes forced (#11 flat colour, #12 mouse input);
everything else reused the L1 surface.** *Open (not yet forced):* camera-visible-bounds query
(2D games still hand-fit their field).

### Content pass — Asteroids (L1) gets a real sprite pack

**Asteroids (L1) — #13 no transparent sprites → the transparency seam (Rule 22).** *Forced:*
a free 2D art pack (Pixel SHMUP, RGBA PNGs) replaced the checkerboard boxes with real
ship/rock/enemy sprites. Two things had to hold: (a) a mesh with real UVs — the pooled
entities used `block.obj`, whose UVs are unused, so a sprite mapped to one texel; (b) the
transparent PNG background must not render as an opaque block.

(a) is **no engine change** — the OBJ loader already parses `vt`, so a hand-authored
`quad.obj` (XY plane, +Z normal, correct UVs) textures 1:1; a game asset, not an engine one.

(b) began as a one-line alpha cutout (`discard` in `basic.frag`), but that is exactly the
symptom-fix **Rule 22** warns against: smoke, glass, ghosts, particles and UI fades all force
alpha *blending*, and a hardcoded discard would make the single opaque pipeline and the
unsorted draw list a rewrite the day blending arrives. So the fix is the seam both Unreal
(`EBlendMode`) and Unity URP (Surface Type + Alpha Clip + blend mode, render queue forcing
opaque→transparent, transparent sorted back-to-front) converge on: a per-material
**`BlendMode { Opaque, Masked, Translucent, Additive }`** the renderer buckets and sorts on.
- `rendering/Material.h` — the enum + `Material::blendMode` (default Opaque, so every existing
  material is unchanged); `isBlended()` helper.
- `shaders/basic.frag` — reads the mode via the push constant (`blendMode` packed into the
  old padding slot, push constant still 96 B): Masked discards `< 0.5`; Translucent outputs
  texel alpha; Additive premultiplies. `basic.vert`/`skinned.vert` match the layout.
- `BasicTrianglePass` — one pipeline per blend bucket (Opaque/Masked share bucket 0; Translucent
  and Additive get blend state + no depth write), built from one parameterised loop for both
  static and skinned. `scenePipelines[skinned][bucket]`; the draw loop selects by material.
  Blended pipelines keep **destination** alpha (`srcAlpha 0, dstAlpha 1`) because the scene
  renders to an offscreen image ImGui composites by that alpha — writing a cutout's own alpha
  there punched holes the panel showed through (caught in testing, fixed).
- `scene/DrawList.cpp` — the render-queue order: opaque/masked first, then the translucent/
  additive tail sorted **back-to-front** by camera distance (`Renderer::getCameraWorldPosition`
  feeds it). Cutout becomes just `Masked`.
- `SceneSerializer` (optional `"blendMode"` string, absent ⇒ Opaque, back-compat), editor
  Inspector combo, and a headless `BlendMode` self-test (reader accepts every mode + garbage →
  Opaque + a pre-field scene). Default-Opaque write is pinned by the serializer golden.
*Verdict:* fixed — all four modes render (verified live: masked sprites, plus a temporary
additive+translucent asteroid to prove blending composites, then reverted to masked). *Ref:*
files above; game-side `quad.obj` + `assets/textures/*.png` + `scene.json`. **Gate 39 → 40/40**
(+`BlendMode`), Debug + Release. Packaged standalone: 11 cooked assets, `verify` OK, boots
from `dist`. *Deferred (Rule 8, not yet forced):* premultiplied-alpha and per-particle soft
blending, and blended shadow-casting — the seam takes them without a rewrite.

**HUDs (L1) — #14 no RmlUi effects (`box-shadow` rendered as a white quad).** *Forced:* polishing
the four HUDs wanted soft drop-shadows, but the custom RmlUi Vulkan backend implemented only
core geometry/texture/scissor — none of RmlUi 6's layer/filter path (`PushLayer`,
`CompositeLayers`, `SaveLayerAsTexture`, `CompileFilter`). box-shadow's internal white mask
geometry leaked straight to screen and its shadow texture resolved to the fallback white
texture (even RmlUi's *own* reference Vulkan backend never shipped these — only GL3 did).
*Change (per Rule 22 — build the compositor **seam**, not the whole effect catalogue):*
- **Frame graph:** the UI now renders in its **own pass after the scene pass** (`Renderer::
  createUiLayerPass`), not appended to it — so the compositor can open/close offscreen passes
  a still-open scene pass would forbid. The scene pass hands the image off in
  COLOR_ATTACHMENT; the UI pass LOADs it and `endFrame` barriers it to SHADER_READ for ImGui.
  With no effects the output is pixel-identical (verified). Vulkan stays SuGar-owned; RmlUi is
  purely a consumer.
- **Compositor** (`RmlVulkanRenderer`): offscreen colour-layer pool + an offscreen render pass;
  `PushLayer`/`PopLayer` open/clear/close layers; `CompositeLayers` draws a fullscreen triangle
  sampling one layer onto another through a **single-pass Gaussian blur** (`rml_composite.frag`,
  the box-shadow "blur" filter) or a plain copy; `SaveLayerAsTexture` copies the layer's scissor
  region into a **persistent** image (RmlUi caches shadow textures across frames); `CompileFilter`
  parses `blur`'s sigma. Lazy pass-open + a "current target" stack handle the interleaving.
- **Scoped (Rule 8):** stencil **clip masks are a deliberate no-op** — box-shadow only needs
  them to hide a shadow under a *translucent* element; opaque elements (the common case, all
  the HUD chips) cover it. A documented, forced-later gap, not a silent one.
*Verdict:* fixed — box-shadow renders correct soft offset shadows across all four HUDs (no
white box, no validation errors); the headless RmlUi smoke test now carries a box-shadow to
guard the effect codepath every launch. *Ref:* `Renderer::createUiLayerPass`,
`BasicTrianglePass` (UI moved after scene pass; scene finalLayout), `RmlVulkanRenderer`
(layer pool, offscreen pass, composite/blur pipeline, effect overrides), `shaders/rml_fullscreen.vert`
+ `rml_composite.frag`, `RuntimeUIView::smokeTest`. **Gate 40/40**, Debug + Release. All four
HUDs repackaged, `verify` OK. *Deferred (not yet forced):* stencil clip masks (translucent-
element shadows), separable/downsampled blur (perf for large sigma), non-blur RCSS filters.

---

**Review-driven hardening pass (between the shooter and the platformer).** *Found:* a
game-developer review of the frozen platform (write-up outside the repo at
`C:\Users\Sujal\Projects\SuGarEngine_Review_2026-08-09.md`) surfaced confirmed
correctness/safety issues and a few small capabilities the L2 games had already demonstrated a
need for. Per the dogfood discipline, only *confirmed* defects + *demonstrated* capabilities
were fixed — no speculative features. *Change:* (bugs/safety) the Script tick moved to Core
`ScriptSystem::run`, iterating a **sorted id snapshot** so behavior order is deterministic and a
behavior may spawn/destroy entities mid-tick without iterator-invalidation UB; `SaveData::save`
is now **atomic** (temp + rename); `Registry::reset` releases handles by resource-owner (a
mesh/audio entity with no `Transform` no longer leaks); collision events carry a **real contact
point**; `AudioEngine` caps voices; the renderer warns once past the skinned-draw cap.
(capabilities) `ColliderComponent` gained **`isTrigger`** (sensor) + **`layer`/`mask`**
filtering; **`PhysicsQuery::raycast`** (Core, ray vs box/sphere, layer-filtered) landed for
ground/hitscan/LOS/picking; `Registry::destroyEntityTree` cascades a subtree; `Mesh::makeUnitCube`
backs a **`builtin://cube`** fallback so a missing mesh can't fail a scene load. *Deferred with
rationale (not silently carried):* **generational entity handles** — a genuine aliasing bug, but
the only clean fix is an ECS-identity rewrite (undo's `createEntityWithId` does `Entity`
arithmetic), so it waits for a game to actually hit the aliasing. *Verdict:* fixed + tested —
gate **40 → 46/46**, +`ScriptSystem`/`ColliderFilter`/`Raycast`/`ContactPoint`/`DestroyEntityTree`/
`BuiltinCubeMesh`. *Ref:* `scene/ScriptSystem`, `physics/PhysicsQuery`, `physics/PhysicsComponents`
+ `PhysicsWorld`, `ecs/Registry`, `core/SaveData`, `rendering/Mesh`, `assets/ResourceManager`,
`scene/SceneSerializer`, `audio/AudioEngine`, `BasicTrianglePass`.

---

**2D Platformer (L2) — forced nothing.** *Found:* the first game to use real gravity + dynamic-
vs-static box resolution (the shooter used static, no-gravity bodies). *Change:* **none.**
Gravity + stable resting contact + jump (`velocity.y`) + ground checks via a short downward
`PhysicsQuery::raycast` from below the feet all worked on the existing surface; meshes are the
built-in cube, colours flat-tint. The strongest possible dogfood signal — a whole game category
that costs the engine nothing. *Ref:* game is external (`E:\Sugar Engine - Games\Level 2\Platformer`).

---

**Survivors-like (L2) — #15 time-travel snapshot cost collapses large scenes.** *Found:*
hundreds of pooled enemies chasing the player ran at **6 fps** at 1000 entities. Measured (not
guessed): a headless Core harness showed the **sim scales fine** (3000 chasing+colliding enemies
= 5.2 ms/step — the `unordered_map` ECS storage is *not* the wall), and timing the loop pinned
the cost to **`captureSnapshot()`** — the time-travel ring serializes the *whole scene* to JSON
every fixed step (~160 ms/frame at 1000 entities; the render was ~1 ms). *Change:* the smallest
correct seam, **not** a backend rewrite — a Core `SnapshotCapturePolicy` gates *when* capture
happens: full per-step capture while a scene fits a 4 ms/step budget, sticky-paused (with a
Timeline reason) once a capture blows it, and off entirely in packaged builds (no Timeline to
consume it). The ring + `ISnapshotStorage` format are untouched, so a future binary/delta backend
still drops in. *Verdict:* fixed — **6 → ~420 fps** at 1000 entities; gate **46 → 47/47**
(+`SnapshotPolicy`). *Deferred:* the binary/delta snapshot backend itself (the review's #44)
stays future work; the policy makes it unnecessary for now. *Ref:* `core/SnapshotCapturePolicy`,
`SuGarApp::captureSnapshotBudgeted`, `Renderer::drawTimelinePanel`. Also logged (not forced): no
gameplay-facing asset-handle incref (→ authored pooling over runtime handle-clone), no in-game
FPS overlay.

**Minecraft (L3) — #16 a game can't drive the camera.** *Forced:* first-person is impossible —
the engine camera was an editor orbit/free rig with no way for game code (Core-only) to set the
view; the engine had even friction-logged "scene-authored cameras are the eventual replacement".
*Change:* Core `CameraComponent {fovDegrees, nearPlane, farPlane, active}` (authoritative lens
only — the eye *pose* is derived from the entity's world transform each frame, Rule 21b, no
second owner); renderer gained `CameraMode::SCRIPTED` + `setScriptedCamera`;
`updateCameraTargets` drives the view from the lowest-id active camera entity. Serialized as an
optional `camera` block; absent ⇒ the orbit camera (back-compat). *Verdict:* fixed — the
Unity/Godot "camera is a component" model, game-forced. *Ref:* `rendering/CameraComponent.h`,
`Renderer::setScriptedCamera`, `SceneSerializer` camera block.

**Minecraft (L3) — #17 a game can't acquire an asset handle.** *Forced:* a Core-only behaviour
can't ask the engine-side ResourceManager for a mesh/texture handle, so runtime block placement
had *cloned* an existing entity's handle without increfing it — a refcount-underflow bug.
*Change:* `AssetGateway` (Core) — the symmetric *acquire* half of the existing `onReleaseAsset`
release hook. The engine installs a backend wired to `ResourceManager::load*` (dedup + incref);
game calls `acquireMesh/Texture/AudioClip(key)`. Only `string`+`AssetHandle` cross; ownership
stays in ResourceManager; acquire/destroy balance. *Verdict:* fixed. *Ref:* `assets/AssetGateway`,
self-test `AssetGateway`.

**Minecraft (L3) — #18 a game can't create a mesh at runtime.** *Forced:* a chunked voxel world
needs one mesh generated *from voxel data*, not loaded from a file — the per-block-entity
alternative measured 1.2 FPS at 25 600 blocks. *Change:* `AssetGateway::createMesh(RuntimeMeshData)`
→ `ResourceManager::createRuntimeMesh` (validate → copy into the engine vertex format → upload →
synthetic `runtime://mesh/<id>` key). Designed first (`DevDocs/DESIGN_RUNTIME_MESH.md`): CPU data is
the caller's (engine copies before return, retains no pointer); `runtime://` is a derived,
non-source resource (excluded from cook/package, not serialized — rebuilt from voxels on load,
Rule 21a); runtime-key release idles the device (re-mesh swap safe). *Verdict:* fixed — chunk
representation took 25 600 draws → **100**, 1.2 FPS → **445 FPS**, and *restored* time-travel
(snapshot back under budget). It proved **C24/A4/binary-snapshot are all NOT forced** — the
representation was the wall. *Ref:* `assets/RuntimeMeshData.h`, `ResourceManager::createRuntimeMesh`,
`SceneSerializer::collectAssetKeys` (runtime:// skip).

**Minecraft (L3) — #19 runtime-spawned renderables had no descriptor set.** *Forced:* a scene
whose renderables are *all* spawned at runtime built texture descriptors for **zero** textures at
load, so the first chunk's material threw `texture descriptor set was not created`. *Change:*
`Renderer::drawFrame` now detects a draw-list texture with no descriptor and rebuilds the sets
(device-idle-safe), so any runtime-spawned renderable works without the game or app knowing.
*Verdict:* fixed. *Ref:* `Renderer::drawFrame`, `collectDrawListTextures`.

**Minecraft (L3) — #20 an atlas cannot be sampled crisply (no per-texture filter).** *Forced:*
block textures live in one atlas so a chunk stays one draw; the global `VK_FILTER_LINEAR` sampler
both blurred the pixel art and **bled neighbouring atlas tiles into each other**, which no UV inset
fully hides. *Change:* `filter: nearest|linear` as an **import setting** (`AssetSettings::
TextureFilter`), baked into the cooked artifact (`CookedTexture::filter`, format version 1→2,
`AssetHash::CookerVersion` 1→2) because a packaged runtime has no `.meta` to read; `Texture::
createFromPixels` takes `pointFilter`. Same placement Unity (Filter Mode) and Unreal (Texture
Filter) use — a property of the image, not of one material. *Verdict:* fixed, +test case in
`AssetImport`. *Ref:* `AssetCooker::cookTexture`, `CookedAsset`, `ResourceManager::loadTexture`.

**Minecraft (L3) — #21 a game cannot define a component (the A3 gap) → `GameDataComponent`.**
*Forced:* mobs. One `Behavior` instance ticks every entity that names it, so a per-mob health /
cooldown / kind has nowhere to live; the player's velocity and hotbar slot had already been
smuggled into module globals, which do not survive snapshot restore. *Change:* designed first in
**`DevDocs/DESIGN_GAME_DATA.md`**, then one engine-owned component whose *contents* the game owns —
`GameDataComponent{ map<string, number|string> }`, serialized as a plain JSON object, snapshotted
free (a snapshot *is* the serializer), shown and edited in the Inspector. The engine never reads a
value. Reflection / game-registered types stay unbuilt (Rule 8: nothing needed *types*, it needed a
place to put six floats). *Verdict:* fixed; the whole L3 game now keeps its state there. Gate
49/49 (+`GameData`). *Ref:* `src/ecs/GameData.h`, `ensureGameData`, `SceneSerializer`.

**Minecraft (L3) — #22 lights were unreachable, positional-only, and capped at 4 → lights as
components.** *Forced:* a day-night cycle and placed torches. Lights were a scene-level
`std::vector<Light>` in the engine layer (a Core-only behaviour cannot touch it), every light was a
point light with **no falloff**, ambient was a `0.12` constant compiled into the shader, and
`MAX_LIGHTS` was 4. *Change:* designed first in **`DevDocs/DESIGN_LIGHTING.md`** — Core
`LightComponent{type,color,intensity,range,castsShadow,active}` with **position and direction
derived from the entity's world transform** (the CameraComponent precedent, Rule 21b);
`LightType{Directional,Point,Ambient}`; the draw list gathers scene lights + light entities and
selects ≤ `MAX_LIGHTS` (shadow-casting directional first, then points nearest the camera);
`MAX_LIGHTS` 4→8; the shader gained the `w == 0` directional convention, `clamp(1-d/range)²`
falloff and a UBO ambient term; the shadow pass synthesises a caster position for a directional
light. Range 0 means unlimited, so every pre-seam scene lights exactly as before (the golden
serializer test still passes byte-for-byte). *Verdict:* fixed; the game now runs a sun, a moon, a
sky term and per-torch point lights from a behaviour. Gate 50/50 (+`Lighting`). *Ref:*
`rendering/LightComponent.h`, `scene/Light.h`, `DrawList.cpp`, `BasicTrianglePass.cpp`,
`shaders/basic.frag`.

**Minecraft (L3) — #23 a HUD can only set text → `UIElementStateComponent`.** *Forced:* a hotbar's
selected slot, a health bar's fill and an inventory panel's visibility. `UILabelComponent` writes
inner text and nothing else, so the alternatives were generating RML markup from gameplay code
(styling leaks out of the RCSS) or a bespoke engine widget per HUD. *Change:* a second UI
component — `{element, classes, style}` — the view syncs the element to exactly those classes and
inline declarations, removing ones it applied before and no longer sees (classes written in the
document itself are untouched). Same split as HTML's `class`/`style`. *Verdict:* fixed; the L3 HUD
is pure RCSS plus state. *Ref:* `ui/UIComponents.h`, `RuntimeUIView::syncElementStatesFromEcs`.

**Minecraft (L3) — #24 first-person look ran out of screen (no cursor capture).** *Forced:* mouse
look stops at the desktop edge without a captured cursor. *Change:* Core `Input::setCursorCaptured`
— the game *requests*, the engine grants it only while in Play and always releases on Stop, so a
game cannot trap the cursor in the editor; GLFW is applied engine-side (Rule 15). *Verdict:* fixed.
*Ref:* `core/Input`, `SuGarApp::mainLoop`.

**Minecraft (L3) — #25 navigation had no off-mesh links.** *Forced:* a mob below a two-block
ledge or across a trench plans and gets `Unreachable`, correctly — a navmesh welds by vertex, so
two surfaces that share no corner are separate islands. *Change:* designed first (addendum in
`DevDocs/DESIGN_NAVIGATION.md`), then `NavMesh::links` — a `NavLink{start, end, cost, bidirectional}`
whose **endpoint polygons are derived** by `buildAdjacency` (the same argument `neighbors` makes:
an asset must not carry a stale resolution). A* expands links as one more edge kind, after the
shared ones and in index order, so determinism is untouched; the funnel splits its corridor at a
link and emits the two endpoints as waypoints (Detour's treatment). No new component, no new agent
status. *Verdict:* fixed, `NavLinks` self-test (unreachable → reachable, one-way, determinism,
corridor link steps). **Measured in the game and reported honestly: this terrain barely uses them** —
13 links on a 96² world, 0 of 120 sampled routes crossing one, because what fragments the walkable
surface here is *water* (33 components, spawn stuck in a 1 582-polygon pocket), not ledges. The
capability is real and tested; the game's next step is a swim rule, not more engine work.

**Minecraft (L3) — #26 snapshot capture was dominated by number formatting.** *Found:* the
particle pool pushed a 245-entity scene to 13.3 ms/step, past the 4 ms budget, and the policy
paused time travel. *Measured first* (new `SUGAR_BENCH` metrics `snapshot_save_gamedata`,
`ostream_float_writes`): **2.63 ms of a 3.32 ms capture was `operator<<(float)`** — ~350 ns per
number, for 7 500 numbers. *Change:* `std::to_chars` with `general`/6 significant digits (and /15
for game-data numbers) — the same digits the stream produced, so **every scene file and the golden
test stay byte-identical**; this is a speed change, not a format change. *Verdict:* fixed. Save
3.32 → **1.99 ms** (plain) and 4.45 → **2.60 ms** (with game data); in-game capture 13.3 → **7.7 ms**.
A binary/delta `ISnapshotStorage` backend is still **not** forced — the text writer was simply
paying 350 ns a number.

**Minecraft (L3) — #27 a pooled particle was a draw call → instanced draws.** *Forced + measured:*
particle counts 96 / 1 000 / 4 000 gave 411 / 397 / **173** FPS — a pooled particle costs a draw
call whether it is alive or not. *Change:* consecutive draw-list items that agree on mesh, texture,
material and blend mode — and are unskinned — collapse into ONE `vkCmdDrawIndexed`, with model
matrix and base colour arriving from a per-frame **instance vertex buffer**. Two new pipelines
(`basic_instanced.vert`, `shadow_instanced.vert`; the shadow pass had to batch too or the win would
be halved), base colour moved from a push constant to a varying so one fragment shader serves both
paths, and a batch of one takes the original path unchanged. *Verdict:* fixed. 4 000 particles
173 → **232 FPS**; 10 000 particles run at 88 FPS. The editor's "Draw calls" now reports **submitted**
draws rather than item count, because with batching those stopped being the same number.

**Minecraft (L3) — #28 RmlUi textures were freed while in flight.** *Found by the validation layer*
while measuring #27: *"vkFreeDescriptorSets(): pDescriptorSets[0] can't be called on VkDescriptorSet
… that is currently in use by VkCommandBuffer …"* — RmlUi drops a cached shadow/layer texture on
every re-layout, which for a HUD whose text changes is every frame. *Change:* retire textures the
way the renderer already retires geometry (`retiredTextures` + `collectRetiredTextures`, freed after
the frames-in-flight margin) — one policy for both, not two. *Verdict:* fixed; validation output is
clean over a 28 s run.

**Minecraft (L3) — #29 no world-space text → `WorldLabelComponent`.** *Forced:* mob nameplates.
The game has 60+ creatures of a dozen kinds and 15 villager professions, and the HUD can only
describe whatever the crosshair is on — identifying anything meant aiming at it, one at a time.
*Change:* designed first (addendum in `DevDocs/DESIGN_RUNTIME_UI.md`), then **UI anchored to a world
point**, not glyph geometry in the scene: Core `WorldLabelComponent{text, offsetY, maxDistance}`
whose anchor is the entity's own transform, plus pure `ScreenProjection::project` (Core, headless-
testable) for the one piece of real math. The renderer projects, culls behind-camera / past-range /
off-screen, keeps the nearest 32 and hands the view a flat list; `RuntimeUIView` drives a fixed
pool of `<span class="worldlabel">` elements — text and inline `left`/`top`/`opacity` only, no
markup rebuilt per frame — inside a `#worldlabels` container the document opts into. Same reasoning
Unity's Screen-Space-Camera canvas and Unreal's widget components use: a nameplate wants the
document's font and styling and must stay legible, not perspective-correct. *Verdict:* fixed;
`WorldLabels` self-test pins the round-trip **and** that a point behind the camera is rejected
rather than mirrored to a plausible on-screen position. Gate **51 → 52/52**. Not built: depth
occlusion (labels show through walls), per-label styling from the game, world-space rotation.
*Ref:* `ui/UIComponents.h`, `rendering/ScreenProjection.h`, `Renderer::updateWorldLabels`,
`RuntimeUIView::syncWorldLabels`.

**Minecraft (L3) — #30 the navmesh vertex welder cost 104 ms per bake.** *Found by chunk
streaming:* the game moved to a 512×512 world with a player-centred 7×7 chunk residency, which makes
the walkable surface change — and the navmesh rebake — every time the player crosses a chunk
boundary. Measured on the game's own timeline: a rebake was **110–139 ms**, of which the game's
triangle generation was 1.2–1.5 ms, its off-mesh links 0.9 ms, and engine `buildNavMesh` **104–115
ms**. *Cause:* `VertexWelder` keyed its spatial hash on a **formatted `std::string`** (`to_string(x)
+ "," + …`) in a **`std::map`** — 27 string allocations and 27 red-black-tree walks per welded
corner, ~75 000 corners per bake. The comment justified the ordered map as making the bake
reproducible, but determinism never came from the container: the probe order is the fixed dx/dy/dz
loop and each bucket is insertion-ordered. *Change:* key the buckets on the exact cell triple in an
`unordered_map` with a splitmix64-per-axis hash. Same bake, same output. *Verdict:* fixed —
`buildNavMesh` **104–115 ms → 9.7–11 ms** in the game, and the new `navmesh_bake_112` benchmark
(25 088 triangles, the size a 7×7 chunk radius produces) reports **13.9 ms** in Release so the cost
stays measured instead of remembered. Gate **52/52**. *Ref:* `navigation/NavMeshBuilder.cpp`,
`Benchmarks.h`.

**Minecraft (L3) — streaming lifetime probe (no defect found).** The point of chunk streaming was
to put `AssetGateway::createMesh` under create → render → release → recreate pressure instead of the
create-once path it had. Measured over 120 boundary crossings in the packaged standalone: **3 507
chunk loads, 3 458 unloads, 7 659 runtime meshes created and 7 608 released**, with resident chunks
pinned at 49, `liveMeshes` equal to the live chunk-entity count on *every* sample (51 or 56, the
difference being chunks with water), entity count oscillating 267–314 with no trend, and 301 MB
private bytes at the end. A Debug run with the validation layers over 371 loads / 777 mesh creates
produced **zero validation messages** — no stale descriptor sets, no double frees, no leaked
handles. Persistence was checked in the same loop: **2 107 edits, all 2 073 that fell in resident
chunks intact** after their chunks had been evicted and regenerated, and still intact after a full
process restart. *Verdict:* the runtime-mesh lifetime seam holds; what streaming exposed was the
navmesh cost above. *Still open, measured not fixed:* a chunk crossing is a **~40 ms hitch**
(streaming ~22 ms + rebake ~18 ms, worst 67 ms) because generation, meshing and the rebake are all
synchronous. That is the honest case for an asynchronous generation seam — and it is not built,
because the number should decide it, not the anticipation of it.

**Minecraft (L3) — #31 navmesh point queries were linear scans.** *Found by the crossing
breakdown:* after #30 the rebake was still 36 ms, and only 12.6 ms of it was accounted for
(triangles 1.3, build 10.2, links 0.9). The missing **24 ms was `registerNavMesh`** — which calls
`buildAdjacency`, which resolves every off-mesh link endpoint through `findNearestPolygon`. With
**39 links and 18 000 polygons** that is 78 linear scans of the whole mesh, each testing every
polygon edge. The same scan runs twice per *path request* (start + destination snapping), so the
cost was on gameplay too, not only on the bake. *Change:* `NavMesh::lookupGrid`, an XZ uniform grid
of polygon indices — **derived**, built by `buildAdjacency` beside the neighbour table, never
stored in an asset, so it cannot go stale. `findContainingPolygon` reads one cell (a point can only
be inside a polygon whose bounds cover its cell); `findNearestPolygon` walks expanding rings and
stops only when the nearest possible point of the next ring cannot beat the best found — visiting
candidates in ascending polygon index so the documented lowest-index tie-break is unchanged.
*Verdict:* fixed — `register` **24.07 → 2.78 ms**, and the crossing total **60 → 40 ms**. New
`NavLookupGrid` self-test asserts *equivalence*: 400 sampled points (inside, in a hole, far
outside) must give the grid and the still-compiled scan fallback the same polygon and the same
projected point. Gate **52 → 53/53**. *Ref:* `navigation/NavMesh.{h,cpp}`, `SelfTests.h`.

**Minecraft (L3) — where a chunk crossing's 40 ms actually goes.** Instrumented per phase in the
packaged standalone (7×7 residency, ~18 000 navmesh polygons, 26 crossings):

| phase | avg ms | what it is |
| --- | --- | --- |
| runtime mesh upload | 16.8 | `AssetGateway::createMesh` for the ~7 new chunks (engine) |
| navmesh build | 11.2 | `buildNavMesh` weld + adjacency (engine) |
| evict | 3.1 | destroy entities, free voxels (engine + game) |
| navmesh register | 2.8 | second adjacency + link resolve (engine) |
| voxel meshing | 2.4 | culled meshing, CPU (game) |
| navmesh triangles + links | 2.2 | walkable surface harvest (game) |
| torch lights | 1.2 | derived light rebuild (game) |
| **terrain generation** | **0.31** | noise + features + edit replay (game) |

The result names the seam that a fix would need, and it is **not** asynchronous terrain generation:
generation is 0.8% of the crossing. It is **GPU upload (42%) and navmesh rebuild (41%)**. Two
questions follow, both deliberately left open until a workload forces them: whether runtime-mesh
upload should be staged/batched across frames, and whether navigation should rebake synchronously
with streaming at all (a tiled navmesh would rebuild only the chunks that changed). *Verdict:*
measured, documented, not built.

**Minecraft (L3) — #32 navigation did not notice the world changing.** *Forced:* building a wall
is the game. *Reproduced* with a probe that asks the planner the same question three times
(`SUGAR_VOXEL_NAVTEST=1`):

```
before wall:            Success       polys=17716
after wall, no rebake:  Success       blocksPlaced=144   <- stale; mobs walk through it
after debounced rebake: Unreachable   bakeMs=12.8
cost of rebaking per block edit: 11.94 ms; 144 blocks would cost 1718 ms
```

Chunk streaming rebakes; a player edit did not, so anything a player built was invisible to
navigation. *Classification:* the correctness half is a **game** bug (the game never told
navigation), and the reason it could not simply fix it is an **engine capability gap** — a navmesh
can only be replaced wholesale, so "one block changed" costs a full bake. *Change (game):*
`markNavDirty` at the break/place sites plus `updateNavRebake`, a **debounce** — one bake 0.25 s
after the last edit of a burst, since mining is a hold and a throttle would bake mid-burst and
again at the end. *Change (engine): none* — the seam that would fix it properly is a **tiled
navmesh** (bake per tile, stitch at borders, rebuild only dirty tiles — the shape Recast/Detour's
`addTile`/`removeTile` and Unreal's dirty-area rebuild both take), and a 12 ms coalesced bake does
not yet force it. *Verdict:* correctness fixed, cost documented, seam named and not built.

**Minecraft (L3) — #33 the navmesh welder probed 27 cells when 8 suffice.** *Found by decomposing
the bake* that #32 made a per-edit cost: weld **9.57 ms** of a 13.5 ms bake, adjacency 3.50 ms.
*Cause:* the spatial hash used cells of exactly `weldEpsilon`, and at that size a corner can weld
with one two cells away, so correctness needed the full 3×3×3 probe. *Change:* size cells at
**2·epsilon**; then per axis only one neighbour can hold a point within epsilon (offset < ε reaches
back, offset > cellSize − ε reaches forward, and both cannot hold at once), so a 2×2×2 probe is
exact. *Verdict:* fixed — weld **9.57 → 3.37 ms**, whole bake **13.5 → 7.5 ms** (benchmark), and
in the game the bake went **16.3 → 12.0 ms** and a chunk crossing **40 → 35.7 ms**. New
`NavWeldProbe` self-test puts corner pairs *exactly on* cell boundaries at 0.5ε, 1ε and 4ε
separation — the straddling case a too-small probe silently misses — and repeats them mid-cell.
Gate **53 → 54/54**. *Ref:* `navigation/NavMeshBuilder.cpp`, `SelfTests.h`, `Benchmarks.h`
(`navmesh_bake_weld` / `navmesh_bake_adjacency` now report the split).

**Minecraft (L3) — where runtime-mesh upload's 16.9 ms goes (measurement, no fix).** Upload was
47% of a chunk crossing and had never been decomposed. `MeshUploadProfile` now splits it at the
boundaries where a fix would land, and prints on `SUGAR_UPLOADLOG=1`. Over a streaming run —
**5 184 meshes, 10 370 buffers, 1.17 GB, 1.05 ms per mesh**:

| phase | ms | share | what it is |
| --- | --- | --- | --- |
| submitWait | 2175 | **40%** | `vkQueueSubmit` + `vkQueueWaitIdle`, once per buffer |
| bufferCreate | 1531 | **28%** | `vkCreateBuffer` + `vkAllocateMemory` + bind, 4 per mesh |
| destroy | 1249 | **23%** | tearing the staging buffer down again |
| translate | 250 | 4.6% | copy into the engine `Vertex` layout |
| command | 128 | 2.4% | allocate/record/free the copy command buffer |
| mapCopy | 86 | 1.6% | `vkMapMemory` + `memcpy` + unmap |
| validate | 14 | 0.3% | index-range and length checks |

**91% is Vulkan object churn and queue stalls; moving the actual 1.17 GB is 1.6%.** Each mesh
allocates four buffers (staging + device, for vertices and indices), stalls the queue twice
(`vkQueueWaitIdle` per buffer — a crossing does 28 full queue waits), then frees two of them
again. 20 740 `vkAllocateMemory` calls over the run; this driver reports
`maxMemoryAllocationCount = 4294967295` so there is no hard ceiling *here*, but drivers that
report 4096 exist and a per-buffer allocation is the pattern Vulkan documents against.

*So the seam is not an asynchronous upload architecture.* Three ordinary fixes are available and
the measurement says roughly what each is worth: a **reused staging buffer** (up to 51%, the
bufferCreate + destroy columns), **one submit and one fence wait per batch** instead of per buffer
(the 40% column, at the cost of a mesh not being renderable until the fence signals), and
**suballocated device memory**. *Verdict:* measured and documented; nothing built, because which
of the three to do is a design decision and the numbers now exist to make it. *Ref:*
`rendering/MeshUploadProfile.{h,cpp}`, `rendering/Mesh.cpp`, `assets/ResourceManager.cpp`.

**Minecraft (L3) — #34 runtime-mesh upload allocated a buffer per buffer.** *Forced by the
measurement above*, and fixed in the two places it said to, each measured on its own against the
identical streaming workload (5 184 meshes, 10 370 buffers, 1.17 GB):

| | before | + staging reuse | + suballocation |
| --- | --- | --- | --- |
| upload total | 5434 ms | 3610 ms | **1400 ms** |
| per mesh | 1.05 ms | 0.70 ms | **0.27 ms** |
| bufferCreate | 1531 ms | 970 ms | **19 ms** |
| staging destroy | 1249 ms | **0 ms** | 0 ms |
| submitWait | 2175 ms | 2208 ms | 1021 ms |
| `vkAllocateMemory` | 20 740 | 10 371 | **2** |

**Reused staging buffer** (`Mesh.cpp`): one host-visible buffer, grown geometrically, never
shrunk. Safe *because the copy is synchronous* — `copyBuffer` ends in `vkQueueWaitIdle`, so the GPU
is provably done reading before the next upload refills it. That invariant is written at the
declaration, because batching the submit later would turn this into a use-after-free and it must be
impossible to make that change without reading why.

**Device-memory suballocation** (`rendering/DeviceMemoryPool.{h,cpp}`): device-local *buffer* memory
comes out of 32 MiB blocks with a first-fit free list and coalescing on release; requests over half
a block get their own. Scope is deliberately one lifetime — staging (reused, host-visible) and
images (`bufferImageGranularity`) stay out, because one abstraction over three lifetimes is harder
to debug than three plain ones and only this one had a measurement behind it. Ownership is
unchanged: the pool owns blocks and bookkeeping, a `Mesh` still owns its `VkBuffer`s and hands its
placement back on destroy, `ResourceManager` remains the resource owner.

*Not done:* the fence/batched-submit change. `createMesh` still means "renderable when it returns",
and turning that into create → pending → usable is a lifetime state machine, not an optimization.
`submitWait` is now 73% of what is left of a much smaller number.

*Verdict:* fixed. Chunk crossing **35.7 → 19.6 ms**, its upload phase **16.9 → 3.5 ms**, eviction
3.06 → 0.10 ms. After 10 370 buffers the pool holds **one 32 MiB block, 9 MiB live, 124
allocations** — no fragmentation growth. A Debug run with the validation layers over 5 093 mesh
creates and 5 042 releases is **clean**, which is the check that matters when buffers start sharing
memory at an offset. New `DeviceMemoryPool` self-test drives the placement bookkeeping headless —
alignment honoured, padding not lost, no two live placements overlapping, no two free runs left
touching (a missed merge is how a pool fragments to death), and a full free/alloc cycle returning
the block as exactly one run. Gate **54 → 55/55**. *Ref:* `rendering/DeviceMemoryPool.{h,cpp}`,
`rendering/Mesh.{h,cpp}`, `SelfTests.h`.

**Minecraft (L3) — off-mesh links validated against gameplay (no engine change).** `NavLinks`
proves the engine's semantics; it cannot answer whether a real game needs the capability or drives
it correctly. `SUGAR_VOXEL_SWIMTEST=1` walks the whole chain — *world geometry → navmesh → NavLink
→ A\* → funnel → NavAgent → actual movement*:

```
1 ground:               Success  waypoints=3  linkSteps=0
2a water, no links:     Unreachable  moatBlocks=704
2b water, with links:   Success  linkSteps=1  swimLinks=30  totalLinks=43
2c funnel:              segmentsCrossingWater=1 of 5
3a plateau:             topY=17 sideY=14 linksNear=20
3  drop:                high->low Success (links=1), low->high Unreachable (links=0)
3c links removed:       high->low Unreachable
3b determinism:         identical
4  traversal:           distanceToIsland=2.8  CROSSED
```

The gameplay rule added is **game-side**: `buildNavLinks` now emits a *swim* link across up to 12
cells of open water — every intermediate column must be water and not merely unwalkable, because a
chasm and a lake are both unwalkable and only one is swimmable. Cost is 6×span so a mob that can
walk around a pond still does. Swimming itself needed no code: the agent walks its waypoints, and
nothing stops a mob entering water. **The engine's job was the connection, and it did it.**

*Verdict:* capability validated end to end, engine unchanged. **But the census says the world does
not need it:** 119/120 sampled routes reachable and **0 of them cross a link**, with 43 links
present. Off-mesh links matter here only where the game deliberately builds an island — which is
the honest answer to "does this feature earn its place", and worth more than a green test would be.

*Three defects, all in the probe, all found by running it:* a 1×1 pillar is too small to become a
navmesh polygon (both directions "succeeded" by snapping to the ground beside it); link endpoints
are on-mesh by construction so counting waypoints *over* water always yields zero (the segment
between waypoints is what crosses); and a passive mob **wanders**, which sets a destination — the
first traversal run watched a cow replace its errand with a graze and reported a failure that was
not one. A probe is code, and gets the same suspicion as the code it probes.

**Minecraft (L3) — #35 the draw list gathered entities that draw nothing.** *Found by the particle
stress:* a saturating spawner (`SUGAR_VOXEL_PARTICLESTRESS=N`) against a swept pool
(`SUGAR_VOXEL_PARTICLES`) in the packaged standalone. Two defects fell out, one on each side.

*Game side, first:* the pool recycled by **scanning** for a dead slot — O(pool) per spawn, fine at
96 and fatal at 4 000. Measured: 4 000 particles at 8 000 spawns/s ran at **1.5 FPS**, and the same
4 000 particles at 100 spawns/s ran at **180**. The cost was never the rendering. Replaced with a
free-slot stack (park hands its index back), O(1) both ways.

*Engine side:* with 16 000 pooled particles **all parked** — zero scale, nothing on screen — the
frame still cost ~17 ms (**60 FPS showing nothing**). `DrawList` resolved a world matrix, copied a
material, built a sort key and an instance-batch entry for every one of them. A pooled system sizes
its pool for the peak and runs mostly empty, so the cost was proportional to the pool rather than to
what is visible. *Change:* skip zero-scaled entities during the gather — free correctness rather
than a heuristic, since there is no scale at which a zero-extent mesh becomes visible. **60 → 126
FPS** with 16 000 parked, `items` 16 179 → 178.

*And the number that says the renderer is fine:* across the whole sweep, **`drawCalls` stayed at
118 while `items` went from 277 to 16 181** — instancing collapses the batch exactly as intended,
and the remaining cost is per-entity CPU, not submission. Saturated (every particle live):

| pool | FPS | items | drawCalls |
| --- | --- | --- | --- |
| 96 | 425 | 277 | 118 |
| 1 000 | 342 | 1 181 | 118 |
| 4 000 | 124 | 4 181 | 118 |
| 8 000 | 40 | 8 181 | 118 |
| 16 000 | 2.9 | 16 181 | 118 |

*Verdict:* both fixed; **~4 000 simultaneously live particles is the budget** on this machine, and
what limits it is per-entity work (the game's per-particle `GameDataComponent` lookups and the
engine's per-item gather), not draw submission. No particle system added to the engine — the game
still builds one out of transforms, materials and runtime meshes, which was the question this
module existed to answer. `SUGAR_FPSLOG` now reports **items and drawCalls separately**, because a
run that sees only the first cannot tell whether batching happened. Gate **55/55**. *Ref:*
`scene/DrawList.cpp`, `Renderer::submittedDrawCalls`, game `Particles.cpp`.

**Minecraft (L3) — adversarial stress pass.** Four hostile workloads against seams that had only
ever seen polite ones, plus eight deliberately broken save files.

*Save/load torture — nothing crashed.* Truncated mid-record, 4 KiB of `/dev/urandom`, a line with
no `=`, an empty file, a block id past `BlockCount`, and a mismatched seed all loaded or were
rejected cleanly, with no crash dump in any run. A **200 000-edit save (2.5 MB)** loaded 197 272
unique edits and ran at **413 FPS** — the edit representation is fine at a scale no player will
reach. *One real defect:* out-of-range coordinates were **accepted**. `editKey` packs y into 16
bits and z into 24, so a save claiming `y=99999` came back as `y=34463` — a corrupt file would not
merely carry junk, it would move an edit to a *different voxel*. Now rejected at both doors
(`recordEdit` and `loadEdits`).

*Streaming / runtime-resource torture — clean.* 96 random long-distance teleports (every resident
chunk evicted and a fresh set generated per jump): **4 395 loads / 4 346 unloads**, resident pinned
at 49, `liveMeshes` equal to live chunk entities on every sample. Zigzag across one seam: 952
loads / 903 unloads, same. Forced remesh of the same chunk **2 318 times** with the terrain
unchanged — pure runtime-mesh create/release churn — no drift in live meshes or entities.

**#36 a failed plan is retried at full cost, every frame, per agent.** *Found by the navigation
torture:* 58 agents given one shared destination each tick while a wall around it is built and
removed. With the wall open, 52 agents follow at **243 FPS**; the moment it closes every agent goes
Unreachable, gameplay re-issues, and each re-issue costs a **full A\* over the whole reachable
component** — **0.89 FPS**, oscillating with the wall. *Two causes, one fixed:* `setDestination`
reset `status` to Idle **unconditionally**, so even a successfully-following agent threw its path
away and replanned every tick — that is now a no-op when the target is unchanged and the agent is
already Following, while Unreachable and Arrived still re-arm so the documented retry survives
(`NavAgentReissue` self-test pins all five cases; gate **55 → 56/56**). *What remains is a named
seam, not built:* there is **no backoff between failed replans**, so N agents chasing something
behind a closed door cost N full searches per frame. Both Unity and Unreal throttle repathing; the
decision here is whether that policy belongs in `NavAgentComponent` (a minimum interval between
failures) or in the game, and it wants a design record rather than a quick constant.

*And one defect in the harness, worth recording because it nearly became a false finding:* the wall
toggled on a **wall-clock** gate, which is correct until the thing being measured slows the frame
past the interval — at which point "every second" silently became "every frame" and the harness was
measuring itself. Frame-counted now.

**Platform audit (DevDocs/PLATFORM_AUDIT.md).** Not a feature checklist — five questions per
subsystem: is there a *seam*, has a *real game* driven it, has it survived a *hostile* workload,
is there an *unresolved architectural decision*, and would widening it *now* avoid a likely
rewrite. Anything a game has not driven is marked **unproven**, not green.

*The finding:* **animation/skinning, audio and collision are implemented, self-tested, and have
never been used by a game.** Across all three dogfood games there is not one
`ColliderComponent`, `AnimatorComponent`, `SkinnedMeshComponent` or `AudioSourceComponent`;
L1 uses `RigidBodyComponent` in six places and that is the whole of it. Not under-tested —
*unused*, and their only tests are written against the shape the code already has.

*One item earns "widen now":* **generational entity ids**. An `Entity` is a bare integer that
is written into save files and game data, so widening it later migrates every artifact that
stored one. Everything else in the matrix is better decided by the next game.

*Two decisions stay deferred with their numbers attached:* tiled navmesh rebuild, and
failed-replan backoff. *Verdict:* **no critical missing seam** — so the next move is the
orthogonal combat game rather than more engine work, because a projectile needs a collider, a
hit needs a sound and a swing needs a clip, which is precisely the unproven column.

**Minecraft (L3) — measurement tooling.** *Forced (soft):* the per-block vs chunk decision needed
numbers, and the windowed app has no capturable FPS. *Change:* opt-in `SUGAR_FPSLOG=1` prints
FPS + drawn-entity + draw count to stderr each second (the L2-noted missing profiler overlay).
*Ref:* `SuGarApp::mainLoop`.

### Level 3, game 2 — the combat arena (the audit's orthogonal game)

*Built to drive the three subsystems the platform audit found **unused**: animation/skinning,
audio, collision. A 36x36 arena, a skinned fighter with a melee swing and a thrown bolt, waves
of skinned navmesh agents, trigger-volume pickups, an RmlUi HUD. Report + numbers:
`E:\Sugar Engine - Games\Level 3\CombatArena\Report.md`. Gate **56 -> 57/57** Debug + Release.*

**The headline is what did *not* happen: this is the first L3 game that forced no new engine
seam.** It forced four bug fixes and nothing else — a voxel streamer and a melee arena share
nothing but the engine, and the engine took the second one unchanged.

**#37 a scene outside the working directory silently loses every clip and skin.** *Found by the
arena's first boot:* the fighter rendered, textured, in bind pose, and nothing anywhere said why.
`ModelImporter::ensureModelAssets` opened the model at **the raw path from the asset key**, and a
key is a *name* anchored at the `assets/` segment, not a working-directory path — every other
asset type resolves through the catalog. For an external game (`SUGAR_GAME`, which is how all
eight dogfood games are built) the file is elsewhere, the load threw, and `catch (...)` swallowed
it. The components round-trip perfectly and the character never moves. The game settled it by
asking the Core registries directly: `skin=0 clipIdle=0 clipDie=0` before, `1 1 1` after.
*Change:* `AssetCooker::sourcePath(key)` — the catalog lookup every cooked type already used
internally, made public for the one consumer that reads a *source* file directly. Keys are still
built from the key spelling (a registered clip name must match what the scene wrote); only the
file is resolved. The swallowed exception now prints (Rule 13). *Test:* `AnimationImport` gained
an out-of-tree content root — assert the key does **not** resolve uncatalogued, scan, assert it
does. *Ref:* `assets/ModelImporter.cpp`, `assets/AssetCooker.{h,cpp}`.

**#38 releasing the last reference to an asset freed GPU memory that frames in flight were still
using.** *Found by a thrown bolt:* it acquires `builtin://cube` + `builtin://white`, lives under a
second, and is destroyed — and `ResourceManager::release` destroyed the resource inside the
gameplay step. `vkDestroyBuffer(): ... currently in use by VkCommandBuffer`, and the same for a
sampler still bound to a descriptor set. **Three sites had already met this problem and each
patched its own instance** (a `vkDeviceWaitIdle` for `runtime://` meshes, another for hot reload,
a deferred list in the Inspector); none covered the ECS destroy path, which is the one a game uses
constantly. Rule 22 says fix the category. *Change (design-first,
`DevDocs/DESIGN_GPU_RETIREMENT.md`):* a **retirement queue** — `release` erases the table entry and
key mapping immediately (so the key reloads fresh the same step) and hands the GPU object a
countdown of `framesInFlight`; `ResourceManager::endFrame()`, called once per frame by the
renderer after it waits on that frame's fence, destroys what has outlived every frame that could
still read it. Deferral rather than a wider `vkDeviceWaitIdle` because an arena destroys a bolt, a
corpse and several audio emitters per second and each would stall the pipeline; the guarantee is
identical. Two of the three ad-hoc mechanisms were **deleted**, and the Inspector path lost a
full-pipeline stall. *Evidence:* **3 validation errors per run -> 0**, over an 80 s run destroying
hundreds of asset-holding entities. *Named, not built:* the engine has no device-backed test gate,
so the queue's timing is verified by the game rather than the suite — the second defect (after
#G43) that such a gate would have caught.

**#39 packaging an external game shipped no model at all.** A skin/clip sub-key is not a cooked
artifact (the Phase 19 interim ships the source model and reports the key), and the copy
destination was `outRoot / sourcePath` — but a catalogued path is **absolute** whenever the content
root sits outside the working directory, and `path / absolute` discards the left operand. The copy
became a self-copy: `0 source model(s)`, and no external game could ever ship an animated
character. The packaging gate behaved correctly throughout — exit code 1, nothing shipped
silently. *Change:* the destination is the key's own directory under the package root, keeping the
real filename. *Test:* `Packaging` gained an external-content-root case (it asserts the catalogued
path really is absolute first, so the test cannot pass for the wrong reason); break-tested FAIL
without the fix. *Ref:* `assets/Packager.cpp`.

**#40 half of every shadow map was being thrown away.** *Found by the arena floor:* a hard straight
seam across it, one side lit and one side flat, and **no character casting a shadow at all**. It
survived removing the walls, clearing `castsShadow`, and a 16x bias increase — so neither occlusion
nor acne. *Root cause:* **GLM emits OpenGL clip space (NDC z in -1..1); Vulkan clips z to 0..1**,
and every projection the engine built used the GLM default. On the perspective camera the discarded
half hides between the near plane and ~2x near, which is why it went unseen for the engine's whole
history. Applied to the shadow map's **orthographic** light frustum, z = 0 sits at the frustum's
*midpoint*: the near half of the scene never reached the shadow map, stayed at the clear value and
read as lit, while the far half was compared against a depth the fragment shader then re-mapped as
if it were -1..1. *Change (the category, not the case):* `glm::perspectiveRH_ZO` and
`glm::orthoRH_ZO` at both sites, each stating its convention, and the shadow lookup remaps only
`xy` — `z` already arrives in the depth buffer's space. *Test:* new **`ProjectionDepth`** self-test
pins near->0 and far->1 for the engine's camera and for the light's ortho; break-tested FAIL against
`glm::perspective`. *Verdict:* fixed — the seam is gone and characters cast shadows, verified in
the packaged standalone. *Ref:* `rendering/Camera.h`, `BasicTrianglePass.cpp`, `shaders/basic.frag`.

*What the arena proved rather than broke.* Release, one machine, `SUGAR_FPSLOG`: **5 / 19 / 35 /
81 / 162 enemies -> 400 / 370 / 285 / 312 / 245 FPS**, at 1 848 entities and 169 draw items held to
**140 draw calls** by instancing. 162 navmesh agents replanning against a moving player, 162
looping **spatial** growls against a 64-voice mixer cap plus hundreds of transient one-shots, no
dropout and no error. The first `ColliderComponent` any game has authored, in all four roles at
once (dynamic player body, kinematic enemies, filtered projectile spheres, `isTrigger` pickups).
**Hot reload with live runtime assets** — F8 mid-run at ~160 entities holding runtime meshes and
skinned characters — loaded `Game_live_1.dll` and carried on with zero validation errors, closing
an *unproven* cell in the audit. Packaged standalone: 368 FPS fullscreen, every key from the
manifest, no source.

*Two deferred items now carry numbers instead of intuition.* **CCD / tunneling (B12):** 10 bolts
fired at a 2 m wall — 34, 120, 170, 200 and 400 m/s all register 10 hits; **800 m/s registers 0**
(a fixed step moves the bolt 13.3 m). The game's bolt is 34 m/s, **24x below the threshold**, so
nothing forces CCD. **`MAX_SKINNED_DRAWS = 64`:** at 65+ skinned characters in one frame the extras
render in bind pose behind a warn-once; reached only by the deliberate 162-enemy probe, against a
designed wave cap of 24. A stress probe is not a game requirement — recorded, not raised.

*The one ergonomic gap the game would ask for next, and did not get:* a Core-only module can build
a skinned character (the skeleton is ordinary entities, the skin is a name) but must **duplicate
the rig's joint table** from the glTF in game code, with nothing checking the two agree. Not a
defect and not forced — `ModelImporter` is engine-side by construction, and the fix is a
spawn-a-model seam that one game wanting it is not yet evidence for.

### Level 3, game 2 — the arena's adversarial pass

*No new features. The arena was pointed at the four things it had just changed, plus the
subsystems the audit had only ever seen used gently. Five passes, validation layers on
throughout, driven by `SUGAR_ARENA_TORTURE=<pass>` in the game's own `src/Torture.cpp`.
Gate **57 -> 60/60** Debug + Release.*

**#41 one big collider turned the broadphase back into the all-pairs scan it replaced.**
*Found by the physics pass:* firing bolts continuously into the walled arena took it from
8.9 FPS to **0.84**. Instrumented rather than guessed (`SUGAR_PHYSDBG=1`):

```
[physdbg] shapes=1016 cell=40 buckets=614 pairs=2124
[physdbg] shapes=1524 cell=40 buckets=624 pairs=23221
```

Shapes rose 1.5x, candidate pairs 11x. `cell=40` is the whole story: the uniform grid sized
its cells from the **largest** shape in the scene, and the arena's 40 m east wall made every
cell wider than the playfield, so a thousand projectiles shared one bucket. **Every game has
a collider far larger than its typical one** — a floor, a wall, a level hull; the three
earlier dogfood games escaped only because none of them had *many* colliders (the voxel game
rolled its own collision and never added a `ColliderComponent`).

*Why the suite missed it, which is the part worth keeping:* `GridVsBruteForce` compares the
grid's pairs against a brute-force oracle and `GridEdgeCases` already had a case named *"a
big shape mixed with small ones"*. Both passed the whole time. **They verify the grid's
answers; nothing verified that it was still a grid** — a degenerate grid is perfectly
correct and merely quadratic. A performance property needs a test that *measures*.

*Change (design-first, `DevDocs/DESIGN_BROADPHASE_SCALE.md`):* cells are sized from the
**median** shape rather than the largest, and a shape whose AABB would span more than 32
cells is kept out of the grid entirely and tested against everything directly — the two-tier
split every physics engine converges on, `O(n + k*n)` with `k` a handful of level pieces.
Determinism is untouched: pairs are still emitted as `(i<k)`, deduplicated and sorted, so
contact resolution order is unchanged (Rule 10). *Test:* new **`GridScale(1200)`** stress
test asserts near-linear growth in AABB tests performed, which needed
`PhysicsWorld::lastBroadphaseCandidateCount()` — a diagnostic in the same spirit as
`Renderer::submittedDrawCalls()`. Break-tested against the old sizing: **300 shapes -> 45 150
candidate tests (exactly n(n+1)/2), 1200 -> 482 570**. With the fix, 232 FPS at 1 016 shapes
and full recovery to 242 FPS as the population drains.

*Named, not built:* the median cell size **moves with the population mix** — measured
flipping 0.34 -> 0.99 mid-run as pickups and characters outnumbered projectiles, roughly
tripling the candidate tests the projectiles pay. A percentile or a smoothed estimate would
steady it; nothing forces the choice, and picking a constant without a workload is the
speculation the freeze exists to prevent.

*What survived the other four passes.* **Animation:** 4 000 skinned characters created and
destroyed *while animating*, with every graph edge driven each cycle — 0 validation errors,
live resource counts flat. **Audio:** 16 000 one-shot voices and 2 000 looping spatial
sources against a 64-voice mixer cap, owners destroyed mid-playback — 310-420 FPS, no
dropout, no error. **GPU/resource torture** (the highest-value pass after #38): 15 000
entities and 7 500 unique runtime meshes created and released across 250 cycles with layers
on — **0 validation errors and `meshes` pinned at 37, `textures` at 6, `clips` at 5 for the
whole run**, with `retired` rising and draining as the queue works. **Two hot reloads mid-
churn** (`Game_live_2.dll`) while those meshes were live: no crash, no leak, gameplay
continued. **Packaging from an external content root:** the package copied to an unrelated
directory boots three times running with `skin=1 clipIdle=1 clipDie=1` and zero validation
errors — no accidental source-tree dependency in either direction.

*Instrumentation added, deliberately small:* `SUGAR_FPSLOG` now also reports live
`meshes/textures/clips` and the retirement queue depth, because "did 15 000 spawn/destroy
cycles leak anything?" is answered by those staying flat and by nothing a headless test can
observe.

*Two dev-environment traps cost real time and are now written down* (`DevDocs/DEV_ENVIRONMENT.md`):
`cmake --build --target SuGarEngine` does **not** always refresh `SuGarCore` for a config, and
both of a game's build directories emit to the same `Game.dll`, so switching config silently
leaves the wrong DLL in place. Both fail identically — an access violation inside
`BehaviorRegistry::registerBehavior` at startup — and in both cases **the crash reporter named
the exact frame**, which is the first time it has been used in anger rather than tested.

### Level 3, game 2 — the arena grows a menu (#42)

*The next increment of ordinary game depth: a pause screen, an upgrade screen, a run name
that persists. It stopped being writable immediately.*

**#42 the runtime UI's interactive half was unreachable by any game.** Three separate
places, one mistake: **the engine's own demo document's element ids were compiled into the
view.**

- **Intents.** The only bridge from a document to a `UIIntent` was
  `GetElementById("open")` -> `openScreen("Inventory")` and `GetElementById("back")` ->
  `popScreen()`. A game ships its own `hud.rml`, so no game could make anything clickable.
- **Screens.** `UIScreenComponent`'s active screen was applied by writing debug text into
  `#body` — an element only the demo document has. Pushing a screen did nothing visible.
- **Text fields.** Every field was rendered as `("Name: " or "Tag: ") + buffer`, chosen by
  matching the demo's ids, so the arena's run-name field displayed a stray `Tag:`.

That is why the platform audit's UI row never said more than "labels and element states":
across L1, L2 and L3 there is not one use of `uiScreens`, `focus` or `textInputs` in any
game. The model half was complete and self-tested the whole time; nothing could reach it.

*Change (design-first, `DevDocs/DESIGN_UI_INTENT_BINDING.md`):*
- **`data-intent` on any element** — `open:<screen>`, `pop`, `focus:<element>`, `unfocus`,
  `text:<s>`, `backspace`, `caretleft`, `caretright` — parsed once at load, one
  `IntentEmitter` per element. Content declares what a button does, in the engine's existing
  vocabulary; the engine stops being the author of the list. An unparseable value is
  **reported and ignored** (Rule 13), never guessed into a button that does the wrong thing.
- **The active screen becomes a class on the document body**, `screen-<id>`, and the game's
  RCSS decides what that means. Chosen over the engine toggling a tagged container because a
  screen is rarely "one panel appears" — it also dims the HUD and greys a button, which is
  RCSS's job. Exactly one `screen-` class at a time, so a game never unsets anything.
- **Text fields render verbatim**, with the caret at its authoritative index and the text
  **escaped** — `SetInnerRML` parses what it is given, so an unescaped `<` would let a
  player's run name inject elements into the document.

*Test:* `UIIntentBinding` covers the parse — every form, unknown verb, missing argument,
empty argument, an argument-free verb given one, and case sensitivity — asserting a bad
attribute yields **no** intent rather than a wrong one. Break-tested. The wiring itself is
device-bound and was verified live: **`[RuntimeUI] bound 6 document intent(s)`** from the
arena's own document, the pause panel appearing on `screen-Pause`, and `best wave 2` read
back from the previous session's save.

*What the increment also proved, game-side:* `SaveData` progression across processes, and
gameplay pausing correctly — the fixed step keeps running (animation, UI intents drain)
while no gameplay behaviour decides anything.

*Still not built (Rule 8):* intents on events other than `click`, arguments beyond one
string, directional focus traversal, any widget library. Which upgrade a player picked rides
on `FocusComponent` — clicking focuses, focus is authoritative, the game reads it — rather
than on a new intent verb invented for it.

### Deferred backlog — working it down (start)

*A pass over items deferred **with rationale** rather than forgotten. The order is by
evidence, not by age: an item is taken now only if it is a defect, or if a game has since
supplied the evidence the deferral said it was waiting for. Everything measured as
not-forced stays deferred — CCD (24x margin), `MAX_SKINNED_DRAWS` (a stress probe reached
it, not a game), batched-submit upload, binary snapshots, greedy face-merge, async chunk
generation, `MAX_LIGHTS` clustering.*

**World-label depth occlusion — deferred since world labels landed, now fixed.** The
original note was one line: *"depth occlusion for world labels (they show through walls)"*.
It stayed deferred for a real reason, and the fix is shaped by it: **"solid" is a game's
word, not the engine's.** An arena wall should hide a nameplate; another enemy standing in
front probably should not, and only the game knows which layer is which.

*Change:* `WorldLabelComponent::occluderMask` — the collision layers that hide this label,
tested by a raycast from the camera to the anchor
(`rendering/WorldLabelVisibility.h`, Core, so the decision is headless-testable while the
projection/layout half stays in the view). **A mask of 0 means never occlude, and 0 is the
default**, so every scene written before this renders exactly as it did; the field is also
written to disk only when set, keeping existing scene bytes identical. The labelled
entity's own collider never counts — a nameplate must not be hidden by the body it belongs
to.

*Test:* `WorldLabelOcclusion` — nothing between, a wall between, the same wall on a layer
the label ignores, mask 0 opting out, the owner's own collider, the degenerate
camera-on-anchor case, and the mask surviving a scene round-trip. Break-tested.

*Verified in the arena:* nameplates render and are occlusion-tested against
`arena::LayerWorld` only, so enemies never hide each other's labels.

*And a game-side finding on the way:* world labels need a `#worldlabels` container in the
game's document — the arena had none, so its nameplates had **never rendered at all**. Not
an engine defect (a document that doesn't want labels shouldn't get them), but one more
case of the same family as #42: the contract between a game's document and the engine was
discoverable only by reading the view's source. It is now in the game's `hud.rml`.

Gate 60 -> **61/61** Debug + Release (+`WorldLabelOcclusion`).

**#43 no backoff between failed replans — deferred at #36, now built.** *The second item off
the deferred list, and the one with the strongest case: an already-observed pathological
workload, a single identified ownership point, and an acceptance criterion that is a number.*

`setDestination` already refused to re-arm a **Following** agent (that was #36). It still
re-armed an **Unreachable** one, deliberately — that retry is how an agent notices a door
opening — and the cost was a full A\* over the reachable component, per agent, **per step**.
Measured at the time as 58 agents going 243 -> **0.89 FPS**.

*Instrumentation first, on purpose.* `NavPath::searchesPerformed()` was added **before** the
policy, because otherwise the fix could only be shown to improve the frame rate, not to
remove the work. The reproduction — 40 agents, two disconnected islands, destination
re-issued every step for 120 steps, exactly the shape of a chase behaviour — reported:

```
[stress]   replan searches 4800 (unthrottled 4800)
```

40 x 120. Every one of those searches returns the correct answer, which is precisely why no
correctness test could ever have caught it.

*Change (design-first, `DevDocs/DESIGN_REPLAN_BACKOFF.md`):* a **per-agent cooldown**,
specified as behaviour rather than as a duration constant — Following + same goal: no
search; **Unreachable + same goal: no search until the cooldown expires**; goal *changes*:
search immediately, because a new decision is not a retry; route becomes available: the next
permitted search succeeds and Following resumes on its own; Arrived: unchanged. Two fields,
and the split between them is the design: `replanCooldown` is **authoritative history** (a
snapshot restored mid-cooldown that forgot it would fire a fresh storm on the frame you
scrubbed to — Rule 21b), while `failedReplanInterval` is per-agent policy, for the same
reason `speed` is per agent. **Decremented by the fixed step's `dt`, never wall-clock** — a
backoff on OS time would make the simulation non-reproducible, and it is the same mistake
that once had a navigation harness measuring itself.

*Result, same workload:* **4 800 -> 160 searches.** `NavReplanBackoff` (stress) asserts the
throttle does not become abandonment (every agent still tried, all still report
Unreachable), that the count is bounded by the interval rather than the step count, and that
a *changed* destination still searches immediately, once per agent. Break-tested at
`failedReplanInterval = 0`, which reproduces the 4 800 exactly.

*Not built:* exponential backoff (nothing shows a fixed interval is wrong, and an
exponential one needs a reset rule nobody has asked for), a navigation-wide search budget
(it would starve agents non-deterministically by iteration order), and waking agents on a
navmesh rebake (the interval already bounds the delay).

Gate 60 -> **62/62** Debug + Release (+`WorldLabelOcclusion`, +`NavReplanBackoff`).

**#44 box-shadow bled through translucent elements — the clip mask that #14 deferred.**
*Third item off the deferred backlog, and the one the deferral itself had named a condition
for: "box-shadow on a **translucent** element would show the shadow bleeding through its
centre; opaque elements hide it." The arena's pause panel is that element.*

*Reproduced before anything was designed.* Two identical boxes, same background, same 50%
alpha, differing only in `box-shadow` — then the shadow **swapped between them**, so
position and background are controlled and only the shadow moves:

```
LEFT  box: shadow-on (39.8, 34.8, 34.9)   shadow-off (93.1, 83.3, 83.6)
RIGHT box: shadow-off (114.0, 104.4, 104.7)  shadow-on  (51.8, 46.7, 46.7)
```

The darkening follows the shadow, not the position. *And it is the clip mask, not the
compositor* — the shadow **is** blurred and composited correctly, merely unclipped.
RmlUi's own source says what should happen (`GeometryBoxShadow.cpp`): mask to everything
outside the element's padding+border box, *then* draw the shadow. `RmlVulkanRenderer`
overrode no clip-mask entry point, so `RenderToClipMask` fell through to a base-class no-op.

*Change (design-first, `DevDocs/DESIGN_UI_CLIP_MASK.md`) — and the design reversed itself
during implementation, which is the part worth keeping.* The obvious answer is a stencil
attachment on the UI pass. It was rejected on evidence: `findDepthFormat()` prefers
**`D32_SFLOAT`, which has no stencil aspect**, so stencil would mean changing the depth
format for the **scene** pass — exactly the leak this seam exists to prevent — and offscreen
effect layers, where blurred shadows are actually drawn, carry no depth attachment at all.
So the mask is an **`R8_UNORM` coverage texture** owned by `RmlVulkanRenderer`, written by
its own pass and sampled by the UI fragment shader. Nothing outside `src/ui/` changed. Set 1
carries it; with no mask active set 1 binds the existing 1x1 white texture, so the multiply
is inert and an unmasked document is unchanged.

RmlUi 6.3 defines **three** operations, not the four an earlier draft assumed: `Set`
(clear 0, write 1), `SetInverse` (clear 1, write 0 — the box-shadow case) and `Intersect`
(clear 0, write the *previous* mask, which needs two targets ping-ponging because sampling
the attachment being written is a feedback loop). An `Intersect` with no existing mask is a
`Set`.

*Test:* `ClipMaskPolicy` pins that table — the half that can be silently wrong, since
inverting `SetInverse` yields a shadow covering everything *except* the element and still
renders and validates. Break-tested.

*Measured result:* the same probe re-run gives **39.8 -> 86.4** against an unshadowed 93.1,
**87% of the bleed removed**; the residual ~7 is the blur running after the mask and
smearing coverage back into the hole, which is inherent and what other backends do.
*Also verified:* inset shadows stay inside their element; an oversized child of a rounded
`overflow:hidden` parent is clipped to the **parent's** radius (a scissor would give square
corners, so the mask is genuinely doing it); **zero validation errors**; Pong's L1 HUD
chips unchanged; and the engine's own demo document — no `box-shadow`, so no mask ever set —
**pixel-identical** apart from the unrelated #42 text-field fix.

*One defect found in the making, by the validation layer:* the pipeline layout's push range
now spans vertex+fragment, and `vkCmdPushConstants` must name **every** stage of an
overlapping range even when the shader reads only half of it —
`"which is missing stageFlags from the overlapping VkPushConstantRange"`. Fixed.

Gate 62 -> **63/63** Debug + Release (+`ClipMaskPolicy`).

**Generational entity ids — the audit's one "widen now" item, designed then built.**
*Not forced by a defect. `DevDocs/DESIGN_GENERATIONAL_IDS.md` was written first and
deliberately implemented nothing; this is that record executed, on the schedule it set (its
own phase, behind L3 game 2).*

`Entity` is still `uint32_t` and `INVALID_ENTITY` is still literal `0`. What changed is what
the bits mean: **20-bit index, 12-bit generation**, with index 0 and generation 0 both
reserved. The index is the half the free list recycles and the only half allowed to address
anything; the generation counts how many tenants a slot has had. Both numbers are measured
rather than chosen — 1 048 575 simultaneous entities against a highest-ever 2 846, and 4 095
reuses of one slot against a measured worst case of ~250 in 90 seconds (which is what rules
out an 8-bit generation).

*The payoff:* `Registry::isAlive(Entity)`. The engine has never been able to answer "is this
handle still the entity I got it for?", because a recycled id was the same integer as the
original — the only detectable case was the one where nothing had reused the slot yet.

*What did not change, as the design predicted:* no scene file, prefab, snapshot or golden,
and no format version. `parent` is an index into the objects array, not an entity id. The
local variable that made that confusing is now `objectIndex`, because "entity index" means
something specific after this change.

*Ordering needed care at six sites, not the three the design counted.* "Lowest id" stops
meaning "oldest" once a recycled low slot carries a high generation, so every site got
`entityOrderLess` (index, then generation): the audio listener, the game camera, the editor's
orbit-parent pick, `EntityQuery`, `DrawList`, and — the one that mattered —
`SceneSerializer::collectOrderedEntities`, whose sort decides **the order objects are written
to the scene file**, with `parent` referring to positions in that array. Sorting packed values
there would have churned every scene file the first time an entity was recycled.

*Three defects fell out of the migration itself:*

**#45 `destroyEntity` banked its argument unconditionally**, so a double destroy — or a
destroy through a stale handle — pushed one id onto the free list twice and later handed it to
two live entities. Undetectable before; the generation check makes it a no-op now.

**#46 a `GameData` handle read with a `-1` fallback produced `0xFFFFFFFF`.** The arena's
torture harness did `static_cast<Entity>(data.getInt(key, -1))`, so a *missing* key sailed
past the `!= INVALID_ENTITY` guard and was handed to `destroyEntityTree`. Rather than fix ~30
`static_cast<int>(entity)` sites across two games (the design's stated plan, written when it
believed there were two), the seam went into the engine: `GameDataComponent::setEntity` /
`getEntity`, which stores a handle exactly as a `double` and returns `INVALID_ENTITY` for
anything outside the handle range. Rule 22 — the category, not the case.

**#47 a refused game module was retried every frame.** `Entity` keeps its size, so a stale
`Game.dll` would link, load and silently misread handles — the design called this out and asked
for a Core version stamp, which now ships (`SUGAR_DECLARE_GAME_MODULE_ABI`, checked before the
entry point is even resolved). Shipping it exposed the second half: the hot-reload watch never
banks the timestamp of a *failed* load, deliberately, so it can recover from a mid-build copy.
Against a permanently-wrong DLL that means retrying identical bytes forever — measured at
**204 rejections in 10 seconds**. A definitively-wrong module now banks its timestamp and waits
for a real rebuild; a copy/IO failure still retries. 204 → 1.

*Verified:* gate 63 → **64/64** Debug + Release (+`EntityGenerations`, which exercises the
generation **wrap** past 4 095 rather than assuming it unreachable). All nine game modules
rebuilt against the new ABI. The arena's `gpu` torture pass — 16 000 spawns / 15 920 destroys
over 200 cycles at ~80 live — ran with the transform count flat at 513–528. The voxel game's
autoplay ran 65 mobs and 2 088 block edits with navigation and HUD intact, and a window capture
confirms every HUD element (all of them addressed through `getEntity` now) still renders.

*Two decisions the design left open, settled in code:* `reset()` clears generations, because
carrying them would make loading the same scene twice produce different handles and break
run-to-run comparison (Rule 10) — the cost is that a handle held across a scene load stays
undetectable. And `kMaxBankGap` dropped `1<<20` → `1<<16`: the index space is now capped at
2²⁰, so the OOM the guard was written for is structurally impossible and the old constant had
become unreachable, but the O(n) free-list scan is still real.

*Unclaimed, and worth keeping visible:* **no game has ever hit a stale-handle bug** (~35 000
destroys across the arena's adversarial passes), and **no game calls `isAlive()` yet**. By this
project's own rule that makes it an unused seam — the next game to hold a handle across frames
is the one that gets to judge the API.

Gate 63 -> **64/64** Debug + Release (+`EntityGenerations`).

**L3 game 3 — the turn-based dungeon crawler: is the engine genre-neutral?**
*Every dogfood game so far has been real-time. The engine had been shown robust for real-time
games; it had never been shown genre-neutral. The contract
(`DevDocs/DESIGN_TURN_BASED_PROBE.md`) was **frozen before the first line of game code**, with
nine watch areas each carrying an instrument and a numeric promotion threshold, so that "no
engine change was needed" would be a falsifiable result rather than a description of how hard
anyone looked.*

**Result: the fixed-step engine survived a fundamentally different time model, and exactly two
assumptions were exposed.** Six areas answered `SUFFICIENT`, two `PROMOTES`, and one turned out
to be unmeasurable — which was the sharpest finding of the three.

**#48 press-edges are lossy for every fixed-step consumer.** `Input::beginFrame()` clears
press-edges once per RENDER frame while behaviours run on the 60 Hz accumulator. At 248 FPS
that is ~4.1 frames per step, so a press survives only if it lands in a frame that also runs a
step: **20 physical presses produced 4 commits**, and a repeat run produced 5 — lossy *and*
nondeterministic. The property is backwards: faster hardware loses more input.

Not a turn-based problem. Six shipped call sites were affected — FlappyBird's only button,
Platformer's jump, the arena's pause/swing/throw, Minecraft's hotbar. No test caught it because
`SelfTests.h` asserts the edge's *value* and never its *lifetime across a frame/step boundary*;
there is no frame and no accumulator in a self-test. The [[property-not-watched]] pattern again:
a lifetime bug in a function that returns the right answer.

Fixed at the seam (`DevDocs/DESIGN_INPUT_EDGE_SEMANTICS.md`, option D): `InputActions` became
the declared simulation-domain layer with a latch consumed at fixed-step end; `Input` kept
frame-domain semantics, so the editor's F-keys are untouched. **Five of six call sites were
fixed with no game code change at all** — the function now means what every caller already
assumed. Minecraft's eight ordinary sites were migrated; its two *parametric* ones
(`GLFW_KEY_1 + slot`) stay on raw `Input` and are recorded as an `InputActions` expressiveness
gap rather than answered with eighteen invented bindings. Verified by parity, not by a smoke
test: an inventory toggle pressed 4 times ends closed and 3 times ends open, which zero
registered presses would not produce.

**#49 idle physics on a world that cannot move** (designed, not implemented —
`DevDocs/DESIGN_STATIC_PHYSICS_COST.md`). 186 static box colliders, no rigid bodies at all,
cost **2.975 ms/frame** — 17.8 % of the step budget against a 10 % threshold. `PhysicsWorld::step`
rebuilds the world-shape array and the uniform grid from scratch every step and narrowphases
static-vs-static pairs that can never resolve. Distinct from **#41**, which was about pair
*count*; #49 is about paying at all when nothing can have changed. The information needed to
fix it already exists — a collider with no dynamic body cannot have moved.

**The unmeasurable one, and why it matters most.** The snapshot watch area asked for a
capture:turn ratio. There were no captures: **4.104 ms at 91 entities and 12.507 ms at 348,
against a 4 ms budget**, so the budget admits roughly 90 entities — smaller than one furnished
room. The contract assumed 200–400 would fit. Invoking the freeze clause's single permitted
amendment (an instrument genuinely incapable of measuring what it claimed), the result is
recorded as unmeasured, with what *was* measured stated separately: 20.4 steps per turn at
machine speed, ~115 at a human's input cadence, unbounded while a player thinks — so per-step
capture would record 20–115+ identical snapshots per turn, on inference rather than
observation. **Time travel is now off in all three L3 games.** SuGar's headline debuggability
feature is unavailable in every real game it has.

*Answered `SUFFICIENT`, with numbers:* ordered inventory as `bag0..bagN` (touched once per turn,
~10 lines, no correctness issue); animation across the commit→complete gap (the engine's
one-shot contract expressed it, 6-line behaviour picks the next clip, no game-side state
machine); `NavAgent` unused (characterization — grid movement is a different representation,
not a missing capability); audio one-shot per committed action (`oneShots == commits == turns`
over ~1 400 steps); lighting at 17 visible point lights against 7 slots (30 churn events but
per-turn brightness deltas max 8.1 and monotone — **nearest-N churns hardest where it matters
least**, at the selection boundary); runtime UI at 0.439 ms/frame for 4 labels (6.5 % of frame,
under threshold, but per-frame-per-label rather than per-model-change, so the figure is
recorded for a game with thirty).

*`Registry::isAlive()` stopped being an unused seam.* A monster holds a target handle across
turns and a projectile holds an owner handle across the steps it flies. The design's named case
was forced deliberately — a thrower destroyed mid-flight — and produced `orphanImpacts=1` with
`ownerAlive=0`. Before generational ids that handle would have named whoever inherited the slot.

*Determinism:* byte-identical replay logs at **different run durations**, converged by a
terminal marker. The byte-compare immediately earned itself by finding a livelock nothing else
had — autoplay indexed its script by `turn`, and a wall bump deliberately does not advance the
turn, so a scripted move into stone retried forever.

**#49 built the same day** (`DevDocs/DESIGN_STATIC_PHYSICS_COST.md`). **2.975 ms -> 0.258 ms,
a 91 % reduction**, now 1.5 % of the step budget. The design record had recommended a
static/dynamic split; writing the failing test proved that wrong, because
`testPhysicsBroadphase` asserts that two overlapping **static** boxes produce collision events
— static-vs-static event generation is contractual, and the split would have deleted it
silently. The implementation instead *does not recompute what cannot have changed*: a world
whose colliders and poses are bit-for-bit last step's re-emits cached events and returns before
the shape gather, grid build and narrowphase.

Invalidation is a **value comparison, not a dirty flag** — the design's own stated fear was
that a missed invalidation is a collider that silently stops colliding, and comparing the real
data removes that failure mode instead of managing it. The new `PhysicsStaticRebuild` test
pins *work proportional to change*: 64 statics and zero bodies rebuild once across three steps,
one hand-moved collider forces exactly one more, then quiet. It was **proven able to fail** —
with invalidation deliberately disabled it reports FAIL while `PhysicsBroadphase` still passes.
Determinism: the crawler's replay log came back **byte-identical to the pre-fix hash**, so the
change altered simulation output by zero bytes. Gate 65 -> **66/66**.

**Two HUD defects, both game-side, both cross-game** (2026-08-20, reported after the crawler
shipped and recorded in `DevDocs/RUNTIME_UI_LESSONS.md`). RmlUi's box model is `content-box`, so
`padding` widens a panel past its slot — the arena's `WAVE`/`KILLS`/`BEST COMBO` and the
crawler's `TURN`/`HEALTH`/`BAG` each overlapped their neighbours by 14 px, fixed with
`box-sizing: border-box`. And **world labels are not a separate pass**: the engine writes them
into a pooled set of spans inside the *game's own* `#worldlabels` container, so paint order is
ordinary RCSS sibling order and belongs to the game. The arena had that container as its LAST
child, so enemy nameplates painted over the pause panel, the upgrades panel and `YOU DIED`;
moved to first child. The voxel game already had it first, which is why it never showed the bug
— the arena was the outlier, not a missing engine feature. Neither is an engine change, and the
label pool living in the game's document is precisely what gives a game this control.

Gate 64 -> **65/65** Debug + Release (+`InputEdgeLifetime`). Game report:
`E:\Sugar Engine - Games\Level 3\DungeonCrawler\Report.md`.

**Phase 1 — snapshot capture cost: measured, not fixed**
(`DevDocs/DESIGN_SNAPSHOT_CAPTURE_COST.md`, 2026-08-27). *Forced:* the "unmeasurable one"
above left the snapshot budget's real cost unknown — 4.104 ms @ 91 and 12.507 ms @ 348 carried
no build-configuration label, and the `SUGAR_BENCH` figure they were checked against
(1.99 ms @ 500, ≈4 µs/entity) turned out to be stale. *Change:* a differential-substitution
instrument (traversal / format / materialize / storage, isolated by swapping the destination
rather than timing inside the hot loop) was built with **zero writer changes**, gated behind
`SUGAR_SNAPDBG` / `SUGAR_SNAP_BUDGET` / `SUGAR_SNAP_CORPUS`, all off by default.

*Measured, not fixed:* in Release, at real measured entity counts (Crawler 91 / 348, Minecraft
up to 295, the arena up to 182), median capture cost cleared the 4.0 ms budget in **every**
game — Crawler-full's median fell to 2.29 ms, nowhere near the 12.507 ms / 3.13× speedup the
design document had opened with. The original figures turned out to be **consistent with a
Debug build, not Release**: Debug-vs-Release on the same crawler-small scene measured 3.251 ms
median against 0.552 ms, a ~5.9× gap that alone accounts for the original numbers without
invoking representation cost. The bench-vs-real "~8× per-entity gap" this document had flagged
as unreconciled dissolved the same way — a stale bench figure compared against
Debug-configuration real-game figures, not a genuine discrepancy; like-for-like Release
numbers now agree within ~20%.

*What is NOT fixed:* three of the four games (all but Crawler-small) each produced a small
number of individual captures — 0.02%–0.12% of samples — that exceeded the 4.0 ms budget
mid-run, unrelated to any first-capture cold-allocator spike (none was observed in any run).
Under the policy's actual one-strike, no-hysteresis latch, the first such capture in a real
session (no `SUGAR_SNAP_BUDGET` override) would permanently disable time travel — a defect in
the *policy* (`SnapshotCapturePolicy::recordCaptureCost`), not the representation, logged and
left for separate, cheaper, later work; it is not cited to justify any representation change.
The design document's own decision rule was applied mechanically to the phase-share numbers
regardless: formatting is 89–92% of `save` at every entity count measured, and its cost is
681×–874× a plain `memcpy` of the same output size, so intervention **A** (specifically
stream-formatting overhead — the copies the document originally staged first account for only
~9%) is *authorized* by that evidence. With the acceptance bar already cleared on the median,
nothing is *forced*. Rule 8: no unforced work.

Gate 66 -> **68/68** Debug + Release (+`SnapshotBudget`, +`SnapshotSinkBytes` — both validate
the instrument itself, not simulation behavior; `Serializer` golden test unchanged throughout,
confirming no emitted byte moved).

*A defect in the instrument, found by the closing review and worth recording because the class
recurs:* the split's advertised self-check — "the four phases must sum to the measured total
within 5%" — **was an algebraic identity and could never fail**. Two of the four phases are
derived by subtraction from the same three measurements, so the sum equals the total by
construction; it read 0.0000% in every run of every scene. A green self-check that cannot go red
is worth less than no check, because it is trusted. It was replaced by one that *can* fail and
was break-tested to prove it (`snapshot_sink_bytes_delta`: the discarding sink's byte count
against the real save's — two independent paths over the same scene), and that check now runs in
`SUGAR_VALIDATE` rather than only in the ungated benchmark. The same review found the
`SUGAR_SNAP_CORPUS` knob nested inside `SUGAR_SNAPDBG`, which made the "never combine corpus
capture with a timing run" instruction printed in three documents describe a usage that could not
exist; the knobs are now independent. Rule 9a's discipline — break it and watch it go red — is
what the original check never received.

---

## Phase detail — M3 (Phases 16–21)

The per-phase engineering record for M3, each with its architecture decided first (the
`DevDocs/DESIGN_*.md` records) and gated by `SUGAR_VALIDATE`. Summary bullets are in the
Milestones appendix; the detail below is the reference.

1. **Runtime UI (RmlUi) — DONE (Phase 16).** It led M3, and not merely because it's a
   bounded library integration. Without it you *cannot* build a proper game (menus, pause, settings,
   HUD, health, inventory, dialogue), and the temptation is to reach for
   `ImGui::Begin("HUD")` — violating the engine's own architecture. It is the *last
   missing piece of the platform*, so it leads.
   - **Architecture decided before code:** see
     **`DevDocs/DESIGN_RUNTIME_UI.md`** — the governing
     invariant is `UI = f(ECS, input)`: RmlUi is a *view*, authoritative UI state
     lives in ECS ([RULES.md](RULES.md) Rule 21), callbacks emit intents only, and
     the UI system polls ECS (never subscribes). This makes snapshot restore /
     hot reload restore the UI for free.
   - **16A — model layer (DONE):** the authoritative half, built and tested headless
     before any RmlUi. `UIScreenComponent` (screen stack) + `FocusComponent` in ECS
     (Core, `src/ui/`); a render-rate→fixed-step `UIIntentQueue`; `RuntimeUISystem`
     that drains intents deterministically (open/pop screen, set/clear focus); full
     serializer round-trip so UI state survives snapshot restore / time travel.
     Verified by the `RuntimeUI` self-test (intent logic + in-place snapshot survival
     with id preserved). Keyboard focus is authoritative; mouse hover stays derived
     (lives in the future view).
   - **16B.1 — RmlUi build + link + FreeType smoke path (DONE):** RmlUi 6.3
     and FreeType are vendored under `external/`, built via CMake, and
     **static-linked into the engine only** (never Core — Rule 15). SuGar-side
     `RmlSystemInterface` (time + logging) and a placeholder no-op
     `RenderInterface` compile against the RmlUi API. `SUGAR_UITEST` now proves
     the headless view foundation end-to-end: initialise RmlUi with the FreeType
     font engine, load a bundled Lato font, create a context, load a document from
     memory, verify the DOM, and render through the no-op interface. The test also
     asserts the probe element has **non-zero layout height**, which proves FreeType
     is actually measuring/rasterizing glyphs rather than merely being linked.
     FreeType is mandatory, not optional polish: RmlUi requires a font engine even to
     `Initialise()` — with `RMLUI_FONT_ENGINE=none` it logs *"No font engine
     interface set!"* and init fails outright. PASS in Debug + Release; the 20/20
     `SUGAR_VALIDATE` gates are unaffected.
   - **16B.2 — Vulkan render interface (DONE, visually verified):**
     `RmlVulkanRenderer` implements `Rml::RenderInterface` against **our** renderer —
     RmlUi's reference `RmlUi_Renderer_VK` creates its own device/swapchain, so it
     can't compose with an existing one. Own graphics pipeline (`shaders/rml.vert`
     + `rml.frag`), `Rml::Vertex` layout (pos/premultiplied RGBA8/uv), premultiplied
     blending (`ONE, ONE_MINUS_SRC_ALPHA`), dynamic viewport/scissor, push-constant
     viewport+translation, per-texture descriptor sets, and a 1x1 white texture for
     untextured geometry. Textures reuse `Texture::createFromPixels` — which is also
     how FreeType's font atlases arrive (`GenerateTexture`). Drawn inside the
     existing UI render pass after ImGui. **Verified by screenshot**: HUD panel with
     border, alpha blending, and FreeType text all rendering. (Caught in the process:
     RmlUi has no HTML defaults — elements are `inline` unless declared `display:
     block`, so rows ran together until the demo RCSS was fixed.) 20/20
     `SUGAR_VALIDATE` gates still pass.
   - **16B.3 — ECS sync + input loop (DONE, visually verified):** `UI = f(ECS, input)`
     now closes end-to-end. A singleton **UIRoot** entity carries
     `UIScreenComponent` + `FocusComponent`; input **queues intents** at render rate
     (F1 open screen, F2 back) and the **RuntimeUI system drains them on the fixed
     step** (registered in the scheduler, declaring `W:UIScreen|Focus`), so
     UI-state changes are deterministic; `RuntimeUIView` then **polls** the ECS each
     frame and pushes changes into the RmlUi document — never subscribing, per the
     design record. Verified by screenshot: `Screen: HUD` → F1 → `Screen: Inventory`
     → F2 → `Screen: HUD`, with the sim running.
     - **Emergent win:** the scheduler now reports **`Stage 3 (parallel): Audio,
       RuntimeUI`** — the first genuinely independent system pair the Phase 13A
       `stages()` analysis has ever found (disjoint writes: `AudioSource` vs
       `UIScreen|Focus`). The parallelism groundwork paid off on its own.
     - **Bug found + fixed (pre-existing):** every function-key shortcut was dead.
       They were gated on `!ImGui::GetIO().WantCaptureKeyboard`, but the editor is one
       big ImGui dockspace, so that flag is true whenever an ImGui window has focus —
       i.e. always. **F5 save, F6 play, F7 pause, F8 hot-reload were all silently
       non-functional.** ImGui never consumes function keys for text, so the guard was
       wrong for them; it now only gates character keys (camera 1/2/3), which really
       do conflict with typing.
   - **16B.4 — assets + pointer input + intent-emitting callbacks (DONE, verified):**
     - **Engine-owned assets:** font moved to `assets/fonts/LatoLatin-Regular.ttf`
       and the document to **`assets/ui/hud.rml`** (loaded via `LoadDocument`), out of
       RmlUi's bundled samples. The in-source document survives only as a fallback so
       a broken asset path is visible rather than silent.
     - **Pointer input routed into RmlUi** (`ProcessMouseMove` / `ButtonDown` /
       `ButtonUp`) at render rate, giving working hover + click.
     - **`IntentEmitter`** — an `Rml::EventListener` that does exactly one thing:
       push a `UIIntent`. This enforces the design's hard rule that **UI callbacks
       emit intents only**; they never mutate UI state, hide documents, or touch ECS.
       Clicking *Open Inventory* / *Back* drives the same fixed-step path as the F1/F2
       keys. Verified by screenshot: click → `Screen: Inventory` → click → `Screen: HUD`.
     - **Bug found + fixed (mine, caught by screenshot):** Vulkan validation spam —
       `vkDestroyBuffer(): can't be called on VkBuffer ... currently in use`.
       `ReleaseGeometry` destroyed buffers immediately, but RmlUi drops geometry during
       a re-layout while those buffers are still referenced by command buffers in
       flight. Now retired into a deferred queue and freed after the frames-in-flight
       margin; `shutdown()` force-collects (device already idle).
   - **16B.5 — bound to the game viewport (DONE, visually verified):** the player UI
     is no longer an overlay on the editor. `RmlVulkanRenderer`'s pipeline is built
     against the **scene render pass**, and the scene pass draws the UI onto the
     offscreen game image just before it ends — so the UI composites *into the
     Viewport panel*, with editor chrome correctly layering on top of it. Required a
     depth-stencil state (that pass has a depth attachment; UI never tests/writes
     depth). Pointer input is now fed in **viewport-local coordinates** (ImGui mouse
     minus the image's top-left): the offscreen image is created at the panel's size,
     so it maps 1:1 onto the RmlUi context and sidesteps window/DPI scaling entirely.
     Clicks only register while the cursor is over the game image. Verified by
     screenshot: HUD sits inside the viewport, and clicking *Open Inventory* there
     still drives `Screen: HUD` → `Screen: Inventory` through the intent → fixed-step
     → ECS path.
     - **Gotcha worth remembering:** the button state is polled via
       `glfwGetMouseButton`, *not* read from ImGui's `io.MouseDown`. ImGui's button
       state stayed false for injected/synthetic clicks even though hover worked, and
       player input shouldn't depend on ImGui's event routing anyway.
   - **16B.6 — ECS-authoritative keyboard focus (DONE, visually verified):** closes
     the design's mouse-hover-vs-keyboard-focus split. **Focus never lives in RmlUi.**
     Tab / Shift+Tab don't move focus directly: the view computes the next id from the
     document's tab ring (a DOM/view concern) and **emits a `SetFocus` intent**; the
     fixed-step system writes `FocusComponent` (authoritative); the view then polls it
     and applies `Element::Focus()`. Enter calls `Element::Click()` on the focused
     element, firing the *same* listener a mouse click would — so keyboard and mouse
     share exactly one path into ECS, and there is no second focus source of truth.
     Added a `button:focus` ring to the RCSS (view-only rendering of authoritative
     state). Verified by screenshot: Tab → amber ring on *Open Inventory* → Tab →
     ring on *Back* → Enter → `Screen: HUD` → `No screen` (pop applied through ECS).
     Because focus is a component, it also survives snapshot restore for free.
   - **16B.7 — authoritative text entry + input-ownership fix (DONE, verified):**
     completes the 16A model: **`TextInputComponent` { buffer, caret }** is now real
     ECS state, wired through the registry, access tracking, and the serializer, so a
     half-typed line survives a snapshot restore. Typed characters become
     `AppendText` / `BackspaceText` **intents** (GLFW char callback → `Input::textThisFrame`
     → fixed step), and the document renders the buffer into a plain `<div>` —
     deliberately **not** an RmlUi `<input>`, which would become a second, hidden home
     for authoritative text (Rule 21). The trailing caret is derived. Verified by
     screenshot: typing → `Name: sugar_`, Backspace → `Name: suga_`; the `RuntimeUI`
     self-test covers append/backspace/underflow and the snapshot round-trip.
     - **Root cause found + fixed:** `ImGuiConfigFlags_NavEnableKeyboard` was the
       source of *two* earlier bugs. It made ImGui claim the keyboard whenever any
       editor window had focus, so `io.WantCaptureKeyboard` was **permanently true**
       (silently disabling every F-key shortcut gated on it — the 16B.3 finding) and
       ImGui swallowed **Tab**, fighting the runtime UI's focus navigation (16B.6).
       Nav is now off: the editor is mouse-driven, and widget tab-nav wasn't worth
       those costs.
     - **Fixed:** the root screen is no longer poppable — backing out of the last
       screen used to leave the game showing *"No screen"*.
   - **16B.8 — focus-routed text + caret movement (DONE, verified):** closes the last
     gap. `TextInputComponent` gained an `element` id, so **typing routes to the
     focused field** — the match is `FocusComponent::focusedElement` ↔
     `TextInputComponent::element`, i.e. an **ECS lookup**, never a question asked of
     RmlUi about which widget holds the caret. Two fields (`name`, `tag`) prove it;
     with nothing focused, typing is ignored rather than hitting an arbitrary field.
     Arrow keys move the caret via `CaretLeft`/`CaretRight` intents, and inserts land
     *at* the caret. The focus ring now spans fields + buttons
     (`QuerySelectorAll(".field, button")`). Verified by screenshot: `Name: ab|`
     stays put while `Tag: z|` takes new keystrokes. Self-test covers routing,
     no-focus, caret clamping, insert-at-caret, and the snapshot round-trip
     (buffer + caret + owning element).
     - *Gotcha:* RmlUi elements are not focusable without `tab-index: auto` —
       `Element::Focus()` silently does nothing otherwise.

   **Phase 16B complete.** The design record is validated by working code, not
   intention: model (16A) → Vulkan render (16B.2) → ECS sync (16B.3) → intent-only
   callbacks (16B.4) → viewport binding (16B.5) → authoritative focus (16B.6) →
   authoritative text (16B.7) → focus-routed text (16B.8). Screen stack, focus, text
   buffer and caret all live in ECS; hover, layout and rendering are derived — with no
   exceptions. Rationale and the bugs found along the way are captured in
   **`DevDocs/RUNTIME_UI_LESSONS.md`** (why not `<input>`,
   why focus is authoritative, why callbacks only emit intents, why polling beat
   subscriptions, why the RenderInterface is hand-written, and the one ImGui flag
   behind two unrelated-looking bugs).

   - **16B.9+ — remaining (enhancement, not redesign):** `DialogueStateComponent`
     isn't modelled yet. Home/End/word-wise caret motion. The demo HUD is still a
     placeholder rather than real screens. UI advances only in Play (intents drain on
     the fixed step — by design), which makes authoring UI in Edit mode awkward.
2. **Animation — DONE (Phase 17).** Skeletal, blend trees, state machines, graphs.
   Hand-rolled playback (external libs may import data, never own playback). Same
   Rule 21 constraint: playback state (current time, active state) is authoritative →
   ECS / serializable; graph evaluation caches are derived → rebuildable.
   - **Architecture decided before code**, as with Runtime UI: see
     **`DevDocs/DESIGN_ANIMATION.md`**. The governing invariant
     is `Pose = f(clip data, playback state)` — clips are immutable assets, playback
     state lives in ECS, and the pose is *recomputed*, never stored. Rule 21 uses an
     animator hiding `currentTime` as its worked example of the bug this prevents.
     - **The record's own contribution:** it decides the gray areas *against* the UI
       record where they genuinely differ. An **animation transition mid-blend is
       authoritative** even though the identical-looking UI tween is derived — a UI
       tween can snap to target because only the eye reads it, while a transition
       determines the actual pose, and therefore root motion, hitboxes, and what the
       player sees at frame N. The general rule that falls out: *a tween is derived
       when it is only looked at, and authoritative when something else reads it.*
   - **17A — model layer (DONE):** the authoritative half, built and tested headless
     before any skinning — the sequencing that worked for 16A. `AnimationClip` +
     track/keyframe data with hand-rolled sampling (STEP + LINEAR, quaternion slerp),
     `AnimationClipRegistry` (name → immutable clip, the `BehaviorRegistry` pattern),
     `AnimationPlayerComponent` { clip, time, speed, playing, loop } as authoritative
     ECS state, and an `AnimationSystem` that advances time on the fixed step and
     writes sampled poses into transforms. Full serializer round-trip, so a
     half-played animation survives snapshot restore / time travel. All of it lives
     in **Core** — playback is pure math over plain data, which is precisely what
     makes a GPU-free self-test possible (Rule 9/15). Scheduled **after Script,
     before Physics**, so a clip-driven transform is an input to this step's
     collision rather than a step stale; it declares `R:Animation|Name|Hierarchy`,
     `W:Animation|Transform` and the self-test asserts it stays inside that
     declaration. 21/21 `SUGAR_VALIDATE` gates pass (was 20/20).
     - **Determinism detail worth keeping:** loop wrap is **modular, not
       subtractive**. `time -= duration` is the obvious version and it breaks the
       moment one step overshoots a short clip (a 0.1 s clip at speed 100 crosses
       several loops in a single step), and again for negative speed. The self-test
       pins both cases.
     - **Verified, not assumed:** the `Animation` self-test was re-run against a
       deliberately neutered `AnimationSystem::update` and it failed — so the suite
       proves the system's behavior rather than merely compiling next to it.
   - **17A cleanup — serializer optional-field emitters (DONE):** adding animation
     exposed `writeEntityObject`'s `tailAfter*` ladder as an architectural liability
     rather than a wart: ~10 hand-maintained booleans, each the OR of every optional
     component declared after it, so **one new component meant editing ten unrelated
     expressions** — and a missed term emitted invalid JSON at *runtime* (a failed
     snapshot parse), not a compile error. Exactly the coupling Rule 8 argues
     against. Replaced with `std::vector<std::function<void(std::ostream&)>> fields`:
     each present component pushes an emitter that writes its field *without* a
     separator, and one loop owns comma placement. The ladder is gone; the
     serializer's control flow no longer depends on component count, and adding a
     component is a single `push_back`. Output is **byte-identical** — the constraint
     that keeps "behavior changed" synonymous with "bug".
     - **The gap this exposed:** `testSerializer` was *not* a golden test — it only
       grepped for `"Probe"` and `"pos"`, so it would have passed through almost any
       format drift. Round-trip tests couldn't cover it either: they prove writer and
       parser *agree*, so both could drift together. It is now byte-exact over an
       entity carrying every optional component, with the expectation **hand-derived
       from the old ladder's rules** rather than captured from the new code (a
       captured expectation would only have proven the new code equals itself). It
       matched first try.
     - **Then break-tested, per the Animation precedent:** deleting one component
       from the writer must fail loudly. It did — but it *crashed the whole suite*
       (`invalid unordered_map<K, T> key`, thrown from a patched-away component),
       killing the run at test 11 of 15 so `testSerializer` never reported. **A
       throwing test now reports `FAIL ... threw: <what>` and the run continues** —
       the table you read to diagnose the failure is no longer the thing the failure
       destroys. `SUGAR_VALIDATE` still exits nonzero (verified: `19/21`, exit 1).
   - **17B — glTF clip import (DONE):** `animations` + samplers parse into engine
     `AnimationClip`s inside `GltfLoader.cpp`; tinygltf stays parse-only and no
     tinygltf type escapes the translation unit. Two shape conversions happen at the
     boundary, which is the point of having one:
     - **Channel-per-property → track-per-node.** glTF emits a channel per animated
       property; SuGar wants one `TransformTrack` per node, so channels are grouped
       by target node.
     - **Node index → node name.** glTF targets nodes by index. Resolving to names at
       import means nothing downstream depends on glTF's numbering, so a re-export
       that reorders nodes cannot silently repoint a saved scene at the wrong bone.
       Clips register as `"<path>#<clipName>"`, mirroring the `"<path>#<meshIndex>"`
       mesh key — by name, not index, for the same reason.
     - **Import attaches a *stopped* player** for the first clip. Registering clips
       with no player leaves the animation invisible until hand-wired; auto-playing
       would let the *importer* decide gameplay and would fight the editor (a model
       that pirouettes the moment you drop it in is not an authoring tool).
     - **CUBICSPLINE is approximated, deliberately.** glTF stores
       `[inTangent, value, outTangent]` per key; the real keyframe at `3i+1` is read
       and interpolated linearly. Exact *at* every keyframe, less smooth between —
       a better failure mode than dropping the channel (silently missing animation)
       or misreading the triples as keys (garbage). Full cubic evaluation lands when
       an asset needs it. Non-float rotations (glTF permits normalized byte/short)
       are skipped rather than misread.
     - **Verified + break-tested** against a hand-written fixture
       (`assets/models/AnimatedSpinner.gltf`, embedded base64 buffer, LINEAR
       rotation/translation + a STEP clip): parse → ECS import → drive the real
       `AnimationSystem` → assert the pose at t=0.5. Inverting the glTF `[x,y,z,w]` →
       glm `(w,x,y,z)` swap — the silent, ruinous one — makes it fail (Rule 9a).
       22/22 `SUGAR_VALIDATE`.
     - **Known limit, stated not discovered:** the fixture is hand-written, so the
       importer is not yet proven against a real exporter's output (interleaved
       buffer views, sparse accessors). 17C brings real skinned assets.
   - **17C.1 — skin model + joint matrices (DONE):** the design record's open
     question ("flat joint array vs. reusing the ECS hierarchy for joints")
     **answered itself** once 17A/17B existed: joints are *already* entities, and the
     AnimationSystem already poses them by writing `TransformComponent`. A parallel
     joint array would be a second representation of the same thing, able to disagree
     after a snapshot restore — the second owner Rule 21 forbids. So the ECS
     hierarchy *is* the skeleton, and the invariant is:

     ```
     Skinning = f(mesh, skeleton pose)
     ```

     `Skin` (Core) therefore carries only what ECS cannot know: joint **names** in
     joint-index order (JOINTS_0 indexes into it) plus inverse bind matrices.
     `SkinRegistry` keys them `"<path>#<skinName>"` (the AnimationClipRegistry
     pattern). `SkinnedMeshComponent` is a **reference, not state** — a name, nothing
     else. `Skinning::computeJointMatrices` is deliberately **not a system**: it
     writes no components, owns no state, and is recomputed on demand, so the
     renderer stays a pure consumer and GPU skinning remains an implementation detail
     of drawing. Nothing would change to skin on the CPU instead. glTF skin import
     does the same index→name conversion as 17B. 24/24 `SUGAR_VALIDATE`.
     - **Convention:** `jointMatrix[i] = inverse(world(skinnedEntity)) * world(joint[i]) * inverseBind[i]`.
       The leading inverse cancels the skinned node's own transform (glTF says it
       must be ignored), which lets the renderer keep applying its ordinary
       per-entity model matrix — so moving the character entity moves the character
       and skinned meshes need no special case in the draw path. An unresolvable
       joint yields identity rather than being skipped: `out` must stay parallel to
       the skin's joint order, or a hole silently re-maps every later joint.
     - **The break test earned its keep — by exposing a bad test, not bad code.**
       Reversing the multiplication order left `Skinning` **passing**. Cause: every
       case used translation-only matrices, and **translations commute**, so
       `world * inverseBind` and `inverseBind * world` are identical — the test
       literally could not see the order it existed to pin. Fixed by rotating a joint
       (rotation does not commute with translation); the reversed product then leaves
       a (-2,-2,0) offset where the correct one leaves zero. This is the Rule 9a
       failure mode in its purest form: a green test that measured nothing.
   - **17C.2 — GPU skinning (DONE, visually verified):** `JOINTS_0`/`WEIGHTS_0`
     vertex attributes, skinned scene + shadow pipelines, and joint matrices uploaded
     per draw. The ownership boundary held: the renderer gained **no** animation
     state. Poses arrive on the `DrawList` as plain matrices (derived in
     `buildDrawListFromECS`, where the ECS is still in hand), so the pass only
     *transports* them — it never asks the ECS for a pose, and never owns one.
     - **Verified by screenshot, the whole point of the phase:** a bar with two
       joints, weights blending by height. **Edit mode → perfectly straight** (bind
       pose); **Play → smoothly bent**, bottom ring (weight 1.0 Root) unmoved, middle
       ring (0.5/0.5) partially rotated, top ring (weight 1.0 Tip) swung the full
       60°, cycling 0→60→0. Linear blend skinning, driven end-to-end by
       `AnimationSystem` → `TransformComponent` → `computeJointMatrices` → shader.
     - **Bug found before it shipped:** clips and skins were registered *only* by the
       importer, so a scene **loaded from disk** kept its components and resolved
       them to nothing — animation silently dead, skinned meshes stuck in bind pose.
       Components hold *names* precisely so they can be re-resolved;
       `ModelImporter::ensureModelAssets` now does that on the scene-load and
       snapshot-patch paths, guarded by a registry lookup so scrubbing does no file
       I/O. This only surfaced because the phase insisted on driving the real app.
     - **A skinned *shadow* pipeline exists for a reason:** without it a character
       animates while its shadow stands in bind pose. The depth pass needs only
       position, but it must skin that position with the same matrices.
     - **Vertex format, stated not buried:** `joints`/`weights` live on the one
       `Vertex` (+20 B on *every* vertex, static geometry included, 32→52 B). Bought
       one Mesh, one loader, one ResourceManager entry, one buffer; a second vertex
       format would fork all of them. Both pipelines share the binding *stride* and
       differ only in declared attributes. First thing to revisit if vertex memory is
       ever measured (Rule 18) — likely unorm8 weights (+8 B) before a split format.
     - **Not repeating a known hazard:** the scene UBO is single-buffered and
       rewritten every frame with 2 frames in flight. The joint buffer is
       per-frame-in-flight instead — a torn pose is a visibly glitching character,
       and the fix costs a few hundred KiB. *(The pre-existing UBO hazard is
       untouched and still latent.)*
     - **Limits, deliberate:** 64 joints/skin and 64 skinned draws per frame (both
       clamp rather than overrun into another character's slice); `JOINTS_1` (5–8
       influences) unread; joint indices clamp to 255.
     - **Still unproven:** both fixtures are hand-written, so interleaved buffer
       views, sparse accessors and exporter quirks remain untested. **Keep both
       fixture kinds** when real exports arrive — hand-written for deterministic
       regression (only the data a behavior needs), real exports for compatibility.
       Different purposes, not replacements.
   - **17D — blend trees + state machines (DONE):** `AnimationGraph` is a data asset
     (`AnimationGraphRegistry`, name-keyed like clips and skins): states play one clip
     or a **1D blend tree**, and transitions fire on a parameter comparison or
     `OnFinished`. `AnimationStateComponent` holds the authoritative half — active
     state, phase, transition target + elapsed — and `AnimationParametersComponent`
     holds the parameters, which are **gameplay's** state that the animator only
     reads. Everything else is derived. Exactly the split the design record predicted
     in 17A, applied without amendment.
     - **The enabler was a `Pose`.** 17A sampled a clip straight into
       `TransformComponent`, which works for one clip and is *impossible* for two:
       once a pose is in the transforms, what you'd need to blend it is gone. A
       derived `Pose` value + `blendPoses` + `applyPose` made blending an ordinary
       function over data — and let the 17A player and the state machine share one
       definition of "apply a pose", rather than two.
     - **Phase, not seconds — the one thing the record didn't predict.** A blend tree
       mixes clips of different lengths (a walk is slower than a run); advance them by
       wall-clock seconds and the feet slide, because each reaches its foot-plant at a
       different moment. `statePhase` is normalized `DevDocs/DESIGN_ANIMATION.md`**.
3. **QA + hardening pass (DONE, between Phase 17 and 18).** Before starting a new
   subsystem, stabilise the last one and clear known debt:
   - **Scene-UBO write-while-in-flight race fixed.** The scene uniform buffer was a
     single copy rewritten every frame while the GPU could still be reading the
     previous frame's (the fence only guarantees the frame *two* submissions ago).
     Now one slice per frame-in-flight, bound `UNIFORM_BUFFER_DYNAMIC` with the
     frame's offset — the same lifetime model 17C.2 used for the joint buffer.
     Verified with validation layers active: zero messages across live rendering.
   - **Shadow bug found + fixed (separate from the race).** `shadow.vert` declared
     binding 0 as `{ mat4 lightSpaceMatrix; }`, but the shadow pass binds the *same*
     descriptor set 0 as the scene pass, whose UBO starts with `view` — so std140 put
     `lightSpaceMatrix` at byte 128 and the shader was reading `view` at byte 0,
     rendering the shadow map from the **camera** instead of the light. A shared
     descriptor set means every shader's UBO block must mirror the real byte layout;
     both shadow shaders now declare the full `UniformBufferObject`. Screenshot diff:
     ~400k viewport pixels changed, shadows now present on the floor.
   - **Dead code removed:** `src/scene/Scene.h` + `src/scene/GameObject.h` — the
     pre-ECS scene graph, unreferenced since the Registry replaced it, and carrying a
     second `getWorldMatrix` overload that only invited confusion.
   - **First animation stress coverage:** `AnimationScale(400)` — 400 characters
     (players + state machines + blend trees) over 600 steps, asserting bit-identical
     determinism across two runs and snapshot survival at scale. 26/26 `SUGAR_VALIDATE`.
4. **Navigation — DONE (Phase 18).** The third M3 platform item, and the third
   subsystem to have its architecture decided **before** any code:
   **`DevDocs/DESIGN_NAVIGATION.md`**. The governing invariant
   is `Route = f(navmesh, start, goal)` — *and* the deliberate counterweight to it,
   which is what the record exists for: **following a route is state, not a cache.**
   - **The record's own contribution — a path is authoritative.** The tempting
     classification is that a path is derived (*"it's just `f(navmesh, position,
     goal)`, recompute it after a restore"*), and it is wrong in a way that looks
     exactly like the reasoning that is *right* for a pose. Apply the engine's
     determinism test: an agent that planned at a corridor fork and took the left
     route, versus one replanning from halfway down it, can legitimately choose
     differently — both optimal, both legal, and the two runs now disagree. A pose is a
     function of the **present** (`clip`, `time`); a path is a function of the **past**.
     - The general rule this exposes, which retroactively explains 17D's
       `transitionElapsed` as well: ***a cache is derived only if it is a function of
       the current state. A value computed once from a past state is a function of
       history, and history is authoritative.*** The test to reach for is not "is this
       expensive to recompute" but "does recomputing it need information that no longer
       exists." Three records, one underlying reason.
     - The failure mode if you get it wrong is the nastiest kind: nothing crashes,
       agents move, every path is valid. Only a scrub-and-compare shows two runs
       diverging — precisely the guarantee time travel exists to provide.
   - **18A — model layer (DONE):** the authoritative half, built and tested headless
     before any baking — the sequencing that worked for 16A and 17A. All of it in
     **Core**, because planning is pure math over plain data (Rule 9/15).
     - **`NavMesh`** — convex polygons over a shared vertex array, with per-edge
       adjacency **derived from the geometry** rather than stored in the asset: an
       asset that carried its own neighbor table could carry a *stale* one, which is
       Rule 21's problem wearing an asset costume. Convexity is load-bearing, not
       tidiness — it is what makes "a straight line inside one polygon is walkable"
       true, which is what makes the funnel *correct* rather than merely plausible.
       Containment is winding-agnostic (same side of every edge), so a bake exporting
       clockwise polygons cannot silently report the whole mesh as unreachable.
     - **`NavMeshRegistry`** — name → immutable mesh, the `AnimationClipRegistry`
       pattern, with adjacency rebuilt at registration so no registered mesh can
       disagree with itself.
     - **Deterministic A\*** over polygons, costed between **portal midpoints** (a
       centroid measure over-charges long thin polygons and picks visibly silly
       corridors through them). Ties break on `(f, polygon index)` — a **total** order,
       so `std::priority_queue`'s lack of stability cannot leak into *which* of two
       equal-cost routes comes back. Determinism by construction, not by hope.
     - **Funnel string-pulling**, kept as a separate pass from the search. Without it
       agents walk polygon-center to polygon-center — the classic zig-zag that makes a
       correct search *look* broken. The corridor and the waypoints are different
       things, not refinements of each other: local avoidance (18D) will need to steer
       *within* the corridor, so neither is hidden inside the other.
       - *Non-obvious detail:* the portal's left/right endpoints are ordered
         **geometrically** (against the crossing direction), not by polygon winding. A
         clockwise bake would otherwise mirror every funnel decision and produce paths
         hugging the **outside** of corners — legal-looking output that is quietly
         wrong. The test pins it: an L-shaped mesh must yield exactly one bend, at the
         concave corner.
     - **`NavAgentComponent`** — `navMesh` name, destination, **path, pathIndex,
       pathGoal, status**, speed, arrival radius. `pathGoal` (the destination the
       current path was planned for) makes "should I replan?" a *comparison against
       authoritative state* rather than a `dirty` flag every behavior that moves a
       destination must remember to set; a forgotten flag is an agent walking
       confidently to the wrong place, and one `vec3` deletes the class (Rule 7).
     - **`status` is authoritative because "we already tried" is not recoverable.**
       Without it an agent with an impossible goal re-runs A* over the whole mesh every
       fixed step forever, and a restore silently restarts that attempt — so the same
       frame does different work in two runs. Structurally the same call 17D made for
       animation events: *the record of an attempt is state, even when the attempt is a
       pure function.* The deliberate consequence, stated rather than discovered: a
       failed path is **not** retried until gameplay re-arms it.
     - **Scheduled after Script, before Animation and Physics** — gameplay decides
       *where* to go, navigation moves the character, animation then *depicts* the
       movement rather than racing it, and the navigated position is an input to this
       step's collision rather than a step stale. Declares `R/W: NavAgent|Transform`
       and the self-test asserts it stays inside that declaration.
     - **A step must spend its whole travel budget.** "Move toward the next waypoint,
       advance if reached" silently caps an agent at one waypoint per frame, so a fast
       agent crawls through a cluster of close waypoints — the same shape as 17A's
       subtractive loop wrap (`time -= duration` breaks the moment one step overshoots
       the clip). The self-test pins it with a single step long enough to cross the
       entire route.
     - **Break-tested three ways (Rule 9a),** each red for its intended reason:
       inverting the funnel's left/right convention, capping the agent at one waypoint
       per step, and dropping `path` from the *parser*. The third is the informative
       one — it fails the **Navigation** restore test while the byte-exact
       **Serializer** golden test stays green, which is the correct split: the golden
       test pins what is written, and only a restore test can pin what is read back.
       **27/27 `SUGAR_VALIDATE`** (was 26/26), Debug and Release.
   - **18B — navmesh baking (DONE):** scene geometry becomes a navmesh, and the Rule 21a
     obligation 18A inherited is discharged.
     - **The split is the design.** `buildNavMesh` (Core) takes a **world-space triangle
       soup and nothing else** — no `Mesh`, no `ResourceManager`, no Vulkan;
       `NavMeshBaker` (Engine) is the only navigation code that knows those exist and
       harvests the triangles. Same boundary `GltfLoader` draws for animation. Worth
       insisting on for a reason this session proved: a bake taking `Mesh` (which
       includes `vulkan.h`) would be untestable headlessly, and the coverage audit below
       found exactly what that costs. *Testability is a property of where the boundary
       goes, not something added to an algorithm afterwards.*
     - **Rule 21a, discharged better than clips got it.** `NavMeshSourceComponent` names
       the navmesh a piece of geometry feeds, so **the scene carries its own bake
       inputs** — a loaded scene rebuilds its navmesh from nothing but the entities it
       just created. No side file, nothing to keep in sync.
       - **And it exposed a limit in the rule's usual shape.** A navmesh is derived from
         the *whole scene*, not one asset file, so it cannot be reconstituted per-entity
         the way `ModelImporter::ensureModelAssets` is: every source entity must exist
         *and be parented* first, because the bake reads world transforms. So
         reconstitution is a **post-load step**, not an inline one. Rule 21a says
         something must rebuild the asset; it never said that something runs per
         component, and navigation is the first case where it cannot.
     - **Welding is load-bearing, not cleanup.** `buildAdjacency` matches edges by
       *vertex index*, so triangles that merely touch share no edge until welded. An
       unwelded bake makes every triangle its own island and every path `Unreachable` —
       which reaches a user as *"pathfinding is broken"*, not *"the bake is wrong"*.
       Hence `NavBakeStats::isolatedPolygons`: the statistic exists to name the cause of
       the symptom (Rule 1).
     - **Winding decides floor from ceiling** (signed normal vs. +Y), so a downward-
       facing surface is rejected rather than becoming ground you stand on from below.
       Degeneracy is checked **twice** — on area, then again *after* welding, since
       welding can collapse a thin-but-valid triangle into a line, and a polygon with a
       repeated index would match its own edge in `buildAdjacency`.
     - **Triangles stay triangles, deliberately.** Merging coplanar neighbours (Recast
       does) buys no correctness: the funnel string-pulls a triangulated floor into the
       same straight line a merged one gives, so merging only shrinks the A* node count.
       An optimization, and nothing has measured the search (Rule 18). The test pins the
       claim — cross a triangulated plane, assert **one** waypoint.
     - **An empty bake is never registered**, because that would be a *cached failure*:
       `has(name)` would go true and convince `ensureBaked` the work was done. And
       `ensureBaked` must not re-bake a name already registered — without that check the
       post-load hook overwrites a good navmesh with the current harvest. *A hook that
       destroys the thing it exists to restore is worse than the bug it fixes*, so it
       gets its own test, break-tested.
     - **Verified against the real app**, per the 17C.2 discipline: the ground cube bakes
       to `12 triangles harvested → 10 rejected as too steep → 2 polygons, 4 vertices`
       — the top face, welded, with the entity's 10×0.5×10 world scale applied. Headless
       tests cannot cover the harvest (it needs a device); driving the editor can.
     - Break-tested (Rule 9a): disabling welding and removing the `ensureBaked` guard
       each turn `NavMeshBake` red while `Navigation` stays green — the isolation is
       itself evidence the two tests cover different things. **29/29 `SUGAR_VALIDATE`**,
       Debug and Release.
   - **Coverage gap found and closed along the way.** `SceneSerializer::loadFromString`
     could not run headless *at all* — it failed even for an entity holding only a name
     and a transform, because `loadTextureWithFallback` reached for the built-in
     checkerboard outside its own `try`, and `loadSceneFromText` turned the throw into a
     bare `false`. **No component's scene-load path had ever been tested**, which is
     precisely where Rule 21a's worked example (17C.2) went wrong. Fixed by making
     headless an *explicit* state (`ResourceManager::isInitialized()`, Rule 13) instead
     of an exception, reporting what the catch caught, and adding a **`SceneLoad`**
     self-test over every optional component. Break-tested: deleting one component's
     `add` in `createEntitiesFromObjects` turns `SceneLoad` red while `Serializer` and
     `Navigation` stay green — the clearest possible demonstration the gap was real, and
     the general lesson: *a golden test pins what is written; only a load test pins what
     is read.*
   - **18C — editor (DONE):** the first navigation work that is a *workflow* decision
     rather than a runtime-correctness one — which is exactly why it lives in the editor.
     - **Navigation panel** — navmeshes with polygon/vertex counts, live `NavBakeStats`,
       and warnings that name *causes*: isolated polygons point at `weldEpsilon`,
       all-triangles-rejected points at winding/slope, and an agent whose navmesh is
       unbaked is told so by name (the Rule 21a symptom is otherwise a component that
       looks perfectly correct and does nothing). Lists names that have *sources but no
       bake*, since the entry you most need to see is the missing one.
     - **Explicit Rebake, deliberately.** `ensureBaked` only fills a *missing* navmesh,
       so editing geometry leaves it stale. Rebuilding automatically on every geometry
       edit is a performance trade nobody has measured at scene scale — choosing it now
       would be the speculative optimization Rule 18 rejects. A button keeps the
       trade-off **evidence-driven**: when a real project finds manual rebaking annoying,
       that annoyance *is* the evidence, and the button is what a policy would replace.
     - **Overlay is ImGui, not a Vulkan pipeline** (Rule 11): navmesh polygons and agent
       paths project to screen and draw through `ImDrawList`. No renderer change, no
       shader, no pipeline state — and the overlay stays a pure *read* of ECS plus the
       registry. Off by default for the mesh, since a navmesh covers the floor you are
       trying to edit.
     - **Authoritative fields are shown, not editable.** Destination/speed/arrival radius
       are editable (what gameplay would set); `status`, `path`, `pathIndex` are
       read-only — editing a path index by hand is editing the middle of a decision. *Set
       Destination* goes through `setDestination`, so repeating a previously
       `Unreachable` goal actually re-plans.
     - **Overlay bug worth keeping, and it generalizes to any editor gizmo:** a polygon
       with a corner **behind the camera** must be **clipped, not rejected**. A point
       with `w <= 0` projects to a *mirrored* position in front of the viewer; the
       tempting fix — skip any polygon with such a corner — fails exactly when it
       matters, because standing on a large ground quad drops every corner behind you and
       the navmesh vanishes at the moment you are close enough to care. Fixed with
       Sutherland–Hodgman against the near plane (per-segment trimming for polylines).
       Found by **instrumenting, not guessing** (DEV_ENVIRONMENT #3): one run logging
       projected corners showed `(0,0) (0,0) (754,105)` — two silently failing to
       project — which staring at screenshots would not have revealed.
     - **Verified by screenshot**, the point of the phase: panel reporting
       `level - 2 polys, 4 verts` with `2 polygons, 4 vertices, from 12 triangles;
       10 rejected as too steep`, the `Systems` panel showing
       `Navigation R:Transform|NavAgent W:Transform|NavAgent` at Stage 1, an agent
       reading `following 0/1` in Play, and the baked top face drawn filled + outlined in
       the viewport.
   - **18D — erosion + local avoidance (DONE) → PHASE 18 COMPLETE.** The pipeline, and
     the separation that keeps it reasonable:

     ```
     agent-radius erosion ──► A* ──► corridor ──► local avoidance ──► steering
        (bake time)                                  (per step)
     ```

     - **Erosion before planning, avoidance after** — erosion changes the traversable
       space itself (a property of the navmesh, baked once); avoidance responds to
       transient conditions. Collapsing them gives a planner that disagrees with the
       space it plans in.
     - **The rule worth holding onto: avoidance changes _how_ an agent traverses its
       corridor, never _which_ corridor it chose.** An agent stepping aside for a moving
       crate is still on its planned route and rejoins it. If avoidance could edit
       `path`, a transient condition would overwrite a planning decision — two things
       with completely different lifetimes sharing one piece of state. The self-test
       asserts the plan is byte-identical at *every step* of a detour, not just at the
       end.
     - **The bug the phase found: repulsion is not avoidance.** Pure radial push means
       an obstacle squarely between agent and waypoint gives `desired + push == 0` — the
       agent **stops dead a clearance-width short and never arrives**. Repulsion answers
       *"get away from it"* when the question is *"get **around** it"*. Each obstacle now
       adds a **tangential** term sided toward the goal; the tangent turns a standoff
       into an orbit. Dead-ahead ties break on a fixed sign, never a random one — a
       random nudge would break replay for the case that most needs to reproduce.
     - **Avoidance is derived, and Rule 21b says exactly why:** it is a pure function of
       the *present* (positions, radii, desired direction), so recomputing it cannot give
       a different valid answer. The contrast with `path` — authoritative for precisely
       the opposite reason — sits in the same component. The boundary is named in
       advance: if reciprocal oscillation ever needs a remembered "preferred side", that
       preference **is** history and must become an ECS field, not a private member.
     - **Obstacles are not baked into the mesh.** Baked geometry says where an agent
       *may* go; an obstacle is a condition met on the way. Baking them in would mean
       rebaking whenever one moved. `NavObstacleComponent` carries only a radius — the
       position is the entity's ordinary transform, never a second copy.
     - **Erosion is polygon-granular and defaults to off.** It drops whole polygons
       within the radius of a boundary rather than offsetting and re-triangulating, so it
       over-erodes by up to one polygon's width — coarse but predictable. Off by default
       because erosion that switched itself on would silently shrink an existing navmesh
       on the next rebake. Two radii exist deliberately:
       `NavBakeParams::agentRadius` answers *where may I plan?*, `NavAgentComponent::radius`
       answers *how close may I pass?*.
     - **`ViewportOverlay` extracted (editor infrastructure, not navigation's).** The
       18C near-plane clipping is now a shared `Projector`: physics contacts, audio
       ranges, camera frusta and AI perception cones all need it, and each would
       otherwise rediscover the same bug. The framing that makes it obvious: `w <= 0`
       does not mean *projection failed*, it means **the primitive crosses the near
       plane** — so the pipeline is `clip → project → draw`, never
       `project → if it failed, skip`. Deliberately free of ImGui and Vulkan, so it is
       pure math and **headless-testable** despite being editor code.
     - **The guard rail caught a real mistake:** iterating `registry.navObstacles.getAll()`
       off a *non-const* Registry binds the non-const overload, which records a **write**
       — so the system mutated a storage it declared read-only, and Phase 13B enforcement
       rejected it. Fixed with a `const Registry&` view (the CollisionDispatch idiom).
       Exactly the hidden-coupling class the declarations exist to surface.
     - Break-tested three ways (Rule 9a), each red for its intended reason: removing the
       tangential term, disabling erosion, and rejecting instead of clipping at the near
       plane. In each case the *other* navigation tests stayed green — the isolation is
       itself evidence the tests measure different things. **31/31 `SUGAR_VALIDATE`**
       (was 29/29), Debug and Release. Obstacle overlay verified by screenshot.
   - **Not built, deliberately:** off-mesh links (jumps/ladders — asset data plus a
     status, no state-model change), full 3D multi-layer queries (a *baking* concern;
     the polygon soup already supports stacked floors), crowd simulation and
     agent-to-agent avoidance, true polygon-offset erosion, automatic navmesh
     invalidation on geometry change, and hierarchical/portal-graph search for large
     worlds (Rule 18 — measure first).

   **Phase 18 complete.** Ownership was settled before any algorithm, and held under all
   of them: 18A the state model and deterministic planning → 18B asset generation and
   reconstruction → 18C editor tooling *without* editor ownership → 18D erosion and
   avoidance slotted either side of planning. The editor remained a consumer of
   navigation state rather than a participant in navigation, and no phase had to revisit
   an earlier one's decision — the same progression Animation followed in Phase 17.

5. **Asset pipeline — IN PROGRESS (Phase 19).** The fourth M3 platform item, and the
   one the other two depend on: packaging exports what cooking produced, and the build
   pipeline runs the cooker.
   - **Architecture decided before code:** see
     **`DevDocs/DESIGN_ASSET_PIPELINE.md`** — the governing
     invariant is `Cooked = f(source bytes, import settings, cooker version)`, with
     `Runtime = f(cooked)`. Asset identity stays the normalized path key (Rule 21a);
     **GUIDs are rejected** because an id database is a function of *history*, not of
     the present (Rule 21b), and text scenes must stay diffable.
   - Ownership split settled first: `AssetDatabase` (catalog + `.meta` + staleness),
     `AssetCooker` (source → cooked, headless, deterministic), `ResourceManager`
     (runtime instances + GPU). Editor consumes the catalog; it never owns cook state.
   - Phasing: **19A** database + `.meta` sidecars + content-hash staleness →
     **19B** cooked formats and cache, runtime stops parsing source formats →
     **19C** importer maturity (settings applied, reimport, dependency edges) →
     **19D** editor Asset Browser surface.
   - **19D done — Phase 19 complete.** The editor surface, built *around* the pipeline
     rather than inside it. Asset Browser: click a tile to inspect; read-only bookkeeping
     (content hash, cook key, cooked artifact path, `.meta` present) above editable
     *intent* (per-type import settings — scale / flipY / gain — plus Apply and
     Reimport); catalog problems surfaced in a warning banner; References /
     Referenced-by lists that walk the dependency graph.
   - **The boundary that mattered:** the editor *requests* work, it never performs it.
     `AssetReimport::reimport` is the one implementation of importing, called by the
     file watcher (`force=false`, so a touched-but-unedited file costs nothing) and by
     the editor (`force=true`, because "nothing changed" is the state Reimport exists to
     escape). No editor-only shortcut exists — that is how "works when I save the file
     but not when I press the button" bugs are born. The editor sets a request string;
     `SuGarApp` performs it next frame, outside the render frame and with the device
     idle, since a reload destroys GPU resources.
   - Editor holds no asset state: only the selected key and the pending request. Cook
     keys, edges and settings are read from the database each frame.
   - Self-test `AssetReimport`; shown to fail (Rule 9a) by ignoring `force` (proved by
     deleting an artifact behind the cooker's memo — only a forced reimport clears it)
     and by dropping the `.meta` -> owner mapping.
   - Verified in the running editor by screenshot (`PrintWindow`), not by assertion
     alone: the panel shows the audio Gain widget, the cooked artifact path, and the
     "no dependencies discovered" note for an asset with none.

   - **19C done.** Import settings are applied at *cook* time — `scale` (model),
     `flipY` (texture), `gain` (audio) — so the setting is baked into the artifact, the
     runtime does no per-load work, and invalidation falls out of the 19A key formula
     (the `.meta` bytes are already hashed) rather than being coded. Malformed values
     cook with the default: a `.meta` is hand-edited, and a typo must not stop an asset
     from loading. Dependency edges make the catalog a graph **without moving
     ownership**: `AssetDatabase` stores them (`dependenciesOf` / `dependentsOf`),
     `AssetCooker` discovers them (only it parses glTF) and *reports* them rather than
     keeping a second table, and `ResourceManager` never learns why an artifact was
     rebuilt. Edges are derived (Rule 21b) — `scan()` clears them, nothing serializes
     them. Editing a `.meta` now reimports its asset (`assetKeyForMetaPath` maps the
     sidecar the catalog deliberately does not list back to its owner). Self-test
     `AssetImport`; shown to fail (Rule 9a) by ignoring the `scale` setting and by
     letting edges survive a rescan.
     - **Bug the phase surfaced:** tinygltf is built with image decoding off (it must not
       clash with the engine's stb_image), and without a loader callback it treated an
       image it could not decode as a **parse failure** — so any glTF that merely
       *referenced* an external texture failed to load entirely, taking its nodes,
       materials and animations with it. No repo model had an external texture, so
       nothing had ever exercised it. Fixed with an explicit no-op image loader in
       `GltfLoader`: URIs are recorded, pixels come from the cooker.
     - **The rule that keeps edges honest later:** if a cooked artifact ever *embeds* a
       dependency's content, that dependency's cook key must be folded into the
       artifact's key. Today none do, so edges are reachability metadata (what packaging
       ships, what the editor shows).
   - **19B done.** `CookedAsset` (the `.sgc` container: magic, format version, cooker
     version, kind, payload — every scalar written little-endian byte by byte, because a
     struct dump would bake in this compiler's padding and quietly turn "recooks
     byte-identically" into "on this toolchain") and `AssetCooker` (cook keys, the
     `build/assetcache` cache, cook-on-demand). `ResourceManager` now reads cooked
     artifacts only: glTF, OBJ, stb_image and audio decoding all moved behind the
     cooker, and its `normalizeResourceKey` stopped calling `weakly_canonical` — that
     resolved symlinks, making a key depend on machine-local filesystem layout, the same
     objection that rejected GUIDs. `SUGAR_COOK=1` is a headless cook-and-exit gate (no
     window, no device) — the build pipeline's "cook, then build" already works.
     Self-test `AssetCooking` cooks a hermetic OBJ/TGA/WAV tree into two caches and
     compares bytes; shown to fail (Rule 9a) by putting a timestamp in the header and by
     dropping the sub-selector from the artifact key.
     - **Layering correction the design record now carries:** the cooker cannot live in
       Core. It parses source formats (tinygltf/stb/miniaudio, which Rule 15 keeps out
       of Core) and produces `Mesh`/`Texture`/`AudioClip` (Vulkan headers). *Needing no
       GPU* was the real requirement; *living in Core* was a guess at how to get it.
     - An animation-only glTF (`AnimatedSpinner.gltf`) has no geometry to cook. `cookAll`
       skips it rather than failing: a build that exits nonzero over a legitimate asset
       teaches developers to ignore the build.
   - **19A done.** `AssetPath` (the identity function, specified in the design record
     and implemented once), `AssetHash` (FNV-1a + the `CookerVersion` counter that any
     format *or hash algorithm* change bumps), `AssetMeta` (`.meta` sidecar read/write,
     deterministic bytes), `AssetDatabase` (catalog, cook keys, problem reporting) —
     all in Core and headless. The old `AssetRegistry` is gone; the editor browser and
     the hot-reload path read the database, and the file watcher's mtime signal is now
     only a trigger to check the content hash: touching a file no longer reloads it.
     Self-test `AssetDatabase` covers normalization, `.meta` round-trip, catalog
     determinism and staleness; shown to fail (Rule 9a) by dropping the case fold in
     `AssetPath` and by dropping the content hash from the cook key.

6. **Packaging — IN PROGRESS (Phase 20).** The fifth M3 item, and the first that
   *consumes* the asset pipeline rather than extending it. Design record written before
   code, as always: `DevDocs/DESIGN_PACKAGING.md`.
   - **The expensive decision: the manifest.** In the editor the runtime names a cooked
     file by hashing the *source*; a shipped build has no source. So the packager records
     `resourceKey -> artifact hash` at package time, and the runtime resolves through
     that manifest and cooks nothing. `Runtime = f(cooked)` finally reaches a
     double-clickable build. Not a new identity (Rule 21a/21b): same keys, same hashes,
     reproducible from source, so `AssetCooker` gained a packaged mode, not a second
     naming scheme. Dev-vs-packaged is decided by one fact — does a manifest sit next to
     the executable — with no build flag.
   - **A package is a graph walk, not a copy of `assets/`.** Roots are the shipped scenes;
     `SceneSerializer::collectAssetKeys` (the serializer owns the format, so it lists the
     asset fields) yields referenced keys; the 19C dependency edges close over them
     (a model reaches its textures); each is cooked and copied, its `key -> hash` added
     to the manifest. Derived navmesh keys are excluded (rebuilt on load); built-ins are
     excluded (procedural).
   - **Honest gap (Rule 18):** animation clips and skins are still reconstituted from
     source, so a package using them ships the source model too, and the packager
     *reports* every such key rather than dropping it (Rule 13). Cooked clip/skin
     artifacts wait on a dogfooded game exercising them.
   - `AssetManifest` is Core (a headless text map). `Packager` is Engine (drives the
     cooker), headless under `SUGAR_PACKAGE=1`. `ResourceManager` is unchanged — it
     never learns whether an artifact came from source or a manifest.
   - Self-test `Packaging`: cook + package a scene, then **delete the whole source tree**
     and resolve every key from the manifest alone, reading the artifacts back. Shown to
     fail (Rule 9a) by making packaged mode ignore the manifest, and by dropping the
     `albedo` texture field from `collectAssetKeys`.

7. **Build pipeline — DONE (Phase 21). M3 COMPLETE.** The sixth and final floor item,
   and the first that introduced *no new subsystem* — it orchestrates the headless gates
   19 and 20 already built. `DevDocs/DESIGN_BUILD_PIPELINE.md`.
   - `scripts/build_release.ps1` (+ `.sh`): `cmake --build` (Release) then
     `SUGAR_PACKAGE=1`. Device-free end to end, so it runs on a headless CI box.
   - **Binaries close Phase 20's gap:** `Packager::collectRuntimeBinaries()` ships the
     exe + its DLLs (enumerated, `*_live_*.dll` excluded), so a package is a runnable
     standalone, not assets alone. The one bit of platform code (exe-path lookup) is
     isolated in `Packager`.
   - **Every build verifies:** packaging ends with `Packager::verify` — load the
     manifest, put `AssetCooker` in packaged mode, resolve every key with no source or
     database. `SUGAR_PACKAGE` exits nonzero if any key fails, so a broken package fails
     the build loudly. Same invariant the Phase 20 self-test pins by deleting the source
     tree, now enforced on every build, reusing the real runtime path (no second
     "does it work?" implementation).
   - Self-test `Packaging` extended: `verify()` passes on a good package and fails when
     an artifact is deleted; shown to fail (Rule 9a) by making packaged `ensureCooked`
     return a missing path instead of reporting. Also made `AnimationImport` /
     `SkinImport` skip when their fixture is absent, so `SUGAR_VALIDATE` is
     cwd-independent — the shipped exe passes it from the package directory.

**M3 is complete: a developer can build a typical indie game and ship a standalone
without extending the engine.** Next: freeze the platform and dogfood (M4). On this
completion, publish the Runtime UI design as a standalone article (see below).

### Hardening pass — input trust boundary (2026-07-28)

M3-complete, pre-dogfood: a QA/security sweep of the untrusted-input surface. Notable
finding — the surviving defects were *all* at the boundary where external bytes enter
(the deserializers), none in the deterministic engine logic, which needed no
architectural change. Healthy distribution for software reaching a production baseline.

- **scene.json — stack overflow.** The hand-rolled recursive JSON parser
  (`SceneSerializer`) had no depth guard; a deeply nested file overflowed the stack — an
  *uncatchable* `EXCEPTION_STACK_OVERFLOW` on Windows. scene.json sits on the runtime path
  (startup + every hot reload), so this was the highest-priority item. Fixed with an RAII
  `DepthGuard` (MaxDepth 256 — far above any real hierarchy, low enough to bound stack use).
- **glTF — heap OOB reads.** The accessor readers trusted tinygltf to have validated
  accessor → bufferView → buffer references and byte ranges; it validates *syntax*, not
  semantic consistency between them. A malformed model read past its backing buffers.
  Fixed with one overflow-safe `validateAccessorSpan`, routed through every accessor read,
  plus an `index < vertexCount` check — that one protects the *renderer* from malformed
  geometry, not the loader. glTF is a cook-time surface (a dev importing third-party art).
- **cooked `.sgc` — reserve DoS + size overflow.** The container reader `reserve()`d on
  attacker-controlled counts (`length_error` → `terminate`) and computed `width*height*4`
  in a way that could overflow before GPU upload. Fixed by deriving every size from the
  payload length — the authoritative fact — and validating before allocating.

Made a **permanent regression, not a one-off fuzz**: the `MalformedInput` self-test is now
part of the gate, so every `SUGAR_VALIDATE` run asserts a JSON bomb, an OOB-accessor glTF,
and a bogus-count `.sgc` all fail cleanly. Count is now **37/37** (was 36). Robustness
note in `DevDocs/DESIGN_ASSET_PIPELINE.md`. Feature gap left open (not a security issue):
sparse glTF accessors unsupported.

### Pre-freeze: crash reporting (2026-07-28)

The one piece of infrastructure worth adding *before* freezing the platform and starting
M4 — because the payoff is entirely in M4. When a game under test dies after hours of play,
a crash left with no context is a lost afternoon. **Deliberately not an engine subsystem** —
`src/CrashHandler.cpp`, exe-only (platform code stays out of Core, Rule 15), allocation-free
in the handler.

- Installs a `SetUnhandledExceptionFilter` first thing in `run()`. On a fault it writes a
  timestamped pair to `./crashes/`: a **minidump** (`.dmp`, opens in Visual Studio / WinDbg
  with the matching `.pdb` — the authoritative artifact) and a **human-readable `.txt`**:
  engine version + git commit (baked in at configure time via a generated `BuildInfo.h`),
  OS / CPU / GPU, the loaded scene and package, the exception code, and a symbolized stack
  walk (DbgHelp).
- `SetThreadStackGuarantee(64 KiB)` so the filter still runs after a *stack overflow* — the
  one case where the remaining stack would otherwise be too small to act (defence in depth
  alongside the scene-JSON depth guard).
- No-op on non-Windows. Verified end to end (null-deref → dump written with correct metadata
  and a `SuGarApp.cpp:line` stack). `crashes/` is gitignored.

That closes the pre-freeze list. **The platform is frozen; M4 (dogfood) begins.**

---

## Deferred / future

Scheduled explicitly *later* so they aren't lost.

**How an item leaves this list (added 2026-08-19).** Not by age, and not by someone deciding
it is time: a workload or an architectural lifetime constraint has to make the complexity
worth spending. The first pass under that rule retired three items and refused several more:

| Item | Outcome | What promoted it |
| --- | --- | --- |
| World-label depth occlusion | **built** | Nameplates showing through arena walls; resolved as an opt-in layer mask because "solid" is a game's word, not the engine's |
| Failed-replan backoff (**#43**) | **built** | 40 agents on an unreachable goal measured at **4 800 A\* searches** in 120 steps; per-agent cooldown took it to **160** |
| Stencil/clip masks (**#44**) | **built**, but *not* as stencil | The arena's translucent pause panel measured a **53 RGB** interior darkening; stencil was then rejected on evidence (the UI pass is `D32_SFLOAT`, effect layers have no depth) in favour of a coverage texture |
| Generational entity ids | **designed, then built** (2026-08-19) | The audit's one "widen now" row. `DevDocs/DESIGN_GENERATIONAL_IDS.md` settled 20/12 bit packing by measurement and found the audit's premise half wrong — no file stores an entity id — so nothing had to be migrated when it landed. Built as its own phase behind L3 game 2, exactly as the record scheduled it. Gate 63 → **64/64** |
| CCD, `MAX_SKINNED_DRAWS`, batched-submit upload, binary snapshots, greedy face-merge, async chunk generation, `MAX_LIGHTS` clustering | **still deferred** | Each measured as not-forced. A projectile tunnels at 400–800 m/s and the game throws at 34; skinned draws cap at 64 and a game has never passed 24 |

The pattern worth keeping: **an item can be promoted and then have its first implementation
rejected by its own evidence.** #44 is the example — the reproduction justified the work, and
the same investigation then ruled out the obvious mechanism.

Still explicitly later:

**Engine**
- **Binary / delta snapshots** — *evidence-gated, not assumed.* The M2 benchmark
  showed JSON snapshots are fine to ~50 entities (18 MiB ring, sub-ms), and that the
  first thing to break as scenes grow is per-frame *save cost*, not memory (500 ent →
  5.6 ms/frame; 2000 → 26 ms, exceeding a 60 Hz frame). Revisit with encode/decode
  speed as the acceptance criterion, past a few hundred entities. Re-run
  `SUGAR_BENCH` to decide.
- Better scheduler parallelism (the `stages()` analysis exists; nothing is provably
  independent yet, and async fights time-travel — opt-in per system only).
- Networking / multiplayer (a non-goal for M3).

**Editor**
- Native gizmo (replace ImGuizmo), viewport overlays, better profiler, graph editors,
  better docking layouts.

**Runtime UI**
- UI asset importer, UI animation, data binding, UI event system.

**Rendering**
- Modern PBR improvements, GPU profiler, occlusion culling, LOD, animation rendering,
  compute pipeline.

---

## Deferred architecture notes

Small, deliberate "later, not now" items:

- **`Transform::getWorldMatrix()`** — `Transform` owns only its *local* matrix;
  `Registry` walks the hierarchy to compose world matrices
  ([Registry.h](src/ecs/Registry.h) `getWorldMatrix`). Eventually `Transform` can own
  a cached world matrix + dirty flags. The free-function approach is fine until it isn't.
- **Physically relocate Core-owned files under `src/core/`** — the Core *library*
  boundary is enforced by CMake/compilation, but files still live in their original
  folders. A `src/core/{ecs,math,assets,components,...}` tree would make the layer
  legible on disk. Communicates intent; not technically required.
- **Physics:** boxes are axis-aligned (rotation ignored in collision); physics bodies
  should be top-level. Contact point is the pair midpoint (fine for sfx/triggers).
- **Broadphase cell size follows the population's median**, so it *moves* when the mix of
  shape sizes changes — measured flipping 0.34 → 0.99 mid-run as pickups outnumbered
  projectiles, roughly tripling the candidate tests the projectiles pay. A percentile or a
  smoothed estimate would steady it; nothing forces the choice, and picking a constant
  without a workload is the speculation the freeze exists to prevent
  (`DevDocs/DESIGN_BROADPHASE_SCALE.md`).
- **Blur runs after the clip mask**, so a blurred box-shadow smears a little coverage back
  into the hole the mask punched — measured at ~7 RGB of the original 53. Inherent to
  blur-after-mask and what other backends do; masking *again* after the composite would fix
  it and nothing has asked (`DevDocs/DESIGN_UI_CLIP_MASK.md`).
- **Entity identity is ~~designed but unbuilt~~ built** (2026-08-19).
  `DevDocs/DESIGN_GENERATIONAL_IDS.md` specified 20-bit index / 12-bit generation packed into
  the existing `uint32_t`, and stated the one decision that makes it more than a typedef
  change: undo must resurrect an exact packed id *including its generation*, so the staleness
  guard protects gameplay reuse and explicitly not the editor's time machine. That is what
  shipped, and the comment saying so lives in `EntityManager::createEntityWithId` rather than
  only in the record. What stays open is smaller and sharper: **`Registry::isAlive()` is an
  implemented seam no game calls yet**, and `reset()` deliberately clears generations (a
  handle held across a scene load is still undetectable) because carrying them would make the
  same scene load twice produce different handles and break run-to-run comparison (Rule 10).
  A game that persists a handle across a scene load reopens that, and would need a
  deterministic generation seed rather than a carried counter.

---

## Long-Term Goal

SuGar should become an engine where changing gameplay code, assets, or data **never
requires restarting the editor**. Everything supports: live editing, live debugging,
live profiling, live asset updates, live code reload, deterministic replay,
reproducible bugs.

SuGar is not trying to be the largest engine — it's trying to be one of the
**cleanest, easiest-to-extend, fastest-to-iterate** modern C++ game engines.

---

## Appendix — Completed milestones

Collapsed for reference; full M3 phase detail is in **Phase detail — M3** above, and the
full phase-by-phase history is in git.

### M1 — Engine Foundation (done)
- **Rendering** — Vulkan forward renderer, offscreen viewport → ImGui dockspace,
  shadow mapping (PCF); cross-platform texture loading via stb_image.
- **ECS** — authoritative, data-oriented registry; handle-based `ResourceManager` +
  asset hot reload; JSON serialization.
- **Runtime (Play mode)** — snapshot/restore, fixed-60 Hz update loop, Play/Pause/Stop.
- **Behaviors + input** — stateless name-registered behaviors (state in components),
  named input actions/axes, built-in PlayerController.
- **Physics** — semi-implicit Euler, gravity, box/sphere collision (broadphase →
  narrowphase → impulse), restitution + Coulomb friction, collision events.
- **Prefabs + glTF import** (tinygltf, parse-only) → ECS hierarchy with quaternion
  transforms + PBR-factor materials; "Import to Scene" auto-prefab.
- **Audio** — hand-rolled mixer over miniaudio (device + decode only); spatial
  attenuation; `AudioClip` as a first-class asset; collision-triggered one-shots.
- **Editor UX** — scene picking, gizmos (ImGuizmo), undo/redo, duplicate/delete,
  multi-select, hierarchy reparenting, component add/remove, prefab revert/apply,
  asset thumbnails.

### M2 — Developer Iteration (done)
- **Editor command system** — transactional history, command compression, persistent
  command IDs; later made id-remap unnecessary (see below).
- **Time travel** — snapshot ring-buffer (~10 s), timeline scrubbing + frame stepping,
  bookmarks, `ISnapshotStorage` abstraction, ECS query console (`EntityQuery`).
- **Code hot reload** — layered `Editor → Engine → Core`; Vulkan-free `SuGarCore`
  shared lib; gameplay in a `SuGarGame` DLL linking only Core; live hot-swap on
  rebuild with state preserved.
- **Opinionated scheduling** — systems declare read/write component sets; deterministic
  `SystemScheduler`; access enforcement (Warn in-editor / `SUGAR_STRICT` fail-fast);
  editor Systems panel; independence analysis (`stages()`) for future parallelism.
- **In-place state restore** — snapshot restore patches live entities instead of
  rebuilding, so selection / inspector / undo survive scrub + Stop.
- **Stable entity recreation** — recreate into original ids (`createEntityWithId`),
  which let the entire entity-remap layer be *deleted* (more code removed than added).
- **Physics broadphase** — deterministic uniform-grid spatial hash (replaced O(n²));
  ~2000 bodies ≈ 1.9 ms.
- **Tooling** — `SUGAR_SELFTEST` (subsystem sanity), `SUGAR_STRESS` (scale/edge
  invariants incl. grid-vs-brute-force), `SUGAR_BENCH` (profiling, csv/json), unified
  under `SUGAR_VALIDATE` (one command, CI exit code).

### M3 — Engine Platform Complete (done)
- **Phase 16 — Runtime UI (RmlUi)** — player HTML/CSS UI; `UI = f(ECS, input)`, RmlUi a
  view, UI state authoritative in ECS, intents-only callbacks, poll not subscribe.
- **Phase 17 — Animation** — skeletal playback, glTF clip/skin import, GPU skinning, blend
  trees, state machines; all state in ECS, poses derived.
- **Phase 18 — Navigation** — navmesh assets, deterministic A* + funnel, authoritative agent
  plans, scene-geometry bake, agent-radius erosion, local avoidance.
- **Phase 19 — Asset Pipeline** — one path→key identity fn, `.meta` sidecars, content-hash
  cooker (cache = `f(source, settings, cooker version)`), runtime reads only cooked
  artifacts, dependency graph, editor import surface.
- **Phase 20 — Packaging** — standalone export: cooked artifacts a scene reaches + a manifest
  the shipped runtime resolves keys through, no source tree.
- **Phase 21 — Build Pipeline** — `scripts/build_release.ps1` = `cmake --build` +
  `SUGAR_PACKAGE`; self-verified standalone, no GPU.
- **Pre-freeze** — input-hardening pass (deserializer bounds checks; `MalformedInput` gate) +
  crash reporting (`CrashHandler`: minidump + text report).

### M4 — Dogfood (active, and not a phase with a near end)

Games get built on the engine until it is a serious one. **M4 is open-ended by design**, and
the question is never "have we shipped enough games?" — it is *"are real games still exposing
important weaknesses?"* If yes, keep dogfooding. A fixed end date would work against the
method, because the method is the evidence.

- **Level 1 (done)** — Pong, Breakout, Flappy Bird, Asteroids. 10 engine boundary features
  forced (all by Pong), zero architecture rewrites, gate held 38/38. Detail:
  `E:\Sugar Engine - Games\Level 1\Report.md`; forced changes in the **M4 friction log** above.
- **Level 2 (done)** — top-down shooter, an Asteroids content pass, a 2D platformer (forced
  nothing) and a survivors-like (forced the snapshot-capture policy). Gate 40 → 47/47.
- **Level 3 (in progress)** — *core game mechanics at real-game scale*, not a single game.
  Game 1, the voxel/Minecraft-like: done, and it forced seven engine seams plus seven defects
  (#16–#36), gate 47 → 56/56. Game 2, the combat arena (chosen by `DevDocs/PLATFORM_AUDIT.md`
  because animation, audio and collision had never been driven by a game): **done**, and the
  first L3 game to force **no new seam** — four defects (#37–#40), gate 56 → 57/57. Its
  adversarial pass found **#41** (one large collider degrading the broadphase to all-pairs)
  and closed the audit's remaining unproven cells; a second increment found **#42** (the
  runtime UI's interactive half, unreachable by any game). A **deferred-backlog pass** then
  worked three long-standing items down by evidence: world-label occlusion, **#43** replan
  backoff, **#44** clip masks — plus a design record for generational entity ids that was
  deliberately *not* implemented at the time. Gate 56 → **63/63**. **Generational entity ids
  were then built as their own phase** (2026-08-19), on the schedule that record set: packing
  and `isAlive()` landed with no artifact, format or golden change, and the migration itself
  produced **#45** (double destroy reissuing a live id), **#46** (a game's `-1` handle
  fallback reaching `destroyEntityTree`) and **#47** (a rejected game module retried every
  frame — 204 times in 10 s — once the new Core ABI stamp started refusing stale DLLs).
  Gate 63 → **64/64**.
- **Level 4 (not started)** — the *other branch* of M4, not L3's successor: advanced systems
  and quality, opened by a workload rather than by L3 running out. Rendering beyond the forward path,
  high-DPI/4K and dynamic resolution, vendor features (FreeSync/G-Sync, DLSS/FSR), GPU-driven
  culling and async upload, plus the L3-deferred optimizations if a workload forces them.
