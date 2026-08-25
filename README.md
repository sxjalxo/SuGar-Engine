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
* **Level 3** (voxel/Minecraft-like, combat arena, turn-based dungeon crawler) — under way.
  It asks whether each core mechanic a game is built from can be built on the engine as it
  stands. Seams forced so far include camera-as-a-component, an asset-acquire seam,
  runtime meshes, lights as components, and game-defined per-entity data.

The recurring result is worth stating plainly: **most engine defects are found by playing a
game, not by reviewing code** — a navmesh welder keyed on a formatted string (104 → 10 ms),
a runtime-mesh upload that was 91 % Vulkan object churn (`vkAllocateMemory` calls 20 740 → 2),
a draw list spending 17 ms a frame drawing 16 000 zero-scaled particles, half of every shadow
map discarded for as long as the shadow pass had existed.

Correctness gate: **66/66**, Debug and Release.

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
# ... [validate] === 66/66 checks passed, 0 failure(s) ===
```

Individual harnesses (`SUGAR_SELFTEST`, `SUGAR_STRESS`, `SUGAR_UITEST`, `SUGAR_BENCH`,
`SUGAR_STRICT`, `SUGAR_COOK`, `SUGAR_PACKAGE`) and what each one covers are documented in
[FEATURES.md](FEATURES.md#verification--tooling).

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
