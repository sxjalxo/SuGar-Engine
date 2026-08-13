# Build Pipeline — Architecture Design Record

> **Status:** **Phase 21 complete.** The sixth and final M3 platform item. Unlike 16–20
> it introduces **no new subsystem** — it orchestrates the headless gates Phases 19 and
> 20 already built. That it needed no new architectural concept is the point: the
> platform is composed, not extended.
> **Type:** Architecture record, kept short on purpose. The only decisions here are the
> two the earlier phases deferred to "the build pipeline"; everything else is a script.

---

## The pipeline

```
cmake --build (Release)  ──►  SUGAR_PACKAGE  ──►  runnable standalone
        1                          2                (build/package/)
```

`scripts/build_release.ps1` (and a `.sh` companion) is the whole orchestrator: build,
then run the freshly built exe with `SUGAR_PACKAGE=1`. No new engine code drives it —
the stages are the same gates a developer runs by hand, in order, with the exit codes
chained so CI can gate on the result.

Cooking and packaging are device-free (Phases 19–20), so the pipeline needs no GPU and
no window: it runs on a headless CI box exactly as on a workstation.

---

## Decision 1 — the build produces the binaries packaging ships

Phase 20 deferred one thing: a package contained assets + manifest + scenes but **no
executable**, because the packager had no build to copy from. The pipeline closes that:
`Packager::collectRuntimeBinaries()` returns the running executable and the DLLs beside
it — enumerated, not hard-coded, so a new engine DLL ships without editing packaging —
and excludes the hot-reload `*_live_*.dll` copies (transient, git-ignored, never
shippable). The exe-path lookup is the one piece of platform code, isolated in
`Packager` so the rest of packaging stays portable and headless.

The result is a directory that runs by double-click: `SuGarEngine.exe`, its DLLs,
`assets.manifest`, `assetcache/`, and the scenes — nothing else, and no source.

## Decision 2 — a package must verify before it is shippable

A build that produces a broken package silently is worse than one that fails loudly. So
packaging ends with an acceptance check, `Packager::verify`, that resolves the package
**the way the shipped exe will**: load the manifest, put `AssetCooker` in packaged mode,
and resolve every key to a readable artifact — with no source tree and no database. If a
single key fails to resolve, `SUGAR_PACKAGE` exits nonzero and the pipeline fails.

This is the same invariant the Phase 20 self-test pins by deleting the source tree, now
enforced on every build rather than only in the test. `verify` reuses the real runtime
resolution path (packaged-mode `AssetCooker`), so it cannot drift from what actually
happens at boot — there is no second "does this package work?" implementation.

---

## What Phase 21 did *not* add (Rule 18)

No installer, no code signing, no compression, no auto-versioning, no cross-compilation.
Each is a real shipping concern and none is needed to answer M3's question — *can a
developer build a typical indie game without extending the engine?* The pipeline turns
a project into a runnable, verified standalone; refinements wait on a real release.

---

## Why this is where M3 ends

The five platform phases each answered a hard ownership question and never had to revise
an earlier answer. Phase 21 asked none — it only wired the answers together, and they
fit. A platform whose final milestone is *orchestration* rather than *design* is a
platform that is done. Next is M4: build real games, and let their friction — not a
roadmap — drive what comes after.
