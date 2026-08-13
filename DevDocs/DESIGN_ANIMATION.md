# Animation — Architecture Design Record

> **Status:** **Phase 17 complete.** Model layer (17A), glTF clip import (17B), skin
> model + joint matrices (17C.1), GPU skinning (17C.2), and blend trees / state
> machines (17D) all implemented.
> Animation is the second M3 platform item, after Runtime UI.
> **Type:** Architecture record — decided *before* code, for the same reason
> [DESIGN_RUNTIME_UI.md](DESIGN_RUNTIME_UI.md) was: the authoritative/derived split is
> the decision every future line of animation code has to optimize around, and it is
> the one that is expensive to change later.
>
> **Implemented so far (17A):** `AnimationClip` + track/keyframe data and hand-rolled
> sampling (Core), `AnimationClipRegistry` (name → clip), `AnimationPlayerComponent`
> (authoritative playback state in ECS), `AnimationSystem` (advances time on the fixed
> step and applies sampled poses to `TransformComponent`), serializer round-trip, and
> the `Animation` self-test. **Not yet built:** glTF clip import, skeletons/skinning,
> blend trees, state machines — everything below about those describes the pending half.

---

## The governing invariant

> ## Playback state is the **model**. The pose is a **view**.
>
> ```
> Pose = f(clip data, playback state)
> ```

A pose is never stored. It is **recomputed** from immutable clip data plus a small
amount of authoritative playback state that lives in ECS.

This is the animation-shaped instance of the same compass Runtime UI uses
(`UI = f(ECS, input)`). Whenever anyone asks *"can I cache this on the animator?"* the
answer is mechanical:

> Does it violate `Pose = f(clip data, playback state)`? If yes — **don't**, unless it
> is a pure cache that can be thrown away and rebuilt at any instant.

**Why this is the whole point.** [RULES.md](../RULES.md) Rule 21 does not pick
animation as its example by accident — it picks it because animation is the classic
place engines hide authoritative state:

```
Animator { float currentTime; }   // hidden authoritative state
        ↓ snapshot → restore
animation jumps                    // regression, hard to reason about later
```

SuGar already guarantees snapshots, time travel, hot reload, stable IDs, deterministic
replay. If playback state lives in ECS, those guarantees extend to animation **for
free** — restore the components and the next fixed step re-derives the pose. There is
no animation-specific time-travel code, and there must never need to be.

---

## Ownership

Ownership is separate from — and prior to — state classification.

```
Gameplay   ──owns──►   "is jumping" · "is hurt" · move speed · facing
                              │
Animation  ──reads──►   picks/blends clips to *depict* them
```

**The animation system never owns a gameplay concept. It depicts one.** "Is the player
attacking?" is gameplay state that the animator *reads*; it is not "animation state
that the state machine owns." The moment combat code asks the animator which state it
is in, the model has inverted and gameplay now depends on a view.

The one deliberate exception is **root motion**: when a clip drives locomotion, the
animation system writes `TransformComponent` — but it writes it as an ordinary
authoritative component that everything else already reads. It does not accumulate a
private "distance travelled" beside it.

---

## The decision test

Same test as Runtime UI, because it is the same underlying question:

> **Restore a snapshot. Does this piece of state, if missing, make the result look
> *wrong*, or just cosmetically *mid-flight*?**
> - Wrong / confusing → **authoritative** (lives in ECS / is serialized).
> - Only cosmetically in-flight → **derived** (rebuild it; never serialize).

For animation there is a sharper, gameplay-flavored version worth applying to every
candidate:

> **Could two runs with identical inputs disagree about this value?** If yes, it is
> authoritative and must be in ECS — otherwise replay diverges and the engine's
> determinism guarantee is a lie.

---

## Classification

| State | Verdict | Home |
|---|---|---|
| Current playback time | **Authoritative** — Rule 21's named anti-pattern | ECS (`AnimationPlayerComponent::time`) |
| Which clip is playing | **Authoritative** | ECS (clip name) |
| Playback speed | **Authoritative** — gameplay changes it (haste, slow-mo) | ECS |
| Playing / paused | **Authoritative** | ECS |
| Loop vs. clamp | **Authoritative** — authored, and changes end behavior | ECS |
| State-machine active state | **Authoritative** | ECS (`AnimationStateComponent::currentState`) |
| State playback **phase** (normalized, not seconds) | **Authoritative** | ECS (`statePhase` / `targetPhase`) |
| **Transition elapsed time** (mid-blend) | **Authoritative** — see gray areas | ECS (`transitionElapsed`) |
| The graph itself (states, transitions, blend trees) | **Not state at all** — immutable asset | `AnimationGraphRegistry` |
| A sampled `Pose`, and a blended one | **Derived** | recomputed each step, never stored |
| Blend **parameters** (speed, direction, health) | **Authoritative** — but they are *gameplay* state | ECS (gameplay components, not animation's) |
| Blend **weights** | **Derived** — a pure function of the parameters | recomputed |
| Sampled local pose (TRS per target) | **Derived** | recomputed each step |
| Joint / skinning matrices | **Derived** | recomputed, uploaded to GPU |
| Clip keyframe data | **Not state at all** — immutable asset | `AnimationClipRegistry` |
| Skeleton / inverse bind matrices | **Not state at all** — immutable asset | skeleton asset (17C) |
| Track → entity binding | **Derived** — a resolvable cache | rebuilt on demand |
| Joint / bone transforms | **Authoritative** — but they are just entity transforms | ECS (`TransformComponent`) — the ordinary one |
| Which entities are a skin's joints, in order | **Not state** — immutable asset | `SkinRegistry` (names, joint-index order) |
| Inverse bind matrices | **Not state** — immutable asset, not derivable from the pose | `SkinRegistry` |
| Which animation *events* already fired this cycle | **Authoritative** — see gray areas | ECS (17D) |
| Root motion **result** (where the character now is) | **Authoritative** | ECS (`TransformComponent`) — the ordinary one |

**Nothing derived is ever stored in ECS.** Derived state lives in the animation system
and is reconstructible from the model at any time.

---

## Gray areas — where this document earns its keep

These are the ones that surface as bugs two years later if not decided now.

1. **Transition progress is authoritative here, even though the *identical-looking*
   UI tween is derived.** [DESIGN_RUNTIME_UI.md](DESIGN_RUNTIME_UI.md) classifies
   transition progress as derived and says to snap to target on restore. **Animation
   is the opposite, and the contrast is the point.** A UI tween can snap with no
   consequence — nothing reads it but the eye. An animation transition mid-blend
   *determines the character's actual pose*, which drives root motion, hitboxes, IK
   targets, and what the player sees at frame N. Scrub to frame N twice and you must
   get the same pose. So the elapsed transition time is authoritative and belongs in
   ECS.

   The general rule this exposes, worth stating once: **a tween is derived when it is
   only looked at, and authoritative when something else reads it.** Same mechanism,
   different verdict, decided by consumers rather than by shape.

2. **Time is authoritative; the *pose* never is.** Storing the pose "for performance"
   is the tempting version of the Rule 21 bug — it makes the snapshot bigger, adds a
   second source of truth that can disagree with `time`, and buys nothing (sampling is
   cheap and the pose is needed every frame anyway). If sampling ever *does* show up in
   a profile, cache it **outside** ECS keyed by `(clip, time)` and let it be thrown
   away freely. Measure first (Rule 18).

3. **Blend parameters vs. blend weights.** The parameter ("speed = 3.2 m/s") is
   authoritative — but it is *gameplay's* state, living in a gameplay component, not
   something the animator owns a private copy of. The weights derived from it
   (`walk 0.4 / run 0.6`) are recomputed and never stored. If the animator caches the
   parameter, gameplay and animation can disagree after a restore; that is a Rule 21
   bug wearing a performance costume.

4. **Phase, not seconds** (found in 17D). A blend tree mixes clips of different
   lengths — a walk cycle is slower than a run. Advance them by wall-clock seconds and
   the feet slide, because each clip reaches its foot-plant at a different moment;
   advance a single normalized **phase** and sample each clip at `phase * duration`
   and the contacts stay aligned. Seconds would also make the time base shift under
   the blend parameter, since the state's effective duration changes with it. Hence
   `AnimationStateComponent::statePhase` — named `phase` precisely so it cannot be
   confused with `AnimationPlayerComponent::time`, which *is* seconds because a lone
   clip has nothing to stay in sync with.

5. **Animation events must not re-fire.** A footstep event at t=0.4s fires once per
   loop. That "already fired" bookkeeping is **authoritative** — it is not derivable
   from `time` alone (scrubbing backward and replaying must fire it again, but a paused
   frame must not fire it repeatedly). Model it explicitly in ECS (17D) rather than
   letting the system hold a private `lastFiredTime`, which is Rule 21's anti-pattern
   with a different field name.

6. **Clip data is an asset, not state.** Keyframes never change at runtime, so they are
   not snapshotted — the component references a clip **by name** and the registry
   resolves it. This is deliberately the `BehaviorRegistry` pattern (see the Phase 6
   decision): name-based indirection means a snapshot holds a string, hot reload can
   swap the clip table underneath a running animation, and nothing dangles.

   **The corollary that bit us (17C.2): a name is only as good as the thing that can
   re-resolve it.** Clips and skins were registered as a side effect of *import*, so a
   scene **loaded from disk** — which never runs the importer — kept its components and
   resolved them to nothing: animation silently dead, skinned meshes frozen in bind
   pose. The component was right; the missing half was a way to rebuild the table from
   the name. Hence `ModelImporter::ensureModelAssets`, and the general rule:

   > If authoritative state references an asset by name, *something* must be able to
   > reconstitute that asset from the name alone — on scene load, not only on the path
   > that happened to create it.

   The failure mode is the tell: not a crash, not a dangling pointer, but a component
   that looks perfectly correct in the inspector and does nothing.

   This is not an animation lesson — it applies to every asset-backed component
   (materials, audio clips, fonts, meshes, behaviors), so it lives in
   [RULES.md](../RULES.md) as **Rule 21a**. It is recorded here because animation is
   where it was found.

---

## Data flow

```
Fixed step ──► advance time ──► sample clip ──► write Transform ──► Renderer
                    │                │
             authoritative       derived
                  (ECS)        (recomputed)
```

With skinning (17C) the tail grows, and every step past the Transform is derived:

```
AnimationSystem ──► TransformComponent ──► computeJointMatrices ──► DrawList
                      (authoritative)          (derived)          (per-frame)
                                                                       │
                                              Renderer uploads ────────┘
                                                     │
                                         Vertex shader skins vertices
```

The renderer owns **GPU buffers, descriptor updates, shader bindings**. It owns
**none** of: animation time, joint transforms, the skeleton, playback state. Poses
reach it as plain matrices on the draw list, so it never reaches into ECS for one —
which is what keeps GPU skinning an *optimization of how poses are consumed* rather
than a second animation system.

Which is, again, the **same shape as the rest of the engine**:

```
Input ──► Behavior  ──► ECS ──► Renderer     (gameplay)
Input ──► Intent    ──► ECS ──► RmlUi        (runtime UI)
  dt  ──► Animation ──► ECS ──► Renderer     (animation)
```

One mental model for the whole engine: **disposable logic, authoritative state in
components, derived views recomputed.** Animation gets to be a third instance of a
pattern the engine already proved twice rather than a new architecture.

---

## Determinism: the fixed step is the only clock

Animation advances **only** on the 60 Hz fixed step, by `dt * speed` — never by render
delta, never by wall clock. Rendering interpolation (if it is ever added) is a view
concern and must not write back into `time`.

Two consequences that are easy to get wrong:

- **Loop wrap must be modular, not subtractive.** `time -= duration` silently breaks
  when `dt * speed` overshoots a short clip (a 0.1 s clip at speed 100), leaving `time`
  out of range for a frame or looping only once per step. Wrap with a proper modulo so
  any `speed` behaves.
- **Advance, then sample, in the same step.** Sampling a pose from a `time` that a
  later system in the same step will change means the rendered pose is one step stale
  in a way that depends on system order — exactly the class of thing the scheduler's
  declared access sets exist to make visible.

The system declares `reads: Animation`, `writes: Animation | Transform | Hierarchy`
and runs **before** physics, so a clip-driven transform is a physics input in the same
step rather than a step behind.

---

## Layering (Rule 15)

Animation splits along the existing `Editor → Engine → Core` line, and the split falls
out naturally because **playback is pure math over plain data**:

| Piece | Layer | Why |
|---|---|---|
| Clip / track / keyframe data | **Core** | plain structs, no GPU |
| Sampling, interpolation, blending | **Core** | pure math — and therefore headless-testable |
| `AnimationPlayerComponent` | **Core** | it is ECS state |
| `AnimationSystem` (advance + apply) | **Core** | needs only the registry + clip table |
| `AnimationClipRegistry` | **Core** | table lives in a `.cpp`, one instance across exe/DLLs |
| glTF clip + skin import | **Engine** | tinygltf is engine-side, parse-only (17B/17C.1) |
| Skin bind data + joint matrices | **Core** | pure math over the registry (17C.1) |
| Skinned vertex buffers, shaders, draw | **Engine** | Vulkan (17C.2) |

Putting sampling in Core is not a technicality — it is what makes the whole subsystem
testable without a GPU (Rule 9), and it is why 17A can ship a *correct, deterministic,
verified* animation player before a single skinned vertex exists.

**External libraries may import animation data. They never own playback**
([REQUIREMENTS_AND_SCOPE.md](../REQUIREMENTS_AND_SCOPE.md)). tinygltf parses
keyframes; the moment they are parsed they become SuGar types and tinygltf is gone.

---

## Component sketch (authoritative only)

```
Animated entity
└── AnimationPlayerComponent {
        clip     : string   // name → AnimationClipRegistry (never a raw pointer)
        time     : float    // seconds, authoritative — Rule 21's example
        speed    : float    // gameplay-mutable multiplier
        playing  : bool
        loop     : bool
    }
```

Deliberately **not** in the component: clip duration (an asset property), the sampled
pose, the resolved track→entity bindings, blend weights. Every one of those is
derivable, and every one of them, if stored, becomes a second source of truth that can
disagree with the first after a restore.

---

## Track binding: by name

A clip's tracks name their targets by **entity name**, resolved against the descendants
of the entity carrying the player. glTF animations target node indices; the importer
(17B) turns those indices into node names at import time, so nothing downstream depends
on glTF's numbering.

Chosen over the alternatives because it is the engine's existing idiom (behaviors,
screens, focus elements, clips are all name-keyed) and it **serializes to a string** —
so a snapshot never holds an index into a table that hot reload might reorder.

The honest cost, stated rather than discovered later: **duplicate names under one root
are ambiguous**, and the first match wins. If that becomes a real problem, the fix is a
stable per-node id assigned at import, not a runtime pointer cache.

---

## Behavior under the engine's guarantees

- **Snapshot restore / scrub** — playback state comes back through the ordinary
  snapshot path; the next fixed step re-samples and the pose is correct. Zero
  animation-specific restore code.
- **Time travel** — scrubbing to frame N restores `time`, and `Pose = f(clip, time)`
  gives back exactly the pose of frame N. Deterministic by construction, not by effort.
- **Code hot reload** — animation-driving behaviors reload; playback state in
  components survives (the Phase 6 / Phase 12 pattern).
- **Clip asset hot reload** — swap the clip in the registry; `time` keeps running
  against the new data. Live animation iteration — on-brand for the wedge (Rule 1).

---

## Open questions (to resolve at implementation, not before)

1. ~~**Skeleton representation** (17C) — flat joint array + parent indices vs. reusing
   the ECS hierarchy for joints.~~ **Resolved in 17C.1: the ECS hierarchy is the
   skeleton.** It never came to a measurement, because it stopped being a performance
   question and became an ownership one. Joints are *already* entities (imported in
   17B) and the `AnimationSystem` *already* poses them by writing
   `TransformComponent`. A parallel joint array would be a **second representation of
   the same state**, able to disagree with the transforms after a snapshot restore —
   the second owner Rule 21 forbids, bought with speed. So `Skin` stores only what
   ECS cannot know (joint names in joint-index order, inverse bind matrices), and
   joint matrices are derived. If joint lookup ever shows up in a profile, the fix is
   a *derived* cache outside ECS, not a second home for the pose.
2. ~~**Blend tree representation** (17D) — a data asset vs. a graph built in code.~~
   **Resolved in 17D: a data asset** (`AnimationGraph` in `AnimationGraphRegistry`,
   name-keyed like clips and skins). Active state, phase, and transition progress are
   authoritative ECS state; blend weights and poses are derived, as predicted. One
   thing the record did *not* predict: the state's playback position is stored as
   **normalized phase, not seconds**, because a blend tree mixes clips of different
   lengths and blending them by wall-clock time slides the feet.
3. **Root motion extraction** — which track is "root", and opt-in per clip. Does not
   affect the state model above.
4. **CUBICSPLINE interpolation** — glTF supports it; 17A implements STEP and LINEAR.
   Add when a real asset needs it, not before.

---

## Rule linkage

This record is the animation-specific application of **[RULES.md](../RULES.md) Rule 21**
(runtime systems must not own hidden authoritative state) — the rule whose worked
example *is* an animator. The one-line rule that belongs alongside it:

> *Clips are immutable assets and poses are derived. Authoritative playback state —
> time, clip, speed, playing, loop, active state, transition progress — lives in ECS;
> sampled poses, blend weights, joint matrices, and binding caches are derived and
> reconstructible from it. Therefore `Pose = f(clip data, playback state)`, and snapshot
> restore / time travel / hot reload restore animation for free.*
