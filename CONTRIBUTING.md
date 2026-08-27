# Contributing to SuGar Engine

Thanks for looking at SuGar. This document is the short version of how the engine is
built and what a change has to satisfy to land.

SuGar is a hand-rolled C++17 / Vulkan engine built around one bet: **win the inner
development loop** — hot reload, time travel, debuggable systems — rather than chase
feature parity with the big engines. That bet shapes what gets accepted, so it is worth
reading [`README.md`](README.md) and [`ROADMAP.md`](ROADMAP.md) before opening anything.

---

## Read these first

Two documents are **contracts**, not suggestions. A pull request that violates them will
be asked to change, however good the code is:

| Document | What it governs |
| --- | --- |
| [`DevDocs/RULES.md`](DevDocs/RULES.md) | The 23 architectural rules and the merge Decision Checklist |
| [`DevDocs/REQUIREMENTS_AND_SCOPE.md`](DevDocs/REQUIREMENTS_AND_SCOPE.md) | Every dependency and subsystem: why it exists, what it may do, what it may **not** do |

The rules most likely to affect your first PR:

* **Rule 1 — developer iteration comes first.** Before anything else: *does this make
  developers faster?*
* **Rule 3 / 4 — engine systems are hand-rolled.** ECS, physics, animation, scheduler,
  serialization, resource management, hot reload and scene management are built in
  house. Libraries solve *infrastructure* (windowing, image decode, audio device, glTF
  parsing, math) and are confined to one layer. A PR that replaces a hand-rolled system
  with a library will be declined.
* **Rule 9a — a test must be shown to fail.** See [Testing](#testing).
* **Rule 15 — Core stays independent.** `SuGarCore` must compile with no Vulkan, no Dear
  ImGui, no renderer, no editor.
* **Rule 19 — documentation is part of the feature.** `README.md` and `ROADMAP.md` are
  updated in the *same* PR, not later.
* **Rule 21 — no hidden authoritative state.** Runtime state that affects gameplay,
  determinism, replay, hot reload or time travel lives in ECS components or is fully
  reconstructible from serialized ECS state.
* **Rule 23 — reference engines are studied per problem, under their license.** See
  [Provenance](#provenance--read-this-before-you-look-at-another-engine). This one has
  legal weight.

---

## What SuGar accepts

The engine is developed **evidence-first**. Since M4 it is driven by dogfooding: real
games are built on it (outside this repository), and engine work is what those games
*force*. Every seam added since M4 exists because a game could not be finished without it.

Practically, contributions sort into three buckets:

**Readily accepted**

* A **confirmed defect** with a reproduction — a failing self-test, a measurement, a
  crash report from `crashes/`, or a scripted repro.
* A **measured** performance fix. Rule 18: measure first, then optimize. Include the
  before/after numbers and how you got them.
* Documentation that is wrong, stale, or missing.
* A capability a **real game demonstrably needed** — say which game, what it could not
  do, and what it did instead as a workaround.

**Discuss before you write code** — open an issue first

* A new subsystem, or a new external dependency.
* Anything that changes an on-disk format (`scene.json`, cooked `.sgc`, package
  manifests, `.meta` sidecars). These are what snapshots, time travel and shipped
  packages ride on; the `Serializer` golden test exists to make such changes deliberate.
* Anything touching the `Editor → Engine → Core` layering or the game-module ABI.
* Porting to a non-Windows platform. Wanted eventually, large, and needs a plan.

**Usually declined**

* Speculative features — "an engine should have X". If no game forced it, it is not
  forced. This is the most common reason a proposal is turned down, and it is not a
  judgement on the idea.
* Feature-parity work imported from another engine's feature list. Rule 23 forbids
  reversing the pipeline from *reference engine has it* to *SuGar should build it*.
* Broad reformatting, renaming sweeps, or refactors with no behavioural or architectural
  argument attached.
* Replacing a hand-rolled system with a third-party one (Rule 3).

Not sure which bucket you are in? Open an issue and ask. That costs a comment; a rejected
PR costs your weekend.

---

## Build

**Platform.** Windows / MSVC / Vulkan is the only supported target today. The engine
compiles as C++17. Linux and macOS are not supported yet — see above.

**Requirements**

* Vulkan SDK
* CMake 3.21+
* Visual Studio 2022 (Desktop development with C++)

All other dependencies are **vendored** under `external/` — no package manager, no
submodules, no network fetch at configure time.

```powershell
cmake -S . -B build
cmake --build build --config Debug --target SuGarEngine --parallel 1
```

Run from the repository root so assets resolve:

```powershell
build\Debug\SuGarEngine.exe
```

A Release build plus a self-verified standalone package is one script:

```powershell
scripts\build_release.ps1
```

### Toolchain traps that cost real time

**[`DevDocs/DEV_ENVIRONMENT.md`](DevDocs/DEV_ENVIRONMENT.md) is the full list** — every
entry in it cost real debugging time, and it carries the reasoning and measurements behind
each one. **Read it before debugging a build or a GUI issue.** It also documents the
screenshot/keypress recipes for driving the window from a script, and the current env-knob
table.

Three of them block a first build, so they are repeated here. None is a bug in your change:

1. **CMake configure sometimes needs two runs.** A fresh configure can fail resolving the
   vendored FreeType dependency (`Freetype could not be found`). Running configure again
   immediately succeeds. Known issue — try twice before debugging it.
2. **The GUI build produces no capturable stdout.** `SuGarEngine.exe > log.txt` yields an
   empty file. For runtime diagnostics, log to a file, or use `SUGAR_FPSLOG=1`, which
   prints to **stderr** precisely because stderr *is* capturable. The headless gates
   (`SUGAR_VALIDATE`, `SUGAR_SELFTEST`, …) print normally.
3. **A stale `Game.dll` is the most expensive trap in this repo.** The M4 game module is a
   DLL linking only `SuGarCore`, built separately, and nothing checks that the two agree.
   Both configs emit the same `<gameDir>/Game.dll`, so MSBuild can leave a Release module
   under a Debug engine; and any change to a Core type the game passes by value or holds
   by reference changes the ABI. The symptom either way is an access violation inside
   `BehaviorRegistry::registerBehavior` or `BehaviorRegistry::has`, at startup, nowhere
   near the mistake. Delete the DLLs and rebuild the game after any engine header change,
   building the config you are about to run **last**:

   ```bash
   rm -f Game.dll Game_live_*.dll && cmake --build <gameBuildDir> --config <Config>
   ```

   Related: `--target SuGarEngine` does not always rebuild `SuGarCore` for that config.
   When Core headers moved, use `--clean-first`.

Two more are worth knowing before you file a bug: **Runtime UI only advances during Play**
(intents drain on the fixed step, so buttons and text entry look dead in Edit mode — by
design), and **an unhandled exception writes `crashes/crash_*.txt`** with the version, git
commit, machine, loaded scene and a symbolized stack. Read that file before debugging
anything else, and attach it to bug reports.

One trap belongs to *writing* changes rather than building them: **never script-edit a test
file.** A bulk regex sweep over `src/SelfTests.h` once silently deleted six assertions;
everything compiled and the suite still reported PASS — *because* the assertions were gone.
A test suite verifies the code, and nothing in the suite verifies the suite. Hand-edit
tests with unique anchors, and always read the diff of test files.

---

## Testing

**Every change runs the gate, in both configurations, before you open a PR.**

```powershell
$env:SUGAR_VALIDATE = "1"; build\Debug\SuGarEngine.exe;   $env:SUGAR_VALIDATE = ""
$env:SUGAR_VALIDATE = "1"; build\Release\SuGarEngine.exe; $env:SUGAR_VALIDATE = ""
```

`SUGAR_VALIDATE` runs every correctness gate — self-tests *and* stress tests — and exits
nonzero if any fail. It is headless: no window, no GPU. Paste the final line
(`=== N/N checks passed, 0 failure(s) ===`) into your PR. A nonzero exit is not
mergeable, and the check count must not go **down**.

Other harnesses, when you want just one:

| Knob | What it does |
| --- | --- |
| `SUGAR_SELFTEST=1` | per-subsystem PASS/FAIL table with timings |
| `SUGAR_STRESS=1` | scale and edge inputs; broadphase vs an O(n²) oracle, determinism, restore churn |
| `SUGAR_UITEST=1` | headless RmlUi integration smoke test |
| `SUGAR_BENCH=1` | profiler over a representative scene (`SUGAR_BENCH_FORMAT=csv\|json`) |
| `SUGAR_STRICT=1` | Debug only: the first undeclared component access throws and exits nonzero |
| `SUGAR_COOK=1` / `SUGAR_PACKAGE=1` | headless asset cook / standalone export |
| `SUGAR_SNAPDBG=1` | per-capture time-travel snapshot phase breakdown to stderr |
| `SUGAR_SNAP_BUDGET=<ms>` | overrides `SnapshotCapturePolicy`'s 4 ms capture budget, for measurement runs |
| `SUGAR_SNAP_CORPUS=<path>` | dumps snapshot bytes to disk each capture — **never combine with a timing run** |

Run benchmarks in **Release** or the numbers are fiction.

### Rule 9a — show the test fail

A green test proves nothing until you have watched it go red *for the intended reason*.
Before you trust a new test, temporarily break the thing it covers and confirm the test
fails, and fails because of that.

This is not ceremony. Reversing the skinning multiplication order once left the
`Skinning` test **passing**, because every case used translation-only matrices and
translations commute — the test could not see the one property it existed to pin.
Rotating a joint gave it teeth. **A test can be blind to the property it claims to check,
and being green tells you nothing about which.**

State in the PR what you broke and what went red.

### Rule 9b — round trips are necessary, not sufficient

`write → read → compare` proves the writer and the parser agree with each other. It says
nothing about the format. Where an external contract exists — an on-disk format a
snapshot or a shipped package rides on — pin it with a **golden test** whose expectation
is derived *independently of the implementation under test*. Generating the expectation
by running the new code proves only that the code equals itself.

`Serializer` is that golden test: it pins byte-exact scene text. It is deliberately
brittle. If your change makes it fail, either the change is wrong, or the format change
is intentional and the commit says so in as many words.

---

## Code style

There is no `.clang-format` yet; match the file you are editing.

* 4-space indent, no tabs. Opening brace on the same line.
* `#pragma once` in headers, not include guards.
* `PascalCase` types, `camelCase` functions and variables, `SCREAMING_SNAKE` macros.
* Include order: standard library, then third-party, then engine headers by path
  (`"ecs/Entity.h"`), each group separated by a blank line.
* Forward-declare in headers where you can — `Registry` is forward-declared, not included.
* **Comments explain *why*, not *what*.** The existing code documents its reasoning and
  the defect a piece of code exists to prevent, often citing a design record or a defect
  number. Match that. A comment restating the line above it is noise; a comment recording
  why the obvious approach was wrong is the most valuable thing in the file.
* Keep `SuGarCore` free of Vulkan, ImGui, renderer and editor includes (Rule 15).

---

## Adding a dependency

The bar is high, and the process is fixed:

1. **Open an issue first.** Say what solved-infrastructure problem it handles and why the
   hand-rolled alternative is not the right call (Rules 3 and 4).
2. **Check the license.** It must be compatible with Apache-2.0 for redistribution: MIT,
   BSD, Apache-2.0, zlib, ISC, public domain / Unlicense / MIT-0 are fine. Anything
   copyleft (GPL, LGPL, AGPL) or source-available-but-restricted is not.
3. **Vendor it under `external/`,** with its license file intact. No submodules, no
   package manager, no configure-time downloads.
4. **Add a scope entry to `DevDocs/REQUIREMENTS_AND_SCOPE.md`** answering the three
   questions that file exists to answer: why are we using it, what is it allowed to do,
   what is it *not* allowed to do.
5. **Confine it to one layer.** Rule 4: a library supports a system, it never becomes the
   system.
6. **Add it to [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).**

---

## Provenance — read this before you look at another engine

SuGar is published, and written about publicly. That makes the *provenance* of its
architecture part of the product. Rule 23 governs this, and it is the one rule with legal
consequences rather than architectural ones.

**The order is never reversed:**

```
real SuGar game → observed problem → measurement → design the seam
  → research comparable architectures → choose SuGar's own solution
  → implement → test → dogfood again
```

Never *reference engine has feature → SuGar should implement feature*.

**Two depths of study, and the license decides which:**

* **Source-level study** — reading implementation to learn where a seam belongs. Requires
  a permissive license that allows it. Godot, Bevy, EnTT and Fyrox qualify.
* **Black-box study** — running it, using its editor, reading its public docs, observing
  behaviour. Always available. Never involves its source.

Source-available is **not** permissive. **Unreal Engine** (EULA) and the **Nu Game
Engine** (Noncommercial License) both restrict use in a competing engine, and both define
"competing" broadly enough to cover a free, open-source engine in a different language.
For those: **black-box only.**

For the Nu Game Engine specifically — kept as a maturity benchmark because it shares
SuGar's stated goal — permitted: running it, using Gaia, reading its public
README/wiki/docs, taking *questions* from them. **Forbidden: inspecting, searching,
grepping, copying, adapting or deriving from its source.** No code, algorithms, data
structures, APIs, naming or implementation patterns.

If you cite a reference for a seam — and Rule 22 asks you to — say what you took and from
which of its **docs**. Do not contribute code derived from a source tree you were not
licensed to read for this purpose.

---

## Games live outside this repository

Since M4, dogfood games are not part of the engine tree. A game is a directory containing
a `scene.json`, its `assets/`, and a `Game.dll` of behaviours built against `SuGarCore`.
The engine boots one by directory:

```powershell
$env:SUGAR_GAME = "<gameDir>"; build\Release\SuGarEngine.exe
```

`SUGAR_PACKAGE=1` with `SUGAR_GAME` set emits a standalone under `<gameDir>/dist`.

Rule 16: the engine never contains project-specific gameplay. Please do not send PRs
adding a game to this repo — send the engine change the game forced, and describe the
game in the PR.

---

## Commits and pull requests

**Commit messages** follow Conventional Commits:

```
feat(ecs): add generational entity handles and ABI validation
fix: correct input edge lifetime across frame and step domains
docs: ...
perf: ...
refactor: ...
test: ...
```

Scope is the subsystem (`ecs`, `physics`, `render`, `ui`, `nav`, `assets`, `editor`,
`audio`, `anim`). The body explains *why*, and cites the defect number, design record or
measurement behind it. Keep the subject under ~72 characters, imperative mood.

**Pull requests**

1. Branch from `main`. One logical change per PR.
2. Fill in the PR template. The Decision Checklist in it comes from `RULES.md` and is what
   the change is actually judged against.
3. Include the `SUGAR_VALIDATE` result line for **both** Debug and Release.
4. Say what you broke to prove your new test can fail (Rule 9a).
5. Update `README.md` and `ROADMAP.md` in the same PR (Rule 19). Update
   `DevDocs/REQUIREMENTS_AND_SCOPE.md` if a dependency or a subsystem boundary moved, and
   `DevDocs/RULES.md` if a *constraint* changed — that last one needs discussion first.
6. Expect review to argue about architecture more than about syntax. Rule 2: architecture
   before features. If a reviewer says the seam is in the wrong place, that is the
   substance of the review, not a nitpick.

Before marking it ready, walk the Decision Checklist from `RULES.md`:

- [ ] Does it make developers faster?
- [ ] Does it improve the architecture?
- [ ] Does it reduce complexity?
- [ ] Is it testable — and has its test been shown to fail?
- [ ] Is it deterministic?
- [ ] Does it fit the existing philosophy?
- [ ] Does it introduce unnecessary dependencies?
- [ ] Is documentation updated?
- [ ] Can future contributors understand it?

If several answers are "no", reconsider the design rather than the wording.

---

## Reporting bugs and requesting capabilities

Use the issue templates. In short:

* **Bug** — what you did, what happened, what you expected, build config, and the
  `crashes/crash_*.txt` if the engine crashed. A failing `SUGAR_VALIDATE` run is the
  strongest possible report.
* **Capability request** — name the game or project, what it could not do, and the
  workaround you used instead. A request that names no project is a wish, and will be
  handled as one.

Security issues do **not** go in the issue tracker. See [`SECURITY.md`](SECURITY.md).

---

## Licensing of contributions

SuGar Engine is licensed under **Apache License 2.0** ([`LICENSE`](LICENSE)). By
submitting a contribution you agree it is licensed under the same terms (inbound =
outbound), and that you have the right to submit it. There is no separate CLA.

If you are contributing on behalf of an employer, make sure you are permitted to.

---

## Conduct

Participation is governed by the [Code of Conduct](CODE_OF_CONDUCT.md).
