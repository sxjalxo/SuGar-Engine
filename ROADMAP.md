# SuGar Engine — Roadmap

> **Vision**
>
> SuGar Engine is a modern, hand-rolled C++ game engine built around one principle:
>
> **Make game developers iterate faster without sacrificing engine architecture.**
>
> Every major feature should satisfy at least one of:
>
> - Reduce iteration time.
> - Improve engine correctness.
> - Remove architectural complexity.
> - Increase production readiness.
>
> If a feature does none of these, it belongs later.

Companion documents: **[RULES.md](RULES.md)** (the architectural constraints — the
law), **[REQUIREMENTS_AND_SCOPE.md](REQUIREMENTS_AND_SCOPE.md)** (per-dependency
boundaries + non-goals). This roadmap is the *what* and *when*; those are the *how*
and *what's allowed*.

---

## North Star: win the inner loop, not the feature list

We will **not** beat Unity/Unreal on graphics, asset ecosystems, or marketplaces —
those are resource wars we lose. The wedge is the thing both engines neglect once a
project grows:

> **The inner loop: change → run → see result → repeat.**
> Ideal is 1–2s. Unity drifts to 10–30s at mid-scale; Unreal to minutes.
> Whoever keeps that loop *instant and debuggable* wins the hearts of indie devs.

**Positioning:** *"A Vulkan engine designed for instant iteration and debuggable
systems — not just rendering power."* Open-source, community-driven, dev-led.

**The decision lens (every feature):** *"Does this make developers faster?"*
Iteration speed / debuggability is the identity and the tie-breaker.

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

> ### ⚠ Current State — the platform's missing half
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

### M1 — Engine Foundation ✅
Vulkan renderer, ECS, editor, asset pipeline, physics, audio, prefabs, serialization.
*(Detail in the appendix.)*

### M2 — Developer Iteration ✅
Time travel, snapshot system, query console, self-tests, native code hot reload,
scheduler + architecture enforcement, in-place restore, stable entity recreation,
uniform-grid physics broadphase, benchmark + stress harnesses. *(Detail in the appendix.)*

### M3 — Engine Platform Complete  (IN PROGRESS)

> **The platform is complete when a developer can build a typical indie game
> without first extending the engine.**

This is an objective exit criterion, not a vibe — "is M3 done?" should have a
yes/no answer. It is bounded on both sides:

**Required (the platform floor):**

| Capability      | State |
|-----------------|-------|
| Editor          | ✅ done |
| Hot Reload      | ✅ done |
| Debugging (time travel / query / profiler) | ✅ done |
| Physics · Audio · ECS · Rendering | ✅ done |
| **Runtime UI (RmlUi)** | 🚧 **next** |
| **Animation** (skeletal, blend trees, state machines) | 🚧 |
| **Navigation** | 🚧 |
| **Asset Pipeline** (maturity: cooking, importers) | 🚧 |
| **Packaging / standalone export** | 🚧 |
| **Build Pipeline** | 🚧 |

**Explicitly *not* required for M3** (so the milestone can't expand forever):

- AAA rendering features (Nanite/Lumen-class)
- Networking / multiplayer
- Console ports
- Massive-world streaming
- Plugin marketplace

### M4 — Dogfood: build real games
Physics sandbox → platformer → top-down shooter. Not products — **validation**.
After M4 begins, engine work becomes *primarily driven by real projects*.

---

## Why "Engine Platform Complete", and why stop there

An engine is never "finished." Unreal still ships Nanite, Lumen, Verse, PCG, Motion
Matching; Unity ships DOTS, UI Toolkit; Godot keeps reworking rendering, physics,
animation. Chasing "complete" is chasing a horizon.

So the discipline is **knowing where to stop adding platform features and start
building games**:

- **Stage 1 — Complete the platform.** Everything *every* game is guaranteed to need
  (M3's Required list). If making a simple game requires implementing another engine
  subsystem first, the platform isn't done.
- **Stage 2 — Freeze the platform, then dogfood.** Build real games (M4). *Only* add
  engine features the game genuinely exposes as missing.

Why not wait for "complete"? Because no roadmap can predict the real friction points.
Before dogfooding, roadmap items look like *"I need runtime UI."* After, they look
like *"the UI workflow is awkward,"* *"animation transitions are clunky,"* *"the
importer should support X,"* *"the prefab workflow needs work"* — far higher-quality
items that only real use surfaces. The best roadmap past M3 comes from development
experience, not speculation.

---

## Current priorities

Ordered by the decision lens (*does this make developers faster?* — and here, *does
this unblock building a game at all?*):

1. **Runtime UI (RmlUi)** — first, and not merely because it's a bounded library
   integration. Without it you *cannot* build a proper game (menus, pause, settings,
   HUD, health, inventory, dialogue), and the temptation is to reach for
   `ImGui::Begin("HUD")` — violating the engine's own architecture. It is the *last
   missing piece of the platform*, so it leads.
   - **Architecture decided before code:** see
     **[docs/DESIGN_RUNTIME_UI.md](docs/DESIGN_RUNTIME_UI.md)** — the governing
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
   - **16B.1 � RmlUi build + link + FreeType smoke path (DONE):** RmlUi 6.3
     and FreeType are vendored under `external/`, built via CMake, and
     **static-linked into the engine only** (never Core � Rule 15). SuGar-side
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
   - **16B.8+ — remaining (later):** caret is display-only (no arrow-key movement or
     mid-string editing; `TextInputComponent.caret` supports it, the input mapping
     doesn't yet). Text entry is global rather than routed to a focused field, so
     there is exactly one text buffer. `DialogueStateComponent` still isn't modelled.
     UI advances only in Play (intents drain on the fixed step — by design), which
     makes authoring UI in Edit mode awkward.
2. **Animation** — skeletal, blend trees, state machines, graphs. Hand-rolled
   playback (external libs may import data, never own playback). Same Rule 21
   constraint: playback state (current time, active state) is authoritative → ECS /
   serializable; graph evaluation caches are derived → rebuildable.
3. Then: navigation, asset-pipeline maturity, packaging, build pipeline.

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

## Appendix — Completed milestones (M1 · M2)

Collapsed for reference; full phase-by-phase history is in git.

### M1 — Engine Foundation ✅ (Track A)
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

### M2 — Developer Iteration ✅ (Track B — the wedge)
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
