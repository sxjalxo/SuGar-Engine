# Development Environment Quirks

> **Scope.** This documents *how to avoid wasting hours working on SuGar* — toolchain
> traps, verification recipes, debugging techniques. It is deliberately separate from
> [RUNTIME_UI_LESSONS.md](RUNTIME_UI_LESSONS.md), which documents *why the engine is
> designed the way it is*. The two have different lifetimes: architecture rationale
> outlives the design, these notes expire when the tooling changes. Delete entries here
> freely once they stop being true.
>
> Platform: Windows / MSVC / Vulkan. Every entry below cost real debugging time.

---

## 1. CMake configure sometimes requires two runs

**Observed.** A fresh configure occasionally fails while resolving the vendored
FreeType dependency:

```
Freetype could not be found.
Call Stack: CMake/Dependencies.cmake:18 (report_dependency_found_or_error)
```

Running configure immediately a second time succeeds:

```
-- Found Freetype::Freetype - Freetype font engine enabled
```

Reproducible from a fully clean `build/`, and after any `CMakeLists.txt` change.

**Status.** Known issue.

**Impact.** Can produce false failures in CI if only one configure pass is attempted.

**TODO.** Investigate the dependency ordering rather than relying on a second
configure. Likely suspect: the `Freetype::Freetype` ALIAS we create after
`add_subdirectory(external/freetype)` vs. when RmlUi's `find_package("Freetype")` /
`report_dependency_found_or_error` evaluates the target.

## 2. GUI applications do not produce capturable stdout

Redirecting

```
SuGarEngine.exe > log.txt
```

produces an **empty file** — not even startup logs. `Start-Process
-RedirectStandardOutput` yields 0 bytes.

For runtime diagnostics, temporarily log to a file instead:

```cpp
std::ofstream log("uidbg.log", std::ios::app);
log << "value=" << x << "\n";
```

If long-term logging is desired, add an engine logging sink rather than relying on
stdout.

*Note:* the headless gates (`SUGAR_VALIDATE`, `SUGAR_SELFTEST`, `SUGAR_STRESS`,
`SUGAR_BENCH`, `SUGAR_UITEST`, `SUGAR_COOK`, `SUGAR_PACKAGE`) print normally — this only affects the windowed app.

*Measuring a windowed run:* `SUGAR_FPSLOG=1` prints FPS + entity/draw counts to **stderr**
each second, which *is* capturable (`... 2>&1 | grep '\[fps\]'`) even when stdout is empty —
the reason it goes to stderr. It was added to decide the M4 L3 per-block-vs-chunk scaling from
numbers. Runtime env knobs a game/measurement uses: `SUGAR_GAME=<dir>` (boot an external game),
and game-defined ones like the voxel game's `SUGAR_VOXEL_W` (world size) / `SUGAR_VOXEL_AUTOPLAY`
(deterministic scripted play + a `voxel_debug.log`). Game env vars live in the game's behaviours,
not the engine.

## 3. Screenshot debugging beats coordinate guessing

Windows DPI awareness means

- PowerShell coordinates
- desktop logical coordinates
- application framebuffer coordinates

may differ. (Measured here: PowerShell is DPI-unaware, the app is DPI-aware, factor
**1.25×**.)

**This trap is still live, and it caught a verification pass on 2026-09-13.** While checking a
new editor panel, a measurement reported the app's ImGui viewport at ~1920x991 against a "real
client rect" of ~1536x792 and concluded the engine was sizing its dockspace 25 % too large. It was
not: 1536 x 1.25 = 1920 and 792 x 1.25 = 990. The *measuring script* was DPI-unaware and the app
was correct. A second pass instrumented both values inside the engine and found them agreeing in
every sample.

The rule that follows: **never compare a number measured inside the app against a number measured
by a PowerShell/Win32 call from outside it.** Print both from inside the engine, or scale
explicitly. A 1.25 (or 1.5, or 2.0) ratio between two window measurements is the signature of this
trap, not a discovery — check for it before believing the number.

Instead of guessing pixels, **instrument the engine**. Useful probes:

- `context->GetDimensions()`
- `element->GetAbsoluteOffset()`
- `context->GetHoverElement()`
- viewport-local coordinates

These immediately reveal whether the mapping is wrong. A single run logging
`hover=div#hud` settled in seconds what several rounds of pixel arithmetic could not.

**Recipe that works** for driving the editor: `Start-Process` → wait ~10s →
`SetForegroundWindow(MainWindowHandle)` (not `AppActivate` — unreliable) →
`ShowWindow(h, 3)` to maximize → act → `CopyFromScreen` → read the PNG. Synthetic keys
need `keybd_event` with a ~250 ms hold; `SendKeys` is too instantaneous.

## 4. Runtime UI only advances during Play

`RuntimeUISystem` drains UI intents during the **fixed-step simulation**. If Play has
not started:

- buttons appear inactive
- keyboard navigation appears inactive
- text entry appears inactive

Verify Play mode (**F6**, Timeline reads `LIVE`) before debugging Runtime UI. This is
by design — intents apply on the fixed step so UI state stays deterministic and
replayable — not a bug to "fix" by applying intents immediately.

## 5. Verify scripted edits

Bulk scripted edits can **partially apply**. Typical failure mode:

- parser updated
- serializer writer **not** updated

Always run

```
SUGAR_VALIDATE=1
```

immediately afterward. The self-tests are the primary safeguard against partial edits
— the one real occurrence of this was caught by a failing self-test within seconds,
not by using the UI. Prefer per-hunk edits, or assert every anchor before replacing.

## 6. Never script-edit a test file — the suite cannot detect its own dilution

A PowerShell one-liner used to strip temporary debug prints from `SelfTests.h` matched
each `[dbg]` line, then skipped to the next line ending in `;` — which for
single-line statements was the *following* line. It silently deleted **six
assertions**.

Everything still compiled. The suite still reported **PASS**. It reported PASS
*because* the assertions were gone.

```
suite detects   wrong behavior          [ok]
suite detects   fewer checks than before [no]
```

This is the asymmetry worth internalizing: a test suite verifies the code, and
**nothing in the suite verifies the suite**. Rule 9a (break-test it) proves a test
*could* fail — it says nothing about whether the test still checks everything it did
last week. A green run after an edit to test code is not evidence.

Practical defenses, in the order they actually catch things:

1. **Read the diff of test files, always.** This is what caught it.
2. **Hand-edit tests.** Use targeted per-hunk edits with unique anchors; never a
   pattern-match sweep across a file full of assertions.
3. **Watch the assertion count**, not just PASS/FAIL, when editing a test.
4. Mutation testing / coverage-drop alerts, where the tooling exists.

Related: item 5 above (scripted edits partially apply). Same root cause — bulk text
manipulation of source — but a strictly worse failure mode, because a partially
applied *engine* edit breaks a test, while a partially applied *test* edit breaks
nothing visible.

## 7. GPU resources outlive CPU objects

Destroying Vulkan buffers immediately after RmlUi releases them is unsafe:

```
vkDestroyBuffer(): can't be called on VkBuffer 0x... that is currently in use
```

Geometry may still be referenced by **in-flight command buffers** — RmlUi drops
geometry during a re-layout, mid-frame, while the previous frames are still executing.

Always retire GPU resources after the frames-in-flight delay (or after
`vkDeviceWaitIdle` during shutdown). See `RmlVulkanRenderer::ReleaseGeometry` →
`collectRetiredGeometry`.

This bug was only discovered through **live rendering with validation layers** — no
headless test would have surfaced it. It is not a Runtime UI lesson; it is a renderer
lesson, and the kind graphics programmers rediscover repeatedly. Any future subsystem
that hands GPU resources to a library on a per-frame basis inherits it.

## 8. A stale `Game.dll` is the most expensive trap in this repo

The game module is a DLL that links only `SuGarCore`. It is built separately from the
engine, and nothing checks that the two agree. Two ways that bites, both reproduced more
than once:

**Config collision.** Debug and Release both emit `<gameDir>/Game.dll`. MSBuild will skip
the link when it thinks the output is current, so a Release DLL can end up under a Debug
engine (or the reverse). The symptom is a crash inside `BehaviorRegistry::has` — long after
the mistake, and nowhere near it.

**Layout drift.** Any change to a Core type the game passes by value or holds by reference
— `NavMesh` grew a lookup grid, `World` changed its storage — changes the ABI. The DLL
built against the old header keeps loading and then segfaults:

```
Segmentation fault    SUGAR_VOXEL_AUTOPLAY=1 ./SuGarEngine.exe
```

The rule that avoids both:

```bash
rm -f Game.dll Game_live_*.dll && cmake --build <gameBuildDir> --config <Config>
```

Delete the DLLs, rebuild the game **after** any engine header change, and build the config
you are about to run **last**. When a packaged standalone crashes right after an engine
change and the engine itself passes its gate, suspect this before suspecting the change.

## 9. A timing gate can manufacture the regression it measures

The adversarial pass toggled a wall every second and logged the cost. That is correct until
the thing being measured slows the frame *past* the interval — at which point "every second"
silently becomes "every frame", the harness does 49 columns of block edits and ~245 chunk
remeshes per frame, and the profile it prints is mostly itself.

The symptom is a measurement that gets worse the worse it gets: a mild slowdown crosses the
interval, which multiplies the work, which slows it further. It looks exactly like a runaway
engine bug.

Gate a stress harness on **frames**, not wall-clock, whenever the harness's own work is part
of what is being timed:

```cpp
const int sinceToggle = data.getInt("tortureFrames") + 1;
data.setInt("tortureFrames", sinceToggle);
if (sinceToggle >= 120) { /* ... */ }
```

Same rule as #5 and #6: a probe is code, and gets the same suspicion as the code it probes.
Three of the defects found in the swim/jump probe were in the probe, not the engine.

## Startup crash inside `BehaviorRegistry::registerBehavior` means a stale/mismatched Game.dll

Two different toolchain traps produce the **same** access violation, both at startup, both
before the scene loads:

```
Exception : 0xC0000005
  #01 BehaviorRegistry::registerBehavior +0x2A
  #02 registerPlayerBehaviors  (...\CombatArena\src\Player.cpp:238)
```

The game DLL and `SuGarCore.dll` disagree about the shared registry table.

**1. `--target SuGarEngine` does not always rebuild `SuGarCore` for that config.** A Release
`SuGarCore.dll` sat four days stale while every `cmake --build build --config Release --target
SuGarEngine` reported success; a freshly built game DLL then bound to it and crashed.
*Fix:* `cmake --build build --config <cfg> --clean-first --target SuGarEngine` when Core
headers have moved, and check `ls -la build/<cfg>/SuGarCore.dll` against the game's
`Game.dll`.

**2. A game's Debug and Release build directories emit the same `Game.dll`.** The engine
loads `<gameDir>/Game.dll` by contract, so both configs write there. MSBuild compares against
its own intermediates, not that copy — so after building the *other* config, asking for this
one reports "up to date" and leaves the wrong DLL on disk. A Debug engine then loads a Release
game module.
*Fix:* `rm -f Game.dll Game_live_*.dll` before building the config you intend to run, or pass
`--clean-first`. Sizes are a quick tell (Debug ~1.3 MB, Release ~200 KB).

**Both times the crash reporter identified the frame immediately** — `crashes/crash_*.txt`
carries the stack, the config, and the commit. Read it before debugging anything else.

## 10. Comparing Debug against Release on a real game costs two rebuilds, not zero

§8 already covers the mechanism: Debug and Release both emit `<gameDir>/Game.dll`, so MSBuild
can leave the wrong config's DLL on disk for the other config's engine to load. §8's framing
is "build the config you're about to run last." This entry is the specific consequence when
the point of the run is a **Debug-vs-Release comparison**, e.g. measuring how much slower a
real game is in Debug: there is no single "last" build, because *both* configs must be
current at the moment their respective run happens. Doing this with `rm -f Game.dll
Game_live_*.dll && cmake --build <gameBuildDir> --config <Config>` once per side, immediately
before that side's run, is not optional — skipping the second rebuild silently reuses the
first config's DLL and either crashes at startup (`BehaviorRegistry::registerBehavior` /
`std::hash<std::string>`, §8's symptom) or, worse, runs and produces numbers for the wrong
configuration with no error at all.

This cost a failed run during the snapshot capture-cost investigation
(`DESIGN_SNAPSHOT_CAPTURE_COST.md`): a Debug engine run against a Release-built crawler
`Game.dll` crashed at startup inside `std::hash<std::string>`
(`crashes/crash_20260827_003106.txt`), diagnosed as exactly this collision, and resolved by
rebuilding the module Debug, running, then rebuilding it Release again before the next run.
**Budget two module rebuilds for every Debug-vs-Release comparison on a real game, not one.**

## 11. A frame time that equals the refresh interval is not a measurement

**Observed 2026-09-13**, during L4's opening resolution sweep. Two games with entirely different
cost profiles, four resolutions spanning a **17.3x** range of pixel counts, and every median frame
time came back:

```
6.945  6.946  6.947  6.949  6.956   (ms)
```

`1 / 144 = 6.9444 ms`. Those are not render costs. They are this display's refresh interval, and
the whole sweep measured a present cap.

The engine *requests* `VK_PRESENT_MODE_MAILBOX_KHR` (`Renderer.cpp` `chooseSwapPresentMode`),
which should be uncapped — but asking is not getting. A Vulkan layer between the engine and the
display can pace presentation regardless; the run detected **OBS / Medal capture hooks** on this
machine, which is the likeliest cause. The engine is ruled out by an older figure: the combat
arena once measured **368 FPS packaged**, far above 144, so it has demonstrably presented faster
than refresh on this hardware.

> **Corrected 2026-09-13, same day.** The entry originally continued "a capped median cannot
> demonstrate headroom, so the question is unanswerable". **That was wrong.** A cap hides *how
> fast* something is; it does not hide *whether it is fast enough*. If the frame rate is
> **sustained** at the cap, every frame demonstrably finished inside the cap's interval — which is
> a real upper bound on frame cost, and often lands on the right side of the threshold you were
> asking about. The L4 sweep's 144 FPS sustained at 7680x4320 proved frame cost <= 6.95 ms against
> a 16.67 ms bar, answering the question the cap appeared to block.
>
> The lesson that survives is narrower and still worth having: **a flat cost across a large load
> change means the instrument has no range there**, so treat the number as a bound and go find a
> second instrument. In that case it was `nvidia-smi` — GPU utilisation went 6 % -> 30 % and power
> 12 W -> 41 W across the same sweep, proving the work was real when frame time could not.

**What to do about it:**

1. **Check the number against `1/refresh` before believing it.** A median sitting within a
   fraction of a percent of the refresh interval, and *not moving* when the load changes, is a cap
   — the flatness across a 17x load change is the real tell, not the value itself.
2. **Close capture/overlay software before a performance run**, and say in the report whether you
   did. This is the same class of contamination as an antivirus scanning a build.
3. **If the cap cannot be removed, measure where the instrument has range** — increase load until
   the cost clears the cap, then fit only over the uncapped points and state which were excluded.
4. **Do not fall back to worst-frame** because the median is unusable. The tail is a different
   statistic answering a different question; a conclusion resting on it because the median was
   broken is resting on the wrong one.

Related: item 3's DPI trap, and the same underlying lesson — **a measurement taken through an
environment you have not characterised is a measurement of the environment.** Two of this
project's sweeps have now died this way.

## 12. Before subtracting two timings, check they are the same statistic and the same unit

**Observed 2026-09-14**, closing L4's frame-time accounting. A section of
`DESIGN_RENDER_CPU_TIMING.md` computed:

```
frame 20.1 ms  −  frameTotal 9.639 ms  −  sim 5.888 ms  =  ~4.6 ms unaccounted
```

Every term was honestly measured. The subtraction was still invalid, for **two independent
reasons**, and together they manufactured about **2.4 ms** of nonexistent engine overhead that then
took a whole instrument to chase.

**Trap A — a mean minus a median.** `20.1 ms` came from `1 / [fps]`, and `[fps]` is
`framesThisSecond / seconds`: a **mean**. `frameTotal` and `sim` are **medians**, because this
project's standing rule is median over tail. When the tail is heavy — `loopTotal` maxed at 50-59 ms
against a 17.96 ms median — subtracting medians from a mean charges the *entire* tail to
"unaccounted". Here that alone was ~2.0 ms.

**Trap B — per-step minus per-frame.** The `[profile]` line reports the fixed step **per step**. The
accumulator runs a *variable* number of steps per frame, and at 143 FPS against a 16.67 ms step
period the median is **zero steps per frame** — most frames run no simulation at all. Adding a
per-step sim figure to a per-frame render figure is adding different units. `[profile-loop]` now
prints **`steps/frame`** (median/max) for exactly this reason; read it first.

**What to do about it:**

1. **Measure the total independently, in the same statistic, at the same place.** `loopTotal` is
   one clock pair around the whole loop body. Its median minus the regions' medians is a real
   residual; a wall-clock frame rate minus a set of medians is not.
2. **Never derive the thing you are decomposing from a rate.** `1/fps` is a mean by construction.
3. **Print the multiplicity.** If a region can run zero or N times per frame, the count is part of
   the measurement, not context.

Related: `DESIGN_SYSTEM_PROFILER.md` §9.2 (a ratio whose numerator and denominator started at
different moments) and item 11 above. Same family: **the arithmetic joining two measurements is
itself a measurement claim, and it is the part nobody break-tests.**

## 13. A counter in an inline header function is per-MODULE, not per-process

**Observed 2026-09-14**, adding work counters to `Registry.h`'s hot helpers. First run reported:

```
subtreeSearches=0 nodeVisits=0 matrixHops=0
```

The accessor was an `inline` function with a **function-local `static`** in a header. On Windows
each module that includes it gets **its own copy**: `Skinning::computeJointMatrices` is compiled
into `SuGarCore.dll`, `buildDrawListFromECS` into `SuGarEngine.exe`, so the code incrementing the
counter and the code reading it were touching different objects. The delta could never be nonzero.

This was written down as a known hazard in the design record *before* the code — as a *game-DLL*
concern — and then not applied to the engine-to-Core boundary one line away. **Knowing a failure
mode is not checking for it.**

**What to do about it:**

1. **Any cross-module shared state gets ONE definition in a Core `.cpp`**, declared (not defined)
   in the header. `SuGarCore` builds with `WINDOWS_EXPORT_ALL_SYMBOLS`, so a non-inline function
   there is exported and every module shares the instance. See `src/ecs/RegistryWork.cpp`.
2. **Ask which target each side of a measurement is compiled into** before trusting it. `grep` the
   `add_library(SuGarCore ...)` and `add_executable(SuGarEngine ...)` lists — the split is not
   visible from the source files themselves.
3. **A zero reading is a result to explain, never a result to report.** This one was caught by the
   run because the counters were printed; had they only fed a ratio, it would have shipped.

Related: this project's standing "counters that cannot fire" class — nine and counting — and item
12 above, which is the same disease in arithmetic rather than in linkage.

## 14. A count is a cost estimate only if you know the unit price

**Observed 2026-09-14**, optimising joint resolution. The design predicted wins in three work
counters. The first implementation delivered **all three** and the region got **8 % slower**:

```
subtreeSearches 12 848 -> 1 606     (8.0x better)
nodeVisits      83 512 -> 16 060    (5.2x better)
skinning          6.46 -> 6.97 ms   (WORSE)
```

Two causes, both instructive:

1. **The replacement's own cost was never counted.** Trading 67 000 node visits for 3 212
   `std::unordered_map` constructions is a good trade only if a map construction is cheaper than
   ~20 node visits. It is not — it is a heap allocation plus ~10 string hashes. Flat `std::vector`s
   with a linear scan won outright at these sizes (~10 nodes, ~4 chain levels).
2. **A memo that could not fire.** `matrixHops` came back *bit-identical*, because the memo was
   consulted only at the top-level call and each joint is asked for exactly once — zero hits by
   construction. The sharing was *inside* the recursion, so the memo had to be consulted at every
   level.

**What to do about it:**

1. **Never let a count stand in for a timing when deciding whether an optimisation worked.** Counts
   locate a cost; wall time adjudicates it. Run both, and let the clock have the last word.
2. **Count the thing you are adding, not only the thing you are removing.** The design predicted
   reductions in three counters and named no counter for the maps it introduced.
3. **A counter that does not move after a change aimed at it is a finding, not noise.** The
   unchanged 54 598 named a dead memo in one line, where the timing alone said only "slower".
4. **Prefer flat containers at small N.** A hash map is an asymptotic answer to a question that, at
   ten elements, is not being asked.

Related: item 12 (the same disease in arithmetic) and item 13 (the same disease in linkage). All
three are the instrument or its interpretation being wrong while the code under test is fine.

## 15. An always-on counter in a hot shared path has to keep earning it

**Observed 2026-09-14.** Counters were added to `Registry.h`'s `getWorldMatrix` and
`findDescendantByName`, and their cost was measured **inside the one region being investigated**
(0.5 %, judged cheap). Every other caller — audio, navigation, the navmesh baker, the renderer, and
the Animation system's per-track name lookups — was never measured. Across the whole fixed step they
cost **~0.4 ms**, about 8 %.

Worse, by the time it was checked the counters bought nothing: the optimisation under study had
moved skinning onto call-local helpers that do their own counting, so the `Registry.h` increments
were feeding a total **nobody read**. Removing them changed the reported numbers **not at all** —
`subtreeSearches=1606 nodeVisits=16060 matrixHops=16060` before and after.

Nothing failed. Nothing went red. The counters kept reporting correct values, produced entirely
elsewhere.

**What to do about it:**

1. **Measure an always-on instrument at every call site, not just the one you care about.** "0.5 %
   of the region I am studying" is not "0.5 % of the engine".
2. **When you rewrite the code an instrument observes, re-ask whether it still observes anything.**
   A counter that is merely *correct* is not the same as one that is *load-bearing*.
3. **Hot inline helpers in a Core header are the worst possible home for unconditional
   instrumentation** — every caller in every module pays, and the bill never appears on the line
   you are reading.

Related: items 12, 13 and 14 — the same family, where the instrument or its interpretation is wrong
while the code under test is fine.

## Env knobs, current list

Runtime, engine:

| Knob | Effect |
| --- | --- |
| `SUGAR_GAME=<dir>` | boot an external game (scene + assets + `Game.dll` from that folder) |
| `SUGAR_FPSLOG=1` | per-second FPS, entity/item/draw counts, **and live `meshes`/`textures`/`clips` + GPU retirement depth**, to stderr |
| `SUGAR_PHYSDBG=1` | broadphase shape count, cell size, bucket count, oversized count, pair count |
| `SUGAR_PACKAGE=1` | headless standalone export to `<gameDir>/dist`; exits non-zero if anything fails to ship |
| `SUGAR_VALIDATE` / `SUGAR_SELFTEST` / `SUGAR_STRESS` / `SUGAR_BENCH` / `SUGAR_UITEST` / `SUGAR_COOK` | the headless gates |
| `SUGAR_SNAPDBG=1` | per-capture snapshot phase breakdown to stderr — `total`/`null_sink`/`materialize`/`bytes`/`entities`/`ns_per_byte` (`DESIGN_SNAPSHOT_CAPTURE_COST.md`) |
| `SUGAR_SNAP_BUDGET=<ms>` | overrides `SnapshotCapturePolicy`'s 4.0 ms budget; use a large value on a measurement run so the sustained-overrun cut-off never engages and every capture is timed |
| `SUGAR_SNAP_CORPUS=<path>` | dumps formatted snapshot bytes to disk on every capture, overwriting; **never combine with a timing run** — the dump is inside the timed region and inflates `total` (F14) |
| `SUGAR_SNAPRATE=1` | snapshot semantics to stderr every 300 captures: byte-identical consecutive captures, run-length histogram, distinct states in the 600-frame ring, and `[snapdiff]` — the first and last offsets where two consecutive captures differ, with surrounding text. Safe to combine with a timing run (it is fed after the timed region closes), but read `differing_bytes` **only** when `size_delta` is 0 — the compare is positional and an insertion misaligns everything after it |
| `SUGAR_AUDIODBG=1` | audio-thread health to stderr, once a second from the gameplay thread plus once at shutdown: `callbacks`, `overruns` (mix duration >= its own deadline, derived at runtime from the frame count the callback is handed), `arrival_gaps`, and the lock-wait max/histogram as a fraction of the deadline. Counters are atomics written on the audio thread; the callback never prints, allocates or takes an extra lock. Measured baseline: 0 overruns, max lock wait 0.33 % of deadline (`DESIGN_AUDIO_THREAD_OWNERSHIP.md`) |
| `SUGAR_PROFILE=1` | per-system fixed-step timing to stderr once a second: `Script=median/max` per named system, `total=` measured independently in `SuGarApp` around the whole fixed step (systems + `Input::endFixedStep` + snapshot capture), and `residual=` total minus the summed systems. `path=` says `plain` or the access-verified path — **Debug numbers include the `ComponentAccessTracker`'s cost and are not comparable to Release**. Collection is always on (~0.16 ms/step, measured against a stubbed build); the editor Systems panel shows the same figures live. **The same knob prints two more lines**: `[profile-render]` (`drawList`/`resources`/`fenceWait`/`record`/`submit`/`frameOther` against an independently measured `frameTotal`; `fenceWait` is WAIT and is deliberately excluded from the work sum) and `[profile-loop]` (`input`/`fileWatch`/`moduleCheck`/`fixedStep`/`sleep` against an independently measured `loopTotal`, plus **`steps/frame`**). Read `steps/frame` before adding any two of these numbers together — see #12. `cameraTargets` on the loop line sits **inside** `frameTotal` and is printed for information only, never subtracted twice (`DESIGN_RENDER_CPU_TIMING.md` §7-§8). A fourth line, `[profile-drawlist]`, splits draw-list construction into `gather`/`items`/`skinning`/`sort`/`lights` against an independent `buildTotal`, with `skinnedItems`/`joints` as counts — divide them yourself, the line will not. **`skinning` is inflated by ~3-5 % by its own per-entity clock pair** (measured against a stubbed build, §10.3), which matters only on torture-scale scenes; at a real entity count it is ~0.03 ms. Also prints `subtreeSearches`/`nodeVisits`/`matrixHops` — work COUNTS inside skinning (~0.5 % overhead for ~150 000 increments, against 3-5 % for 3 212 clock reads), which say *why* rather than *how bad* and survive a change of machine |
| `SUGAR_RENDER_RES=<W>x<H>` | overrides the offscreen scene render target, independently of the window, so render cost can be measured at 4K on a smaller display. `createViewportResources()` logs the extent it actually allocated — trust that line, not the variable, since `requestedViewportExtent` is otherwise reassigned every frame from the ImGui panel size |
| `SUGAR_NOAUDIO=1` | skips opening the playback device entirely: no mixer thread, no device, every `play()` a no-op. The same state a machine with no sound card produces. Use for any measurement run that would otherwise play a game's music into whatever else the machine is doing |

Game-defined (they live in the game's behaviours, not the engine) — the combat arena:

| Knob | Effect |
| --- | --- |
| `SUGAR_ARENA_AUTOPLAY=1` | deterministic scripted play; no random rolls anywhere in the game |
| `SUGAR_ARENA_TORTURE=anim\|physics\|audio\|gpu` | adversarial pass; writes counters to `arena_debug.log` |
| `SUGAR_ARENA_CYCLES` / `_COUNT` / `_PERIOD` | torture shape |
| `SUGAR_ARENA_WAVE` / `_MAXENEMIES` | start at a given wave / raise the wave cap, for scaling runs |
| `SUGAR_ARENA_BOLTPROBE=<speed>` | fires a burst at a wall and reports hits vs expiries (the CCD measurement) |

## Driving the window from a script

The engine has no scripted-input mode, so a menu or a hot reload has to be triggered through
the OS. Two scripts, kept in the session scratchpad, do the whole job:

- **Screenshot** — `SetProcessDpiAwarenessContext(-4)` *before any window API*, maximise,
  `GetWindowRect` (not client rect), `PrintWindow(handle, dc, 2)`. Traps and reasoning are in
  the memory note; the short version is that both mistakes look exactly like "the app clips
  its own UI".
- **Keypress** — `SetForegroundWindow` then `keybd_event` with a **~250 ms hold**. `SendKeys`
  is too instantaneous for per-frame key sampling. Even so it is unreliable: roughly one press
  in three did not register across this session, so a run that depends on a keypress must
  *verify* the effect (a log line, a captured frame) rather than assume it.

Measurement by pixel is the honest way to check a renderer change: mean RGB of a fixed crop,
with the *variable swapped between two otherwise identical elements* so position and
background are controlled. That is how #44's bleed was both found (−53 RGB) and confirmed
fixed (39.8 → 86.4).
