# Getting help

SuGar Engine is maintained by one person. Help is best-effort and free — please pick the
channel that matches what you need.

## Where to go

| You want to… | Go here |
| --- | --- |
| Understand what the engine does and where it is | [`README.md`](README.md), [`ROADMAP.md`](ROADMAP.md) |
| Understand *why* it is built this way | [`DevDocs/RULES.md`](DevDocs/RULES.md), [`DevDocs/REQUIREMENTS_AND_SCOPE.md`](DevDocs/REQUIREMENTS_AND_SCOPE.md) |
| Build it, or fix a build that fails | [`CONTRIBUTING.md` → Build](CONTRIBUTING.md#build) — read the toolchain traps first |
| Ask a question, or show what you built | GitHub **Discussions** |
| Report something broken | GitHub **Issues** → Bug report |
| Ask for a capability | GitHub **Issues** → Capability request |
| Report a vulnerability | **Not** an issue — see [`SECURITY.md`](SECURITY.md) |
| Report Code of Conduct problems | <sujalgarje@gmail.com> |

## Before you open an issue

Three checks resolve most reports, in order of how often they are the answer:

1. **Try the CMake configure twice.** A fresh configure sometimes fails resolving the
   vendored FreeType dependency; running it again immediately succeeds. Known issue.
2. **Rebuild the game module.** If the engine crashes at startup inside
   `BehaviorRegistry::registerBehavior` or `BehaviorRegistry::has`, the `Game.dll` and
   `SuGarCore.dll` disagree — a stale DLL or a config mismatch, not an engine bug. Delete
   `Game.dll` / `Game_live_*.dll` and rebuild the game for the config you are running.
3. **Read `crashes/crash_*.txt`.** Every unhandled exception writes one, with the version,
   git commit, machine, loaded scene, exception code and a symbolized stack. It usually
   names the frame outright.

All three, and the rest, are in
[`DevDocs/DEV_ENVIRONMENT.md`](DevDocs/DEV_ENVIRONMENT.md) — summarised in
[`CONTRIBUTING.md` → Toolchain traps](CONTRIBUTING.md#toolchain-traps-that-cost-real-time).

Then run the gate — it is headless and needs no GPU:

```powershell
$env:SUGAR_VALIDATE = "1"; build\Debug\SuGarEngine.exe; $env:SUGAR_VALIDATE = ""
```

If it exits nonzero, that output is the single most useful thing you can put in an issue.

## What is *not* supported

* **Platforms other than Windows / MSVC / Vulkan.** Ports are wanted eventually but do not
  exist today, and there is no help available for making one build elsewhere.
* **Anything older than `main`.** The engine is pre-1.0 and there are no maintained
  release branches.
* **Your game's gameplay code.** Engine bugs, yes. Debugging a behaviour in your own
  `Game.dll` is out of scope — though if the engine gave you a bad or confusing error
  along the way, *that* is a real report, and a welcome one. Rule 1: better error messages
  are a feature.

## Response times

There is no SLA. Realistically: security reports get acknowledged within a week, issues
within a couple of weeks, and pull requests when the maintainer has a block of time to
review them properly. Pinging a thread that has gone quiet is fine and appreciated.
