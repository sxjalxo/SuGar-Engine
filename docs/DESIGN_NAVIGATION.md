# Navigation — Architecture Design Record

> **Status:** **Phase 18 complete (18A–18D).** 18A: navmesh asset, deterministic A* +
> funnel, `NavAgentComponent`, `NavigationSystem`, serializer round-trip. 18B: baking
> (`NavMeshBuilder` in Core, `NavMeshBaker` in Engine), `NavMeshSourceComponent`, and the
> Rule 21a post-load reconstitution hook. 18C: editor — Navigation panel with bake
> statistics and an explicit Rebake, viewport overlay, inspector sections. 18D:
> agent-radius erosion (bake time), local avoidance (steering time), and the shared
> `ViewportOverlay` near-plane clipping extracted for every future editor gizmo.
> Navigation is the third M3 platform item, after Runtime UI (16) and Animation (17).
> **Type:** Architecture record — decided *before* code, for the same reason
> [DESIGN_RUNTIME_UI.md](DESIGN_RUNTIME_UI.md) and [DESIGN_ANIMATION.md](DESIGN_ANIMATION.md)
> were: the authoritative/derived split is the decision every future line of navigation
> code has to optimize around, and it is the one that is expensive to change later.
>
> **Not built, deliberately:** off-mesh links (jumps/ladders), crowd simulation and
> agent-to-agent avoidance, hierarchical search for large worlds, true polygon-offset
> erosion, and automatic navmesh invalidation when scene geometry changes. Each is
> waiting on a real game asking for it (Rule 18), not on a schedule.

---

## The governing invariant

> ## The navmesh is an asset. The route is a function. **Following a route is state.**
>
> ```
> Route     = f(navmesh, start, goal)      ← pure, deterministic, replannable
> Following = authoritative state          ← history-dependent, lives in ECS
> ```

This is the navigation-shaped instance of the compass the engine already applies twice
(`UI = f(ECS, input)`, `Pose = f(clip data, playback state)`). The navmesh is immutable
data in a registry; planning is pure math over it; and the small amount of state that
records *what an agent decided and how far along it is* lives in ECS.

---

## Why the path is **not** a cache — the decision this record exists for

The tempting classification is: *"a path is derived — it's just `f(navmesh, position,
destination)`, so recompute it after a restore and don't serialize it."* That reasoning
is wrong, and it is wrong in a way that is worth writing down, because it looks exactly
like the reasoning that is *right* for a pose.

Apply the engine's own determinism test:

> **Could two runs with identical inputs disagree about this value?**

Yes. Consider an agent at a corridor fork, equidistant between two routes:

```
run A:  plan at t=0 from P0 ──► took the left corridor ──► now halfway down it
run B:  restore at t=5, replan from the same position ──► may take the right corridor
```

Both routes are legal, both are optimal, and the planner is deterministic — yet the two
runs disagree, because the stored path is a function of **where the agent was when it
planned**, not of where it is now. A pose is a function of the *present* (`clip`,
`time`). A path is a function of the *past*.

That gives the general rule, which is this record's contribution:

> **A cache is derived only if it is a function of the current state. A value computed
> once from a past state is a function of history — and history is authoritative.**

The same rule retroactively explains the animation transition call in
[DESIGN_ANIMATION.md](DESIGN_ANIMATION.md): `transitionElapsed` is authoritative because
it records *when the transition started*, which the present cannot reconstruct. Two
subsystems, one underlying reason. The shape to look for is not "is this expensive to
recompute" — it is "does recomputing it need information that no longer exists."

The failure mode if you get this wrong is the nastiest kind: everything looks correct.
Agents move, paths are valid, nothing crashes. Only a scrub-and-compare shows two runs
diverging — which is precisely the guarantee time travel exists to provide.

---

## Ownership

Ownership is separate from — and prior to — state classification.

```
Gameplay   ──owns──►   "go to the door" · "flee" · move speed
                              │
Navigation ──reads──►   finds a legal route and walks it
```

**Navigation never owns a gameplay decision. It executes one.** *Where* an agent wants
to go is gameplay state that navigation reads; it is not "navigation state the agent
system owns." The moment combat code asks the navigation system what an agent is doing,
the model has inverted.

The one thing navigation does write is `TransformComponent` — and it writes it as the
ordinary authoritative component everything else already reads, exactly as animation
does for root motion. It does not accumulate a private "distance travelled" beside it.

---

## The decision test

Same test as the other two records, because it is the same underlying question:

> **Restore a snapshot. Does this piece of state, if missing, make the result look
> *wrong*, or just cosmetically *mid-flight*?**
> - Wrong / divergent → **authoritative** (lives in ECS / is serialized).
> - Only cosmetically in-flight → **derived** (rebuild it; never serialize).

---

## Classification

| State | Verdict | Home |
|---|---|---|
| Navmesh geometry (vertices, polygons, adjacency) | **Not state at all** — immutable asset | `NavMeshRegistry` |
| Which navmesh an agent walks | **Authoritative** — a name, per Rule 21a | ECS (`NavAgentComponent::navMesh`) |
| Destination | **Authoritative** — gameplay's decision | ECS (`destination` + `hasDestination`) |
| **The planned path (waypoints)** | **Authoritative** — history-dependent, see above | ECS (`path`) |
| **Which waypoint is next** | **Authoritative** — progress along that history | ECS (`pathIndex`) |
| **The goal the current path was planned for** | **Authoritative** — it is what makes "should I replan?" a decision rather than a guess | ECS (`pathGoal`) |
| Agent status (Idle / Following / Arrived / Unreachable) | **Authoritative** — records that a plan was *attempted*, see gray areas | ECS (`status`) |
| Agent position | **Authoritative** — but it is just an entity transform | ECS (`TransformComponent`) — the ordinary one |
| Movement speed, arrival radius | **Authoritative** — gameplay tunes them | ECS |
| A* open/closed sets, `cameFrom` | **Derived** — scratch, per call | stack-local, never stored |
| The polygon corridor before string-pulling | **Derived** — an intermediate of planning | stack-local |
| Which polygon the agent is standing on | **Derived** — a query over the present | recomputed |
| Steering direction / desired velocity this step | **Derived** — a function of position and the next waypoint | recomputed each step |
| Distance remaining | **Derived** | recomputed |

**Nothing derived is ever stored in ECS.**

---

## Gray areas — where this document earns its keep

1. **`status` is authoritative because "we already tried" is not recoverable.**
   `Unreachable` is not a cosmetic label. Without it, an agent with an impossible goal
   re-runs A* over the whole mesh **every fixed step, forever** — and worse, restoring a
   snapshot would silently restart that attempt, so the same frame does different work
   in two runs. This is structurally identical to
   [DESIGN_ANIMATION.md](DESIGN_ANIMATION.md)'s animation-events call ("which events
   already fired is authoritative"): *the record that something was attempted is state,
   even though the attempt itself is a pure function.*

   The consequence is deliberate and stated rather than discovered later: a failed path
   is **not** retried automatically. Gameplay re-arms it by setting a destination again.
   Navigation does not decide when to hope.

2. **`pathGoal` exists so replanning is a comparison, not a guess.** The alternative —
   a `dirty` / `needsRepath` flag that gameplay must remember to set — puts the
   correctness of navigation in the hands of every behavior that ever writes a
   destination, and a forgotten flag is an agent walking confidently to the wrong place.
   Storing the goal the current path was planned for makes the check a pure comparison
   against authoritative state (`destination != pathGoal → replan`), which cannot be
   forgotten and survives a restore. One extra `vec3` buys the removal of an entire
   class of caller-error bugs (Rule 7).

3. **The path is serialized in full, and that is accepted, not regretted.** It is the
   one genuinely bulky thing navigation adds to a snapshot (a handful of `vec3`s per
   agent). Storing only the *corridor* (polygon indices) would be smaller — but the
   waypoints are the string-pulled result, so reconstructing them still requires the
   funnel pass, and the polygon indices would then be an index into an asset that a
   re-bake can renumber. That is exactly the fragility Rule 21a warns about, traded for
   bytes. Measure before optimizing (Rule 18); if snapshots ever show navigation
   dominating, the fix is a compact encoding of the same authoritative data, not a
   demotion of it to derived.

4. **Agents do not own their position; the transform does.** A `NavAgentComponent` that
   cached its own `currentPosition` would be a second owner able to disagree with
   `TransformComponent` after a restore — Rule 21 in its plainest form. The agent's
   position is read from, and written to, the ordinary transform.

5. **Determinism is a property of the planner, not a hope about it.** A* must break ties
   by a stable key or two runs can expand equal-cost nodes in different orders and
   return different (equally optimal) paths. The comparator orders by `(f, polygon
   index)` — a total order, so the priority queue's lack of stability cannot leak. The
   self-test pins it by planning the same query twice and comparing waypoints exactly.

---

## Data flow

```
Fixed step ──► destination changed? ──► plan (A* + funnel) ──► write path to ECS
                                                                     │
              steer toward path[pathIndex] ◄───────────────────────┘
                        │
                write TransformComponent ──► Physics ──► Animation depicts it
              (authoritative)
```

Which is, again, the **same shape as the rest of the engine**:

```
Input ──► Behavior   ──► ECS ──► Renderer     (gameplay)
Input ──► Intent     ──► ECS ──► RmlUi        (runtime UI)
  dt  ──► Animation  ──► ECS ──► Renderer     (animation)
  dt  ──► Navigation ──► ECS ──► Physics      (navigation)
```

One mental model for the whole engine: **disposable logic, authoritative state in
components, derived views recomputed.** Navigation is the fourth instance of a pattern
the engine has now proved three times, rather than a new architecture.

---

## Determinism: the fixed step is the only clock

Navigation advances **only** on the 60 Hz fixed step. Two consequences that are easy to
get wrong, both direct echoes of lessons the engine already paid for:

- **Waypoint advance must consume the whole step, not one waypoint per step.** Moving
  "toward the next waypoint by `speed * dt`, and advance if we reached it" silently
  caps an agent's speed at one waypoint per frame, so a fast agent crawls through a
  cluster of close waypoints. This is the same shape as 17A's loop-wrap bug (`time -=
  duration` breaks the moment one step overshoots the clip): the fix is to spend the
  step's full travel budget in a loop, not to assume one step crosses at most one
  boundary. The self-test pins it with a step long enough to cross several waypoints.
- **Plan, then move, in the same step.** An agent that plans on one step and starts
  moving on the next is a frame of latency that depends on system order — exactly the
  class of thing the scheduler's declared access sets exist to make visible.

The system declares `reads: NavAgent | Transform`, `writes: NavAgent | Transform` and
runs **after Script** (gameplay sets the destination this step) and **before Animation
and Physics** (the character is moved before its pose is computed and before collision
sees it).

---

## Layering (Rule 15)

Navigation splits along the existing `Editor → Engine → Core` line, and the split falls
out naturally because **planning is pure math over plain data**:

| Piece | Layer | Why |
|---|---|---|
| `NavMesh` (vertices, polygons, adjacency) | **Core** | plain structs, no GPU |
| A*, funnel string-pull, containment queries | **Core** | pure math — and therefore headless-testable |
| `NavAgentComponent` | **Core** | it is ECS state |
| `NavigationSystem` | **Core** | needs only the registry + the navmesh table |
| `NavMeshRegistry` | **Core** | table lives in a `.cpp`, one instance across exe/DLLs |
| Navmesh **baking** from scene meshes | **Engine** | reads imported geometry (18B) |
| Navmesh debug draw, Rebake action | **Editor** | a view + a workflow decision (18C) |

Putting planning in Core is not a technicality — it is what makes the whole subsystem
testable without a GPU (Rule 9), and it is why 18A can ship a *correct, deterministic,
verified* pathfinder before a single navmesh is baked from real geometry.

---

## Component sketch (authoritative only)

```
Navigating entity
└── NavAgentComponent {
        navMesh        : string   // name → NavMeshRegistry (never a raw pointer)
        destination    : vec3     // gameplay's decision
        hasDestination : bool
        path           : vec3[]   // authoritative: history-dependent (see above)
        pathIndex      : int      // progress along it
        pathGoal       : vec3     // what `path` was planned for → replan is a comparison
        status         : enum     // Idle | Following | Arrived | Unreachable
        speed          : float
        arrivalRadius  : float
    }
```

Deliberately **not** in the component: the resolved `NavMesh*`, the polygon corridor,
the current polygon, the steering vector, distance remaining. Every one of those is
derivable from the present, and every one of them, if stored, becomes a second source
of truth that can disagree with the first after a restore.

---

## The navmesh: convex polygons, adjacency by shared edge

A navmesh is a set of **convex** polygons over a shared vertex array, plus per-edge
adjacency. Convexity is the load-bearing property: it is what makes "a straight line
inside one polygon is always walkable" true, which is what makes the funnel
string-pull correct.

- **Polygons index into a flat index array** (`firstIndex` / `count`), the same shape as
  a mesh — one allocation, cache-friendly, and trivially serializable when 18B bakes one.
- **Adjacency is per-edge**: `neighbor[polygon.firstIndex + i]` is the polygon across
  edge *i*, or `-1` for a boundary. Built once at load by matching undirected vertex
  pairs, so the asset format does not have to carry it and cannot carry a stale copy.
- **Queries are 2D (XZ) with height carried along.** Containment, the funnel, and the
  side tests all run in the ground plane; `y` is interpolated from the polygon so agents
  follow slopes. A full 3D navmesh (overlapping floors) is a *baking* problem, not a
  representation one — the polygon soup already supports it.

Planning is the standard two-pass shape, and the two passes are deliberately separate:

1. **A\* over polygons** produces a *corridor* — the sequence of polygons to cross.
   Costs are measured between **portal midpoints**, not polygon centroids, because a
   centroid path over-charges long thin polygons and picks visibly silly corridors.
2. **The funnel (simple stupid funnel algorithm)** string-pulls that corridor into the
   shortest actual line through the portals. Without it, agents walk from polygon center
   to polygon center — the classic zig-zag that makes a navmesh look broken even though
   the search was correct.

Keeping them separate matters beyond tidiness: the corridor is what a future local
avoidance pass (18D) needs to steer *within*, and the string-pulled waypoints are what
the agent follows today. One is not a refinement of the other.

---

## Behavior under the engine's guarantees

- **Snapshot restore / scrub** — the path, progress, goal, and status come back through
  the ordinary snapshot path; the next fixed step continues walking the *same* route it
  was walking, rather than re-deciding. Zero navigation-specific restore code.
- **Time travel** — scrubbing to frame N restores the agent's position *and its plan*,
  so re-running forward reproduces frame N+1 exactly. This is the guarantee that a
  "derived" path would have quietly broken.
- **Code hot reload** — navigation-driving behaviors reload; agent state in components
  survives (the Phase 6 / Phase 12 pattern).
- **Navmesh asset hot reload** — swap the mesh in the registry and agents keep walking.
  Their stored waypoints may then cross geometry that no longer exists, which is the
  honest cost of paths being authoritative; the recovery is a destination re-set, and a
  future 18B may invalidate paths on re-bake explicitly. Stated here so it is a known
  limit rather than a surprise.

---

## Baking (18B), and the Rule 21a obligation it discharges

`NavAgentComponent::navMesh` is a **name**, which makes it serializable and
hot-reloadable — and puts it squarely under [RULES.md](../RULES.md) Rule 21a:

> If authoritative state references an asset by name, something must deterministically
> reconstruct that asset from the name alone **on scene load**.

18B discharges it, and the answer turns out to be better than the one clips and skins
got. For those, reconstitution means re-reading the model file. For a navmesh, **the
scene already carries its own bake inputs**: `NavMeshSourceComponent` names the navmesh
a piece of geometry contributes to, so a loaded scene can rebuild the mesh from nothing
but the entities it just created. No side file, nothing to keep in sync.

### The consequence that makes navigation different

A navmesh is derived from the **whole scene**, not from one asset file. So it *cannot*
be reconstituted per-entity the way `ModelImporter::ensureModelAssets` is — every source
entity has to exist and be parented first, because the bake reads world transforms.
Reconstitution is therefore a **post-load step** (`ensureSceneNavMeshes`, called at the
end of the load and patch paths), not an inline one. Rule 21a says *something* must
rebuild the asset; it does not say that something runs per component, and navigation is
the first case where it cannot.

### Where the bake is split, and why it matters more than it looks

```
Engine  NavMeshBaker  ── harvests world-space triangles from Mesh/ResourceManager
                          │  (the only navigation code that knows Vulkan exists)
Core    buildNavMesh   ◄──┘  weld → slope filter → adjacency.  Plain data in, NavMesh out.
```

The same boundary `GltfLoader` draws for animation. It is worth insisting on for a
reason 18A learned the hard way: a bake that took `Mesh` (which includes `vulkan.h`)
would be untestable headlessly — and the coverage audit that came out of 18A found the
engine's **scene-load path had gone its entire history untested** for exactly that
reason. Testability is not a nicety you add to an algorithm; it is a property of where
you put the boundary.

### Decisions inside the bake

- **Welding is load-bearing, not cleanup.** `NavMesh::buildAdjacency` matches edges by
  *vertex index*, so triangles that merely touch geometrically share no edge until they
  are welded. An unwelded bake produces a mesh where every triangle is its own island
  and every path is `Unreachable` — which presents to a user as "pathfinding is broken",
  not as "the bake is wrong". Hence `NavBakeStats::isolatedPolygons`: the statistic
  exists to name the cause of the symptom.
- **Winding decides floor from ceiling.** The slope test compares the *signed* normal
  against +Y, so a downward-facing surface is rejected rather than silently becoming
  ground you can stand on from below. A real constraint on source geometry, stated:
  double-sided or inside-out meshes bake wrong.
- **Degeneracy is checked twice** — once on area, and again *after* welding, because
  welding can collapse a thin-but-valid triangle into a line. A polygon with a repeated
  index would otherwise match its own edge in `buildAdjacency`.
- **Triangles stay triangles.** Merging coplanar neighbours into larger convex polygons
  (as Recast does) buys no correctness — the funnel string-pulls a triangulated floor
  into the same straight line a merged one gives, so merging only shrinks the A* node
  count. That is an optimization and nothing has measured the search (Rule 18). The
  self-test pins the claim by crossing a triangulated plane and asserting *one*
  waypoint.
- **An empty bake is not registered.** Registering it would make
  `NavMeshRegistry::has(name)` true and convince `ensureBaked` the work was already
  done — a *cached failure*, which outlives whatever caused it. Leaving the name
  unresolved keeps agents `Idle` (not `Unreachable`) so the next attempt actually runs.
- **`ensureBaked` must not re-bake a registered name.** Without that check the post-load
  hook would overwrite a good navmesh with whatever the current harvest returns. A hook
  that destroys the thing it exists to restore is a worse bug than the one it fixes, so
  it gets its own test.

---

## Editor (18C) — where workflow decisions live

Everything in 18A and 18B was about *runtime correctness*: deterministic planning,
authoritative paths, pure baking, reconstruction. 18C is the first navigation work that
is a **workflow** decision, and that is precisely why it belongs in the editor rather
than in the navigation system.

- **The Rebake action is explicit, and that is the design.** `ensureBaked` only fills in
  a *missing* navmesh, so editing a platform leaves the mesh stale. The alternative —
  rebuild automatically whenever geometry changes — is a performance trade nobody has
  measured at scene scale, and choosing it now would be exactly the speculative
  optimization Rule 18 rejects. A button keeps the trade-off **evidence-driven**: when a
  real project makes manual rebaking annoying, that annoyance *is* the evidence, and the
  button is what an automatic policy replaces.
- **The overlay is ImGui, not a Vulkan pipeline.** The editor is Dear ImGui (Rule 11),
  so the navmesh and path overlay project to screen and draw through `ImDrawList`. No
  renderer changes, no shader, no pipeline state — and the overlay stays a pure *read*
  of ECS plus the navmesh registry. It renders state and owns none.
- **Authoritative fields are shown but not editable.** The inspector exposes destination,
  speed, and arrival radius — what gameplay would legitimately set — while `status`,
  `path`, and `pathIndex` are read-only. Editing a path index by hand would mean editing
  the middle of a decision the system made; the correct affordance is *Set Destination*,
  which re-arms planning through `setDestination` so a repeat of a previously
  `Unreachable` goal actually re-plans.
- **The panel names causes, not just symptoms.** `NavBakeStats` surfaces here because
  "your navmesh is empty" is a far worse thing to show a developer than "412 triangles,
  all rejected as too steep". Isolated polygons get a specific warning pointing at
  `weldEpsilon`, and an agent whose navmesh is unbaked is told so by name — the Rule 21a
  symptom is otherwise a component that looks perfectly correct and does nothing.

### The overlay bug worth keeping

The first overlay drew nothing, and the reason generalizes to any 3D-to-2D editor gizmo:

> A polygon with a corner **behind the camera** must be **clipped, not rejected.**

A point with `w <= 0` projects to a *mirrored* position in front of the viewer, so it
cannot be drawn as-is — and the tempting fix, skipping any polygon with such a corner,
fails exactly when it matters: stand on a large ground quad and every corner behind you
drops out, so the navmesh vanishes at the moment you are close enough to care. The fix
is Sutherland–Hodgman against the near plane for polygons, and per-segment trimming for
polylines.

It was found by **instrumenting rather than guessing** (`docs/DEV_ENVIRONMENT.md` #3):
one run logging projected corner coordinates showed `(0,0) (0,0) (754,105)` — two
corners silently failing to project — which several rounds of staring at a screenshot
would not have revealed.

---

## 18D — erosion before planning, avoidance after

The pipeline, and the separation that keeps it reasonable:

```
agent-radius erosion  ──►  A*  ──►  corridor  ──►  local avoidance  ──►  steering
   (bake time)                                        (per step)
```

**Erosion belongs before planning** because it changes the traversable space itself:
it is a property of the navmesh, baked once. **Avoidance belongs after planning**
because it responds to transient conditions. Collapsing the two gives a planner that
disagrees with the space it is planning in.

The rule that falls out, and the one to hold onto:

> **Avoidance changes _how_ an agent traverses its corridor, never _which_ corridor it
> chose.**

An agent stepping aside for a moving crate is still following the route it planned, and
rejoins it once the crate has gone. If avoidance could edit `path`, a transient
condition would silently overwrite a planning decision — two things with completely
different lifetimes sharing one piece of state. `steerAroundObstacles` therefore never
touches `path`, and the self-test asserts the plan is byte-identical at *every step* of
a detour, not merely at the end.

### Why the obstacle is not baked into the navmesh

Baked geometry defines where an agent *may* go; an obstacle is a condition it meets
while going there. Folding obstacles into the mesh would mean rebaking whenever one
moved, and would let a crate redefine which corridor the planner picked.
`NavObstacleComponent` therefore carries only a radius — its position is the entity's
ordinary `TransformComponent`, never a second copy.

### Avoidance is derived, and Rule 21b says exactly why

The steering push is a pure function of the **present**: obstacle positions, agent
position, radii, desired direction. Nothing is remembered between steps, so recomputing
it from today's state cannot produce a different valid answer — derived, no ECS
presence, nothing serialized. Contrast the path in the same component, which *is*
authoritative for precisely the opposite reason.

This also marks the boundary in advance. If reciprocal oscillation ever needs a
remembered "preferred side" (two agents dancing in a doorway), that preference **is**
history and must become an ECS field, not a private member. Rule 21b's bug arrives in
exactly that shape, and naming it here is cheaper than rediscovering it.

### The bug this phase actually found: repulsion is not avoidance

The first implementation pushed agents directly away from obstacles. An agent walking
into an obstacle squarely between it and its waypoint then computed
`desired + push == 0` — it **stopped dead a clearance-width short and never arrived**.

```
desired  →→→→→→
push     ←←←←←←      sum = 0     agent parks forever
```

Purely radial repulsion answers *"get away from it"* when the question is *"get
**around** it"*. Each obstacle now contributes a radial term **and a tangential one**,
sided toward the goal — the tangent is what turns a standoff into an orbit. The
tie-break when an obstacle is exactly dead ahead is a fixed sign, never a random one:
a random nudge would break replay for the one case that most needs to reproduce.

### Erosion granularity, stated not discovered

Erosion drops whole polygons within `agentRadius` of a boundary rather than offsetting
the boundary inward and re-triangulating. It therefore **over-erodes by up to one
polygon's width** — coarse, but predictable and trivial to reason about. The right fix
when that is not enough is voxelization (the 18B open question), not a more elaborate
polygon offset. It defaults to **off**: erosion that switched itself on would silently
shrink an existing navmesh on the next rebake, and a navmesh that quietly got smaller is
far harder to diagnose than one that never eroded.

Two radii exist deliberately, and they answer different questions:

| Field | Question | When |
|---|---|---|
| `NavBakeParams::agentRadius` | *where may I plan?* | bake |
| `NavAgentComponent::radius` | *how close may I pass?* | steering |

### Known limit of 18B, stated not discovered

`ensureBaked` only bakes a name that is **missing**, so a navmesh does not rebuild when
scene geometry *changes*. Editing a platform leaves a stale navmesh until the name is
cleared. That is deliberate for now — an automatic rebuild on every geometry edit is a
performance decision nobody has measured — and the fix is an explicit editor "Rebake"
action in 18C, plus a considered invalidation policy. Agent-radius erosion is likewise
still absent: it belongs in the bake (pay once) rather than at query time (pay every
step), and agents are points on a mesh assumed pre-shrunk until it lands.

---

## Open questions (to resolve at implementation, not before)

1. ~~**Navmesh baking** (18B) — voxelize-and-march (Recast-style) vs. deriving polygons
   directly from flagged walkable geometry.~~ **Resolved in 18B: derived directly from
   flagged geometry.** `NavMeshSourceComponent` names the navmesh a mesh contributes to,
   and the bake welds + slope-filters that triangle soup. Voxelization was not needed to
   get a correct, connected, deterministic mesh, and it would have made the bake
   unanswerable to a headless test. It becomes the right answer when overhangs and
   agent-radius erosion over arbitrary geometry are required — i.e. when a real level
   asks, not before.
2. **Dynamic obstacles / local avoidance** (18D) — steering-only (agents keep their path
   and dodge locally) vs. path invalidation. The corridor is retained precisely so the
   first option stays available.
3. **Off-mesh links** (jumps, ladders, doors) — an edge that is not a shared polygon
   edge. Does not affect the state model: a link is asset data, and traversing one is a
   status.
4. **Agent radius** — currently a point agent on a mesh assumed pre-shrunk by the bake.
   Erosion belongs in 18B (bake once) rather than at query time (pay every step).

---

## Rule linkage

This record is the navigation-specific application of **[RULES.md](../RULES.md) Rule 21**,
and it contributes the sharpening that the other two records converge on:

> *The navmesh is an immutable asset and planning is a pure function of it — but a
> **path in progress is authoritative**, because it is a function of where the agent was
> when it planned, and the present cannot reconstruct the past. Destination, path,
> progress, planned-for goal, and status live in ECS; corridors, steering, containment
> queries, and search scratch are derived. Therefore snapshot restore / time travel /
> hot reload continue an agent's journey rather than re-deciding it.*
