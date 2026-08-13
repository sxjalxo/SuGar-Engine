# Packaging — Architecture Design Record

> **Status:** **Phase 20 — design settled, code in progress.** The fifth M3 platform
> item, and the first that *consumes* the asset pipeline instead of extending it: a
> package is the cooked artifacts a set of scenes can reach, plus the manifest that lets
> the runtime find them without the source tree.
> **Type:** Architecture record — decided *before* code, like the four before it. The
> expensive-to-change decision here is **how a shipped runtime resolves an asset key to
> a cooked file when there is no source to hash.** Phase 19 answered "cooked = f(source)";
> packaging has to answer the same question with the source gone.
>
> **Not built, deliberately (Rule 18):** installers, code signing, compression of the
> package, patch/delta updates, cross-compiling for another OS, and cooked artifacts for
> animation clips and skins (still reconstituted from source — see the gap below). Each
> waits on a real shipped game asking for it.

---

## The problem packaging exists to solve

In the editor, the runtime resolves a resource key to a cooked file like this
(Phase 19B):

```
resourceKey ──► hash the SOURCE bytes + .meta + cooker version ──► artifact filename
```

`AssetCooker::artifactKey` needs the **source file** to compute the name. A shipped game
has no `assets/` source tree — that is the whole point of shipping cooked artifacts. So
the editor's resolution path cannot run, and the question packaging must answer is:

> **Given a resource key and no source, which cooked file is it?**

---

## The governing decision: a manifest, written at package time

```
Package = { cooked artifacts reachable from the shipped scenes }
        + assets.manifest : resourceKey ──► artifact hash
        + the scenes, the executable, its DLLs
```

The manifest is the one new artifact this phase introduces, and it exists for one
reason: **at cook time the source is present, so the key can be computed; at run time in
a package it is not, so the key must have been recorded.** The manifest is that
recording.

- **The runtime is told, not asked to derive.** When a manifest is present next to the
  executable, `AssetCooker` resolves `artifactKey(resourceKey)` by lookup, and
  `ensureCooked` returns the artifact path **without cooking** — a shipped game parses no
  glTF, decodes no PNG, and needs no source. `Runtime = f(cooked)` finally holds all the
  way to a double-clickable build.
- **No new identity.** The manifest maps the *same* resource keys scenes already store
  (Rule 21a) to the *same* artifact hashes the cooker already produces. It is a recorded
  answer to an existing function, not a second naming scheme — the GUID objection
  (Rule 21b: a database that becomes authoritative history) does not apply, because
  deleting the manifest and re-packaging from source reproduces it byte-for-byte.
- **Deterministic bytes.** Sorted by key, `\n` endings, cooker version in a header. Two
  packages of the same scenes from the same checkout have identical manifests.

**Dev vs packaged is decided by one fact: does a manifest sit next to the executable?**
No build flag, no mode switch — the presence of `assets.manifest` is the signal, and its
absence means "editor, cook on demand from source."

---

## Reachability: the package is a graph walk, not a copy of `assets/`

Shipping the whole `assets/` folder would ship source, uncooked, plus everything no
scene uses. Instead the packager walks from what the game actually loads:

```
scenes to ship ──► asset keys they reference ──► + dependency edges ──► cook ──► copy
```

1. **Roots.** The scene files named in the package spec (and any prefab a scene
   instantiates — a prefab is a scene fragment, so its keys count too).
2. **Referenced keys.** `SceneSerializer::collectAssetKeys` parses a scene and returns
   every asset-reference field (`mesh`, `albedo`, `clip`, `skinnedmesh`, `prefab`). The
   serializer is the one owner of the scene format, so it is the one place that knows
   which fields are asset references — the packager never parses scene JSON itself.
3. **Dependency closure.** For each key, add what the database says it references
   (Phase 19C edges: a model reaches its base-colour textures even when no scene names
   the texture directly). This is exactly the direction `dependenciesOf` was built for.
4. **Cook and copy.** Each key is cooked (or found already cooked) and its artifact is
   copied into `<out>/assetcache/`, its `key → hash` line added to the manifest.

**Derived navmesh keys are excluded.** A navmesh is baked from scene geometry and
reconstituted on load (`ensureSceneNavMeshes`), not backed by a source file, so it has
no artifact to ship — it rebuilds in the package exactly as it does in the editor.

---

## The honest gap: clips and skins still need source (deferred, Rule 18)

Animation clips and skins are **not** cooked artifacts (Phase 19 scoped them out). They
are reconstituted at load by `ModelImporter::ensureModelAssets` **parsing the source
model**. A package that ships an animated or skinned model therefore still needs that
model's source file, which is a real dent in `Runtime = f(cooked)`.

The decision, stated so it is not rediscovered as a bug:

- The packager **reports** every referenced key it cannot fully package as a cooked
  artifact (a `#clip` / `#skin` sub-key), rather than silently dropping it (Rule 13).
- As an interim it copies the **source model** for such keys into the package, so
  animation actually works in a shipped build today. This is flagged in the report and
  in the manifest as source-backed, not a silent inconsistency.
- The clean end-state — cooked clip/skin artifacts, no source in any package — is a
  Phase 19 extension that waits on a dogfooded game exercising it (Rule 18). Building it
  now would be speculation; the report makes the day it is needed obvious.

---

## Ownership (Rule 5, Rule 14)

| Owner | Packaging role |
|---|---|
| `SceneSerializer` | Lists the asset keys a scene references (`collectAssetKeys`). It already owns the format; it does not gain any packaging logic. |
| `AssetDatabase` | Answers reachability (`dependenciesOf`) and locates source files. Owns no package. |
| `AssetCooker` | Cooks reachable keys; in a package, resolves keys through the manifest instead of source. |
| `AssetManifest` | The `key → hash` map. Core, headless, no format dependencies — a text file, the thing packaging and the build pipeline enumerate. |
| `Packager` | Orchestrates the walk, the copies, and the manifest write. Engine layer (it drives the cooker). Owns nothing after it returns. |
| `ResourceManager` | **Unchanged.** It still asks the cooker for an artifact and reads it; it never learns whether the answer came from source or a manifest. |

The runtime not knowing dev-from-packaged is the same discipline the whole pipeline has
kept: the consumer trusts the cooked artifact, and everything upstream is free to change
how that artifact was produced or found.

---

## Headless, like the cooker

Packaging needs no Vulkan device: it cooks (already device-free) and copies files.
`SUGAR_PACKAGE=1` runs it and exits, mirroring `SUGAR_COOK=1`, so the build pipeline
(the next and final M3 item) is `cook → package → done` with no editor and no GPU.
