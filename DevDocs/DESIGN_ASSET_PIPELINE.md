# Asset Pipeline — Architecture Design Record

> **Status:** **Phase 19 complete (19A–19D).** 19A: `AssetPath` / `AssetHash` /
> `AssetMeta` / `AssetDatabase` — identity, sidecars, content-hash staleness. 19B: the
> `.sgc` container and `AssetCooker`; the runtime stopped parsing source formats;
> `SUGAR_COOK=1`. 19C: import settings applied at cook time, dependency edges (database
> owns, cooker discovers). 19D: the editor surface, and `AssetReimport` — one import
> path for the watcher and the Reimport button. The fourth M3 platform
> item, after Runtime UI (16), Animation (17) and Navigation (18), and the one the two
> remaining items depend on: packaging exports what cooking produced, and the build
> pipeline runs the cooker.
> **Type:** Architecture record — decided *before* code, for the same reason
> [DESIGN_RUNTIME_UI.md](DESIGN_RUNTIME_UI.md), [DESIGN_ANIMATION.md](DESIGN_ANIMATION.md)
> and [DESIGN_NAVIGATION.md](DESIGN_NAVIGATION.md) were. Here the expensive-to-change
> decision is **asset identity** — what a component's name-string actually names. Every
> scene, prefab, snapshot and save file on disk already contains that answer.
>
> **Not built, deliberately:** texture block compression (BC7/ASTC), mipmap generation,
> async / streaming loads, an asset dependency *graph* beyond one level, per-platform
> cook variants, and any content-addressed store shared across machines. Each waits on a
> real game asking for it (Rule 18).

---

## The governing invariant

> ## The source file is truth. Import settings are truth. **Everything else is cooked, and cooked is derived.**
>
> ```
> Cooked   = f(source bytes, import settings, cooker version)   ← pure, reproducible, deletable
> Runtime  = f(cooked)                                          ← never parses a source format
> ```

Delete the entire cook cache and the engine must produce byte-identical output on the
next run. That is the acceptance test for the whole phase, and it is the same shape as
`UI = f(ECS, input)`, `Pose = f(clip, playback state)`, `Route = f(navmesh, start, goal)`.

---

## Asset identity: paths stay, GUIDs are rejected

The reflex from Unity/Unreal is a GUID per asset plus a database mapping GUID → path,
so renames don't break references. Rejected, deliberately:

- Rule 21a already says a component stores a **stable identifier that can reconstitute
  the asset**. The normalized path key (`assets/models/foo.gltf#3`) does exactly that
  today, and `ModelImporter::ensureModelAssets` is the reconstitution half.
- A GUID is only stable if a database is authoritative for the mapping — which makes
  that database a piece of **history**, not a function of the present (Rule 21b). Lose
  it and references are unrecoverable. Paths recompute from a directory scan.
- Text scenes stay diffable and hand-editable. `"assets/models/hero.gltf#Idle"` in a
  git diff means something; `"a3f9…"` does not. Rule 1 — the developer reads this.
- Rename breakage is real but cheap: it is a find/replace over text scenes, and the
  editor can offer it. That cost is paid rarely; the database's cost is paid always.

**Decision: the normalized relative path (plus the existing `#sub` selector) is the
asset identity, everywhere, forever.** No GUIDs, no id database.

### The normalization algorithm is part of the identity

Changing it later is a **migration of every scene, prefab and save file on disk**, so it
is specified here and implemented exactly once (`AssetPath::normalize`, Core):

1. **Split at the first `#`.** Only the path part is normalized. The `#sub` selector is
   copied **verbatim** — it is a glTF mesh index or a clip/skin *name*, and glTF names
   are case-sensitive identity, not a path.
2. **Separators:** `\` → `/`; runs of `/` collapse to one.
3. **Dot segments** collapse textually: `.` dropped; `a/b/../c` → `a/c`. A leading `..`
   that cannot be collapsed makes the key **invalid** (it escapes the project).
4. **Anchor:** if the result contains `assets/`, everything before the *first* one is
   dropped, so an absolute path and a relative one give the same key. A path with no
   `assets/` segment is used as-is (relative to the project root).
5. **Case:** ASCII-lowercased. Windows and macOS filesystems are case-insensitive by
   default; a case-sensitive key would let `Hero.png` and `hero.png` be two assets on
   Linux and one everywhere else. Lowercasing is what `ResourceManager` already does,
   so existing scenes on disk already carry lowercased keys — this is the *status quo*
   written down, not a change. Consequence, accepted: **asset filenames are effectively
   case-insensitive**, and two files differing only in case are a conflict the database
   reports.
6. **Unicode:** none. Bytes pass through unchanged — no NFC/NFD normalization, so an
   asset whose name differs only by composition form is a different asset. Non-ASCII
   filenames are **not supported**; the database reports them rather than silently
   producing a key that resolves on one machine and not another. Revisit only when a
   real project needs it (Rule 18).
7. **Symlinks are not resolved.** The key is the path the developer typed, not the path
   it happens to land on; `weakly_canonical` would make identity depend on machine-local
   filesystem layout — the same historical-state objection as the GUID database.

---

## Ownership — three responsibilities, three owners (Rule 5, Rule 14)

| Owner | Owns | Does **not** own |
|---|---|---|
| `AssetDatabase` (Core, headless) | The catalog: source files, their `.meta` import settings, cook state and staleness | Any loaded resource, any GPU object |
| `AssetCooker` (Engine, headless) | Source format → cooked format, deterministically | When cooking happens, or caching policy |
| `ResourceManager` (Engine) | Loaded runtime instances, refcounts, GPU upload | Import settings, source parsing |

`AssetRegistry` (today: a flat path/name/extension scan feeding the editor browser) is
absorbed by `AssetDatabase`. The editor is a **consumer** of the catalog, exactly as it
is a consumer of navigation state — it may trigger a reimport, it never owns cook state.

Cooking is headless by construction: `ResourceManager::isInitialized()` already exists
so a run without Vulkan is a legitimate state rather than a caught exception. The cooker
tool and the self-tests use that path; no cook step may need a device.

**Headless is not the same as Core** (settled in 19B, and worth stating because the
first draft of this record put the cooker in Core). Cooking *reads source formats*, so it
needs tinygltf, stb_image and miniaudio — exactly the libraries Rule 15 keeps out of
Core, and its outputs are `Mesh`/`Texture`/`AudioClip`, whose headers include
`vulkan.h`. So `AssetCooker` lives in the Engine layer beside the importers, and needs
no device: **needing no GPU is the requirement, living in Core was only a guess at how
to get it.** Core keeps what has no format dependencies — identity, `.meta`, catalog,
hashing — which is what packaging and the build pipeline actually enumerate.

---

## Import settings live in `.meta` sidecars

`assets/models/hero.gltf` → `assets/models/hero.gltf.meta`, JSON, versioned:

```json
{ "version": 1, "type": "model", "scale": 1.0, "generateColliders": false }
```

- **Sidecar, not central manifest** — one file per asset means no merge conflicts on a
  shared file, and moving an asset moves its settings with it.
- **Committed to git.** They are *source*, not cache: two developers must cook the same
  bytes from the same checkout.
- **Missing `.meta` is legal** — defaults apply and the file is written on first
  import. Adding an asset must never require ceremony (Rule 1).
- Editing a `.meta` invalidates the cook exactly like editing the source does.

---

## Cook keys and staleness

A cooked artifact is stored at `build/assetcache/<hash>.sgc`, where

```
hash = H(normalized asset key, source file bytes, .meta bytes, cooker version)
```

Content hash, not mtime. Mtime says *a write happened*; the hash says *the bytes
differ*. Touching a file, switching git branches back and forth, or a clean checkout
must not recook. The existing `FileWatcher` mtime poll stays — but as a *cheap trigger
to go check the hash*, never as the answer.

`cooker version` is a single integer bumped whenever a cooked format changes. It is
what makes "delete the cache" never necessary.

**The hash algorithm is part of the cooker version.** Swapping FNV-1a for xxHash or
BLAKE3 changes every key, so it *must* bump the version — otherwise a stale entry keyed
by the old algorithm can collide with, or be mistaken for, a new one. Stated as a rule
so it is not re-litigated: *any change to what the hash is computed over, or how, bumps
the cooker version.* 19A ships 64-bit FNV-1a — cheap, dependency-free, and adequate for
change detection rather than adversarial integrity; the version field is exactly the
mechanism that makes upgrading it later a non-event.

**The cache is derived (Rule 21b): it is never serialized, never in a snapshot, and
deleting `build/assetcache/` is always safe.**

---

## Determinism obligations (Rule 10)

Cooking must be a pure function, which forbids more than it sounds like:

- No unordered-container iteration order in output; sort by a stable key before writing.
- No absolute paths, timestamps, machine names or thread ids in cooked output.
- No floating-point choices that depend on hardware; cooked geometry is copied and
  reordered, not re-derived.
- Parallel cooking is allowed, but only where each output is written by exactly one
  task — the *set* of outputs must not depend on scheduling.
- **No locale dependence.** Sort with byte-wise comparison, never `strcoll`/`std::locale`
  collation; case-folding is ASCII-only and explicit; number formatting uses the C
  locale. A machine set to `tr_TR` must cook the same bytes as one set to `en_US` —
  locale-sensitive comparison and Turkish dotless-i case folding are a classic source of
  "reproducible except on that one developer's machine" builds.

Test (Rule 9a — it must be shown to fail): cook twice into two directories and compare
bytes; introduce a deliberate unordered map into the mesh cooker and see the test fail.
**This is the defining test of the cooker, not one test among many** — a regression here
is an architecture violation, not a bug, so it runs in the self-test suite and gates CI.

---

## Robustness at the trust boundary (hardening pass, 2026-07-28)

Every format the pipeline touches is a place external bytes enter the engine, so each
parser must **fail cleanly on malformed input — never crash, never read out of bounds.**
This is not the same obligation as determinism, and it is easy to assume a parsing library
already discharges it. It does not: a library validates the *syntax* of its format, not the
*semantic consistency* between its internal structures. The 2026-07-28 sweep found three
gaps, all at this boundary and none in the deterministic core:

- **glTF (cook-time, third-party art).** tinygltf parses the JSON and loads buffers but
  does not check that an accessor's `bufferView`/`buffer` indices are in range or that its
  `byteOffset + count·stride` fits the backing buffer. The engine must. One overflow-safe
  routine, `validateAccessorSpan`, gates every accessor read (positions, normals, UVs,
  joints, weights, indices, animation samplers, inverse-bind matrices); on any failure the
  accessor is refused, not read. Index *values* are additionally checked `< vertexCount` —
  that check protects the **renderer** (a stray index is an out-of-range GPU fetch), not the
  loader. Sparse accessors remain unsupported — a feature gap, not a safety gap.
- **Cooked `.sgc` (packaged / cached).** The reader must trust *what is present* (the
  payload length), not *what the header claims*. Element counts and `width·height` are
  derived from — or validated against — the payload size before any allocation, so a
  tampered or truncated container cannot drive a multi-gigabyte `reserve()` (which throws
  and terminates) or overflow a size calc before GPU upload. Trusted in a normal ship, but
  mods, downloaded content and disk corruption make "fail cleanly" the requirement anyway.
- **scene.json.** Not part of the cook pipeline but the same discipline: its recursive
  parser is depth-guarded, because a nested-container bomb overflows the stack — and on
  Windows that is an *uncatchable* crash on the runtime path (startup + every hot reload).

These are pinned by the `MalformedInput` self-test in `SUGAR_VALIDATE` (a JSON bomb, an
OOB-accessor glTF, a bogus-count `.sgc` — each must be rejected), so malformed-input
handling is a permanent part of the regression contract, not a one-off audit.

---

## Dependencies: a graph, without moving ownership (19C)

One source asset references others — a model names its base-colour textures. That makes
the catalog a graph, and a graph is where asset pipelines usually leak ownership: the
cooker starts remembering what it built, and the "database" becomes a cache of the
cooker's opinions. The split that avoids it:

| | |
|---|---|
| `AssetDatabase` | **owns** dependency metadata — it stores the edges and answers `dependenciesOf` / `dependentsOf` |
| `AssetCooker` | **discovers** edges (only it can — they come out of parsing glTF) and **consumes** them when deciding what to rebuild |
| `ResourceManager` | unchanged. It loads cooked artifacts and never learns *why* one was rebuilt |

Discovery lives with the parser and storage lives with the catalog, which is why the
cooker *reports* edges to the database rather than keeping its own table.

**Edges are derived, not authored** (Rule 21b): they are recomputed from the present
source bytes at scan/cook time, so they are never serialized, never in a snapshot, and a
missing edge is a rescan away — the same argument that rejected a GUID database.

**The rule that keeps edges honest as the cooker grows:** *if a cooked artifact ever
embeds a dependency's content, that dependency's cook key must be folded into the
artifact's key.* Today none do — a cooked mesh references its texture by key, it does not
inline pixels — so edges are **reachability metadata**: what packaging must ship, and
what the editor shows. The moment an artifact inlines a dependency (an atlas, a
material blob), its key stops being a function of its own source alone, and that is the
day this rule is not optional.

## Import settings are applied at cook time, never at load time

19A carried settings; 19C applies them. Each importer claims the keys it understands:

| Type | Key | Effect |
|---|---|---|
| model | `scale` | multiplies vertex positions |
| texture | `flipY` | flips rows (the GL-vs-Vulkan origin question, answered per asset) |
| audio | `gain` | scales samples |

Applied in the cooker, so the setting is baked into the artifact and the runtime does no
per-load work. The cook key already hashes the `.meta` bytes, so changing a setting
renames the artifact and the change appears without anyone thinking about invalidation —
the property 19A's key formula was built for.

Unknown keys are ignored, not rejected: a project may carry settings a newer importer
will understand, and refusing to cook would make a `.meta` written by a teammate's build
break yours.

## The editor requests work; it never performs it (19D)

The editor edits **intent** — import settings, and a request to reimport. It never edits
bookkeeping: cook keys, artifact paths and edges are shown read-only, because they are
derived and the editor is not their owner.

`AssetReimport::reimport` is the single implementation, called by both the file watcher
and the editor. They differ in one flag: the watcher passes `force=false` (a touched
file whose bytes did not change costs nothing), the editor passes `force=true` (a
developer pressing Reimport means it — the cache was deleted, the cooker was fixed, a
tool rewrote the file keeping its mtime). An editor-only import path would be a second
answer to what importing means, the same failure the single identity function and the
single dependency-graph owner were built to avoid.

The editor's request is a string the engine picks up next frame, outside the render
frame and with the device idle — a reload destroys GPU resources, and performing it
mid-panel would be the editor doing engine work in the wrong place, not just the wrong
layer.

## Phasing

Each sub-phase ends with the engine working, tests green, and no half-migrated state.

- **19A — `AssetDatabase` + `.meta`.** Catalog, sidecar read/write with defaults,
  content-hash staleness, headless tests. `AssetRegistry` folds in; the editor browser
  reads the catalog. No cooked format yet — behavior unchanged.
- **19B — cooking.** Cooked formats for mesh, texture and audio; `AssetCooker`; the
  cache; `ResourceManager` loads cooked and no longer parses source formats at runtime.
  Determinism test. glTF/OBJ/stb parsing moves fully behind the cooker (which is where
  the "tinygltf objects never survive loading" requirement in
  [REQUIREMENTS_AND_SCOPE.md](../REQUIREMENTS_AND_SCOPE.md) has wanted it all along).
- **19C — importer maturity.** Import settings actually applied (model scale, clip
  splitting, texture color-space); reimport-on-change wired through the watcher; the
  one-level dependency edge model → textures so changing a texture reloads the texture
  and not the model.
- **19D — editor surface.** Asset Browser shows type, cook state and errors; an explicit
  Reimport; import settings editable in the inspector. Editor consumes, never owns.

---

## What this buys the two remaining M3 items

- **Packaging** becomes "copy the cook cache entries reachable from the shipped scenes,
  plus the executable" — a graph walk over the catalog, not a new subsystem.
- **Build pipeline** becomes "run the headless cooker, then build" — possible only
  because no cook step needs a GPU.
