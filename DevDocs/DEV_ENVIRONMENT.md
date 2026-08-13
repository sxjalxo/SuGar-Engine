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
