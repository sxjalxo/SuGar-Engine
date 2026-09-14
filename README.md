# SuGar Engine

**SuGar Engine** is a hand-rolled C++17 game engine on Vulkan, with GLFW and CMake.

It is built around one bet: **win the inner development loop** — instant iteration and
debuggable systems — rather than chase feature parity with the big engines. Hot reload,
time travel, deterministic simulation and a real editor come first; the feature list comes
second.

* **What it does today** → [FEATURES.md](FEATURES.md)
* **Where it is going, and why** → [ROADMAP.md](ROADMAP.md)
* **How to contribute** → [CONTRIBUTING.md](CONTRIBUTING.md)

---

## Status

| Milestone | State |
| --- | --- |
| **M1 — Engine Foundation** | done — Vulkan renderer + shadows, ECS, editor, physics, audio, prefabs + glTF, serialization |
| **M2 — Developer Iteration** | done — time travel, ECS query console, native code hot reload, deterministic scheduler, in-place restore, self-test / stress / benchmark harnesses |
| **M3 — Engine Platform Complete** | done — Runtime UI, animation, navigation, asset pipeline, packaging, build pipeline |
| **M4 — Dogfood** | **active, open-ended** — real games are built on the engine until it is a serious one |

**M3's exit criterion was objective:** a developer can build a typical indie game and ship
it *without extending the engine*. All six floor items are done. Explicitly not required,
so the milestone could not expand forever: AAA rendering, networking, console ports,
world streaming, a plugin marketplace.

**M4 is not a phase with a near end.** Games are probes, not products. An engine feature
is added only when a game forces it — never because another engine has it — and every
forced change is recorded in the friction log in [ROADMAP.md](ROADMAP.md). Games live
*outside* this repository: a game is a `scene.json`, its `assets/`, and a `Game.dll` of
behaviours built against `SuGarCore`, booted with `SUGAR_GAME=<dir>`.

* **Level 1** (Pong, Breakout, Flappy Bird, Asteroids) — complete. Ten engine boundary
  features forced, **zero** architecture rewrites.
* **Level 2** (twin-stick shooter, 2D platformer, survivors-like, content pass) — complete.
  Four renderer *seams* forced: material tint, mouse input, per-material blend modes, an
  RmlUi effects compositor. The platformer forced nothing at all.
* **Level 3** (voxel/Minecraft-like, combat arena, turn-based dungeon crawler, RTS handle
  probe) — under way. It asks whether each core mechanic a game is built from can be built on
  the engine as it stands. Seams forced so far include camera-as-a-component, an asset-acquire
  seam, runtime meshes, lights as components, and game-defined per-entity data.

The last two L3 games were **pre-registered experiments**: the contract — watch areas,
instruments, numeric promotion thresholds — frozen before the first line of game code, so
"no engine change was needed" becomes falsifiable instead of a description of how hard anyone
looked. That discipline is why the engine's own claims now carry numbers: a comment asserting
"~16x headroom" for entity-handle reuse turned out to have compared a cap against a 90-second
window rather than a play session, and measuring it is what found the units error rather than
the arithmetic.

Every cell in [DevDocs/PLATFORM_AUDIT.md](DevDocs/PLATFORM_AUDIT.md) is now answered. That
audit marks a subsystem *unproven* rather than green until a real workload has driven it
hostilely, and run to exhaustion it held: **every row it marked unproven produced either a
defect or a number, and no row it marked green produced a defect.**

The recurring result is worth stating plainly: **most engine defects are found by playing a
game, not by reviewing code** — a navmesh welder keyed on a formatted string (104 → 10 ms),
a runtime-mesh upload that was 91 % Vulkan object churn (`vkAllocateMemory` calls 20 740 → 2),
a draw list spending 17 ms a frame drawing 16 000 zero-scaled particles, half of every shadow
map discarded for as long as the shadow pass had existed.

Correctness gate: **72/72**, Debug and Release.

---

## Highlights

**Iteration**

* **Native code hot reload** — gameplay lives in a DLL over a layered `Editor → Engine →
  Core` split; recompile and it hot-swaps live, state preserved.
* **Time-travel debugging** — snapshot ring buffer, timeline scrubbing, frame stepping,
  bookmarks. Restore is *in place*, so your selection and undo history survive it.
* **Deterministic fixed-step simulation** with a scheduler that enforces declared component
  access — Debug builds fail on an undeclared touch, Release compiles the tracking out.
* **ECS query console**, editor undo/redo with transactions, and a headless benchmark suite.

**Runtime**

* Vulkan renderer with shadow mapping, per-material blend modes, and lights as components.
* Hand-rolled ECS, physics (grid broadphase, layers/masks, triggers, raycasts), and audio
  mixer.
* Skeletal animation: glTF clip/skin import, GPU skinning, blend trees, state machines.
* Navigation: navmesh bake from scene geometry, deterministic A\* + funnel string-pulling,
  agent-radius erosion, local avoidance — where an agent's *plan*, not just its position, is
  authoritative ECS state.
* Runtime UI on RmlUi, ECS-authoritative, composited into the game viewport. Dear ImGui is
  permanently reserved for the editor and never ships in a game.

**Pipeline**

* Content-hash asset cooking (`Cooked = f(source bytes, import settings, cooker version)`),
  `.meta` import sidecars, a dependency graph, and a runtime that reads only cooked
  artifacts.
* One-command standalone export and a self-verified release build.

Full detail — every subsystem, the controls, the harnesses, the world-scale limits — is in
**[FEATURES.md](FEATURES.md)**.

---

## Build & Run

**Requirements**

* Vulkan SDK
* CMake 3.21+
* Visual Studio 2022 (Desktop development with C++)

Windows / MSVC / Vulkan is the only supported target today. Every other dependency is
vendored under `external/`, so a clone builds offline.

```powershell
cmake -S . -B build
cmake --build build --config Debug --target SuGarEngine --parallel 1
```

Run from the repository root so assets resolve:

```powershell
build\Debug\SuGarEngine.exe
```

Release plus a self-verified standalone package, in one script:

```powershell
scripts\build_release.ps1
```

> **If the configure step fails on FreeType, run it again** — a fresh configure
> occasionally loses that dependency and succeeds immediately on a second pass. That and
> the other toolchain traps that cost real debugging time are in
> [DevDocs/DEV_ENVIRONMENT.md](DevDocs/DEV_ENVIRONMENT.md). **Read it before debugging a
> build or a GUI issue.**

### Verify

`SUGAR_VALIDATE=1` runs every correctness gate — self-tests *and* stress tests — and exits
nonzero if any fail. Headless: no window, no GPU, so it drops straight into CI.

```powershell
$env:SUGAR_VALIDATE = "1"; build\Release\SuGarEngine.exe; $env:SUGAR_VALIDATE = ""
# ... [validate] === 72/72 checks passed, 0 failure(s) ===
```

Individual harnesses (`SUGAR_SELFTEST`, `SUGAR_STRESS`, `SUGAR_UITEST`, `SUGAR_BENCH`,
`SUGAR_STRICT`, `SUGAR_COOK`, `SUGAR_PACKAGE`) and what each one covers are documented in
[FEATURES.md](FEATURES.md#verification--tooling).

Snapshot capture-cost measurement (`DevDocs/DESIGN_SNAPSHOT_CAPTURE_COST.md`) adds three
knobs, off by default and dev-only: `SUGAR_SNAPDBG=1` prints a per-phase capture-cost
breakdown (`total`/`null_sink`/`materialize`/`bytes`/`entities`/`ns_per_byte`) to stderr on
every snapshot capture; `SUGAR_SNAP_BUDGET=<ms>` overrides `SnapshotCapturePolicy`'s 4.0 ms
budget so a measurement run captures every step instead of pausing after a sustained run of
over-budget frames; `SUGAR_SNAP_CORPUS=<path>` additionally dumps the formatted snapshot bytes
to disk on every capture — **never set together with a timing measurement**, since the dump
runs inside the timed region and inflates `total`. A fourth knob, `SUGAR_SNAPRATE=1`, reports
snapshot *semantics* rather than cost: how many consecutive captures are byte-identical, how
many distinct states the ring actually holds, and where two consecutive captures differ. Across
~10 700 measured captures the answer was **never any** — even in a fully idle turn-based scene,
where a game counter and an idle animation clip both advance every fixed step.

`SUGAR_AUDIODBG=1` instruments the audio thread (`DevDocs/DESIGN_AUDIO_THREAD_OWNERSHIP.md`),
also dev-only and off by default: each device callback's mix duration against its own deadline
(derived at runtime from the frame count it is handed), inter-callback arrival gaps, and how long
the callback waited for the mixer mutex the gameplay thread also takes. The counters are atomics
written on the audio thread and printed from the gameplay thread — the callback never prints,
allocates or takes an extra lock, because an instrument that perturbs a real-time thread measures
itself. Contended at ~120 000 lock acquisitions per second and in a 1 000-unit game: **zero
callback overruns in 8 186 callbacks**, max lock wait 0.33 % of the deadline.

`SUGAR_PROFILE=1` is the one instrument that is **always collecting** — only its reporting is
gated (`DevDocs/DESIGN_SYSTEM_PROFILER.md`). It prints, once a second to stderr, the median and
max wall time of every named system in the fixed step, the step total measured *independently*
around the whole step, and the residual between them; the editor's Systems panel shows the same
numbers live. A tool that needs an environment variable and a restart before it can say why
something is slow is one nobody reaches for, and the cost is ~0.16 ms/step — measured against a
stubbed build, not asserted. It exists because three separate measurements were blocked without
it, and on its first run it attributed a 1 000-unit slowdown to the game's own O(N) scan rather
than to any engine system.

The same knob now prints two more lines, so a frame can be accounted for end to end: a render line
(`drawList`, `resources`, `fenceWait` kept separate as *wait* and never folded into work, `record`,
`submit`, `frameOther`) and a loop line (`input`, `fileWatch`, `moduleCheck`, `fixedStep`, `sleep`),
each against a **total measured independently** so the residual is a real number rather than a
subtraction identity. The loop line also prints **`steps/frame`** — which turned out to be the most
useful number on it: at 143 FPS against a 16.67 ms fixed step the median is **zero**, so most frames
run no simulation at all, and any statement of the form "the frame costs render plus sim" is mixing
per-step with per-frame figures. With those lines the arena's frame time accounts to within
**0.8-1.2 ms** at both 411 and 1 611 draw items, and the ~4.6 ms that previously looked like hidden
engine overhead turned out to be mostly a mean subtracted from a median plus a 1 ms sleep that
really costs 1.5 ms.

A fourth line goes one level deeper, into the region the other three agree is the dominant one.
Draw-list construction splits into `gather` / `items` / `skinning` / `sort` / `lights`, and on a
1 611-item torture scene **skinning is 73 % of it** — which makes pose resolution ~68 % of the whole
render frame. The per-joint cost is flat at **0.50 µs across a 4x change in load**, so the cost
scales with *resolved joints*, not with draw items as an earlier sweep had it. The same line
retired an optimisation before anyone built it: sorting 1 611 render items costs **0.008 ms**.

Inside skinning the line switches from timing to **counting**, because a clock read costs ~100 ns
and the question needed ~77 000 of them a frame. Per joint, per frame, the engine does **one heap
allocation, 6.5 hash lookups with string compares, and 4.25 matrix-chain levels** — none of it
memoised, not even between consecutive joints of the same skeleton. Counting ~150 000 events costs
0.5 %, where 3 212 clock reads cost 3-5 %: counts say *why* rather than *how bad*, and they survive
a change of machine.

Then the payoff. Both repeated costs were hoisted out of the per-joint loop — one subtree walk
resolving every joint name, and a world-matrix memo consulted at every level of the parent chain —
both scoped to a single call, so there is no cache invalidation to get wrong. **Skinning 6.46 → 3.41
ms, and the heavy scene went 20.0 → 15.4 ms a frame, through the 16.67 ms bar it had been missing.**
No renderer change: the cost was never fill rate or draw calls. Six instruments were built before one
line was optimised, and every suspect named by adjacency along the way measured false.

One last correction to the instrument itself: those work counters lived in two of the engine's
hottest inline helpers, and once skinning stopped calling them they were charging audio, navigation
and animation ~0.4 ms a fixed step to feed a number nobody read. Removed — the reported figures came
back bit-identical. **An always-on instrument in a hot shared path has to keep earning it.** The
heavy cell finished the sequence at **14.7 ms a frame, from 20.0 where it started.**

The same shape turned up once more, in pose application, and this time the instrument would have
cost a third of the region it measured — so it was found by **ablation** instead: make `applyPose`
return immediately, measure, revert. That attributed **63 %** of the animation system to resolving
pose targets by name, for zero instrument overhead. One shared `findDescendantsByName` now serves
both hot loops. `Animation` **3.82 → 2.94 ms a step**, and the heavy cell ends at **13.6 ms a frame
— 3.1 ms under a 16.67 ms budget it began 3.0 ms over.** Still no renderer change.

Two more, both dev-only: `SUGAR_RENDER_RES=<W>x<H>` renders the scene to an offscreen target at an
explicit resolution independently of the window — which is what makes render cost at 4K measurable
without a 4K display — and `SUGAR_NOAUDIO=1` skips opening the playback device entirely, so a
measurement run does not play a game's music into whatever else the machine is doing.

### Controls

Editor and play-mode bindings are in [FEATURES.md](FEATURES.md#controls). The two you need
first: **F6** toggles Play, **Esc** exits.

---

## Documentation

| Document | For |
| --- | --- |
| [FEATURES.md](FEATURES.md) | Every subsystem in detail, controls, harnesses, world-scale limits |
| [ROADMAP.md](ROADMAP.md) | Milestones, phase detail, and the M4 friction log |
| [DevDocs/RULES.md](DevDocs/RULES.md) | The 23 architectural rules and the merge Decision Checklist |
| [DevDocs/REQUIREMENTS_AND_SCOPE.md](DevDocs/REQUIREMENTS_AND_SCOPE.md) | What every dependency and subsystem may and may not do |
| [DevDocs/DEV_ENVIRONMENT.md](DevDocs/DEV_ENVIRONMENT.md) | Toolchain traps — read before debugging a build or GUI issue |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Build, test, style, PR process, dependency policy, provenance |
| [SECURITY.md](SECURITY.md) | Threat model, trust boundary, private vulnerability reporting |
| [SUPPORT.md](SUPPORT.md) · [GOVERNANCE.md](GOVERNANCE.md) · [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) | Getting help · who decides what · conduct |

The rest of `DevDocs/` — the `DESIGN_*.md` architecture records and `RUNTIME_UI_LESSONS.md`
— is development-local for now: working records that change with the code they describe.
They ship when the platform does. The Runtime UI design record is the first earmarked for
publication.

---

## Contributing

Contributions are welcome. SuGar is opinionated — it has written architectural rules and
pull requests are argued against them — so start with
**[CONTRIBUTING.md](CONTRIBUTING.md)**.

The short version: the engine is developed **evidence-first**. A defect with a
reproduction, a measured performance fix, or a capability a real project demonstrably
needed will get a serious look. A speculative feature usually will not — that is Rule 8
(deleting complexity is progress), not a judgement on the idea.

Every change runs the gate in **both** configurations before it is opened:

```powershell
$env:SUGAR_VALIDATE = "1"; build\Debug\SuGarEngine.exe;   $env:SUGAR_VALIDATE = ""
$env:SUGAR_VALIDATE = "1"; build\Release\SuGarEngine.exe; $env:SUGAR_VALIDATE = ""
```

---

## Project goal

SuGar Engine is developed as a **final-year project** and an **open-source engine**
demonstrating low-level graphics (Vulkan), engine architecture (ECS, resource systems),
real-time rendering, and — its differentiator — **iteration speed and runtime
debuggability**. It is not trying to become the largest engine; it is trying to become one
of the cleanest and fastest to iterate on.

---

## License

**Apache License 2.0** — see [LICENSE](LICENSE) and [NOTICE](NOTICE).

Bundled third-party components (GLFW, Dear ImGui, ImGuizmo, RmlUi, FreeType, GLM,
tinygltf, stb_image, miniaudio) are vendored under `external/` and keep their own
licenses — all permissive, none copyleft. Full attribution is in
**[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)**. FreeType is used under the FreeType
License (FTL), not its GPLv2 alternative.

Contributions are accepted under the same Apache-2.0 terms (inbound = outbound). There is
no CLA.
