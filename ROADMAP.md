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
(`docs/DESIGN_RUNTIME_UI.md` + `docs/RUNTIME_UI_LESSONS.md`) as a standalone article —
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

**Level 3 — The real game: voxel / Minecraft-like. IN PROGRESS.**
- First slice runs: first-person, a chunked voxel world, gravity + game-side voxel collision,
  raycast break/place. Forced **three architectural seams** — each designed as a record first
  (`docs/DESIGN_RUNTIME_MESH.md`, the `AssetGateway` + `CameraComponent` designs), never
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
- Write-up: `E:\Sugar Engine - Games\Level 3\Report.md`.
- Remaining (measurement-gated, not built): greedy face-merge, per-block colour, voxel-edit
  persistence (a game-data component — A3-adjacent), cursor capture for continuous mouse-look.

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
synthetic `runtime://mesh/<id>` key). Designed first (`docs/DESIGN_RUNTIME_MESH.md`): CPU data is
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

**Minecraft (L3) — measurement tooling.** *Forced (soft):* the per-block vs chunk decision needed
numbers, and the windowed app has no capturable FPS. *Change:* opt-in `SUGAR_FPSLOG=1` prints
FPS + drawn-entity + draw count to stderr each second (the L2-noted missing profiler overlay).
*Ref:* `SuGarApp::mainLoop`.

---

## Phase detail — M3 (Phases 16–21)

The per-phase engineering record for M3, each with its architecture decided first (the
`docs/DESIGN_*.md` records) and gated by `SUGAR_VALIDATE`. Summary bullets are in the
Milestones appendix; the detail below is the reference.

1. **Runtime UI (RmlUi) — DONE (Phase 16).** It led M3, and not merely because it's a
   bounded library integration. Without it you *cannot* build a proper game (menus, pause, settings,
   HUD, health, inventory, dialogue), and the temptation is to reach for
   `ImGui::Begin("HUD")` — violating the engine's own architecture. It is the *last
   missing piece of the platform*, so it leads.
   - **Architecture decided before code:** see
     **`docs/DESIGN_RUNTIME_UI.md`** — the governing
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
   **`docs/RUNTIME_UI_LESSONS.md`** (why not `<input>`,
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
     **`docs/DESIGN_ANIMATION.md`**. The governing invariant
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
       different moment. `statePhase` is normalized `docs/DESIGN_ANIMATION.md`**.
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
   **`docs/DESIGN_NAVIGATION.md`**. The governing invariant
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
     **`docs/DESIGN_ASSET_PIPELINE.md`** — the governing
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
   code, as always: `docs/DESIGN_PACKAGING.md`.
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
   19 and 20 already built. `docs/DESIGN_BUILD_PIPELINE.md`.
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
note in `docs/DESIGN_ASSET_PIPELINE.md`. Feature gap left open (not a security issue):
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

Scheduled explicitly *later* so they aren't lost:

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

### M4 — Dogfood (active)
- **Level 1 (done)** — Pong, Breakout, Flappy Bird, Asteroids. 10 engine boundary features
  forced (all by Pong), zero architecture rewrites, gate held 38/38. Detail:
  `E:\Sugar Engine - Games\Level 1\Report.md`; forced changes in the **M4 friction log** above.
- **Level 2 (in progress)** — top-down shooter done (forced #11 flat-colour material tint +
  #12 mouse input; everything else reused the L1 surface; gate 38→39/39). Remaining: 2D
  platformer, survivors-like.
