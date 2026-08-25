# Security Policy

## Reporting a vulnerability

**Do not open a public issue for a security problem.**

Report privately, either way:

1. **GitHub Security Advisories** — preferred. Go to the repository's
   **Security → Report a vulnerability** tab. This creates a private advisory only you
   and the maintainer can see.
2. **Email** — <sujalgarje@gmail.com>, subject prefixed `[SuGar security]`.

Please include:

* What the issue is, and which component (`src/…` path is ideal).
* A reproduction — a malformed `scene.json`, a crafted `.gltf`, a cooked `.sgc`, a
  package manifest, or a short driver.
* Which build config and commit you reproduced it on.
* The `crashes/crash_*.txt` file if the engine crashed. It carries the engine version,
  git commit, OS/CPU/GPU, the loaded scene and package, the exception code and a
  symbolized stack — it is usually enough on its own.

### What to expect

SuGar Engine is maintained by one person as an open-source project, with no commercial
backing and no bug bounty. Response is best-effort:

| Stage | Target |
| --- | --- |
| Acknowledgement | within 7 days |
| Initial assessment (in scope / severity / plan) | within 14 days |
| Fix or documented mitigation, for an in-scope issue | within 90 days |

If you have not heard back in 14 days, please ping the thread — mail gets lost.

Coordinated disclosure is appreciated. Credit is given in the release notes and the
advisory unless you would rather not be named.

## Supported versions

The engine is pre-1.0 and moves fast. **Only `main` is supported.** Fixes land on `main`;
there are no maintained release branches or backports. The current version string is in
`CMakeLists.txt` (`SUGAR_VERSION`).

---

## Threat model

Knowing what SuGar *is* determines what counts as a vulnerability.

SuGar is a **local, single-user desktop game engine and editor**. It has:

* no network stack, no multiplayer, no server component, no telemetry;
* no sandbox, and no privilege boundary between the engine and the content it loads;
* no authentication, no user accounts, no secrets at rest.

The engine's real attack surface is therefore its **deserializers** — the places where
bytes someone else produced become engine state.

### The trust boundary

| Input | Parsed by | Trust |
| --- | --- | --- |
| `scene.json` — scenes and prefabs | the hand-rolled JSON reader / `Serializer` | **untrusted** |
| `.gltf` / `.glb` models | tinygltf + the engine's importer | **untrusted** |
| Cooked `.sgc` artifacts | the asset cooker/loader | **untrusted** |
| Package manifests, `.meta` import sidecars | asset database / packaged runtime | **untrusted** |
| Images (`stb_image`), fonts (FreeType), audio (miniaudio) | vendored libraries | **untrusted** |
| `SaveData` store (`save.dat`) | `core/SaveData` | **untrusted** |
| `Game.dll` — the game module | the OS loader | **trusted, by design** — see below |

A hardening pass on 2026-07-28 added a permanent regression gate for this boundary: the
`MalformedInput` self-test feeds hostile bytes to the three deserializers that take
external input — a deeply nested JSON bomb that would overflow the parser stack, a glTF
whose accessor runs past its buffer, and a cooked `.sgc` claiming an absurd element count
that would abuse `reserve` or overflow a size calculation. Each must be **rejected
cleanly**. It runs as part of `SUGAR_VALIDATE`.

### In scope

* Memory-safety bugs reachable from any untrusted input above: out-of-bounds read or
  write, use-after-free, double free, type confusion, uninitialized reads.
* Integer overflow or unchecked size/count fields leading to an over-allocation, a wild
  `reserve`, or an undersized buffer.
* Unbounded recursion or unbounded allocation from a small crafted file (parser stack
  overflow, allocation bombs).
* Path traversal — an asset key, manifest entry or import path that escapes the project
  or package root and reads or writes outside it.
* Any crafted asset, scene or package that achieves **code execution** when merely opened
  or played.
* A packaged standalone that can be made to load code or data from outside its own
  package directory.

### Out of scope

These are known, deliberate properties of a local development tool, not vulnerabilities:

* **Loading an untrusted `Game.dll` executes arbitrary code.** The game module is native
  code loaded by the OS loader, by design (this is how hot reload works). Running someone
  else's game module is exactly as trusting as running their `.exe`. Never point
  `SUGAR_GAME` at a directory you did not build or do not trust.
* **The editor has full local filesystem access** with the privileges of the user running
  it. It is a developer tool, not a sandbox.
* **Opening an untrusted project directory** in the editor is not a supported safe
  operation, because of the point above.
* Vulkan validation-layer warnings, driver crashes, GPU hangs, and denial of service
  caused by legitimately expensive content (a 10-million-triangle mesh is slow, not
  insecure).
* Precision loss at extreme world coordinates. This is IEEE-754 behaviour and is
  documented in `README.md`; the broadphase is hardened against extreme and NaN
  coordinates so it will not crash or corrupt, but it cannot manufacture precision.
* Anything requiring an attacker to already have write access to your source tree, build
  directory, or installed engine.
* Missing compiler hardening flags, absent ASLR/CFG, or similar — worth reporting as a
  normal issue, and welcome, but handled publicly.

### Vendored dependencies

All third-party libraries are vendored under `external/` (see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)). If you find a vulnerability in one
of them, **report it upstream first** — that is where the fix belongs and where other
projects will get it. Then open an issue here, or tell us privately if the vendored copy
is exploitable through SuGar specifically, so the copy can be updated.

---

## Hardening notes for people shipping a game

If you ship a SuGar standalone to players, the relevant facts:

* A packaged build resolves asset keys through its manifest and reads only cooked
  artifacts — the runtime does not parse a source asset format.
* `SaveData` is written atomically (`save.dat.tmp` → rename) and has size caps, but it is
  a plain local file. Treat it as player-modifiable, because it is. Do not put anything
  in it that must be trusted.
* `crashes/crash_*.txt` includes the machine's OS, CPU and GPU, the loaded scene, and a
  stack trace. It is written locally and never transmitted, but if you collect crash
  reports from players, that is what you are collecting.
