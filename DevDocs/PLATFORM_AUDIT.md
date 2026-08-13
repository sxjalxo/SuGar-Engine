# Platform audit — what is proven, and what merely exists

*Snapshot: 2026-08-13, gate 56/56 Debug + Release, packaged standalone verified.*

M4's question was "did a real game expose this as missing?". After a Minecraft-scale dogfood
that forced seven engine seams and found eleven engine defects, the answer is increasingly
*no* — which is a success condition, and also the moment the question stops being useful.
This audit asks a different one, per subsystem:

1. **Seam** — is there an intentional engine boundary, or only an implementation?
2. **Game** — has a *real game* driven it? A self-test does not count.
3. **Stress** — has it survived a deliberately hostile workload? If not it is **unproven**,
   not green.
4. **Open decision** — is there an unresolved architectural question? Those are M4/M5
   candidates, not accidental future work.
5. **Widen now?** — would widening the seam *today* avoid a likely rewrite later? Rule 22
   says fix the category; it does not say build everything, and this column is where the
   two meet.

Evidence is this session's measurements and the friction log (`ROADMAP.md` #20–#36), not
recollection.

## The matrix

| Subsystem | Seam | Game | Stress | Open architectural decision | Widen now? |
| --- | --- | --- | --- | --- | --- |
| ECS / Registry | yes | **heavy** — 300+ entities, streamed create/destroy | **yes** — id-gap OOM fixed, 4 395 chunk loads | generational entity ids (a stale `Entity` is currently detectable only by absence) | **yes, cheap** — an id is stored in save files and game data; widening later migrates data |
| Game data (`GameDataComponent`) | yes (#21) | **heavy** — all player/mob/particle state | partial — 16 000 particles made its per-entity `std::map<string>` the cost | string-keyed map is O(log n) per access and allocates; a typed/interned key would not change the API | no — measured cost only appears past 8 000 entities |
| Scene serialization / snapshots | yes | yes — snapshot policy budget in play | **yes** — `std::to_chars` fix, byte-identical golden | binary backend still **not forced** (formatting was the cost, not the format) | no |
| Persistence (`SaveData`) | yes | **heavy** — 197 272 edits, 2.5 MB, 413 FPS | **yes** — 8 hostile files, no crash; coordinate-aliasing defect found and fixed | none | no |
| Asset pipeline (cook / import / reimport / manifest) | yes | yes — every texture and model | partial — hot reload not stressed *while* runtime assets are live | none known | no |
| `AssetGateway` + runtime meshes | yes (#17/#18) | **heavy** — every chunk mesh | **yes** — 7 659 create/release, 2 318 forced remeshes, validation clean | batched submit / fence contract (`createMesh` means renderable on return) — deliberately deferred (#34) | no — the contract change is the expensive part, and nothing needs it |
| GPU memory (`DeviceMemoryPool`) | yes (#34) | yes — all runtime meshes | **yes** — 10 370 buffers, 1 block, no fragmentation growth | images and staging deliberately out of scope | no |
| Rendering (draw list, instancing, blend modes, shadows) | yes | **heavy** | **yes** — 16 181 items → 118 draw calls flat | none forced; per-item CPU is the ceiling, not submission | no |
| Lighting (`LightComponent`) | yes (#22) | yes — day/night + torches | partial — never run with hundreds of dynamic lights | `MAX_LIGHTS = 8` with nearest-N selection; a real light-heavy scene would force clustering | no — wait for a game that lights a room |
| Runtime UI (RmlUi) + world labels | yes (#23/#29) | **heavy** — HUD, panels, 32 nameplates | partial — never stressed with hundreds of elements | depth occlusion for world labels (they show through walls) | no |
| Editor UI (ImGui) | yes | **n/a** — the editor is not a game | n/a | none | no |
| Input + cursor capture | yes (#12/#24) | yes | no | none | no |
| Camera (`CameraComponent`) | yes (#16) | yes — first-person rig through a hierarchy | no | none | no |
| Navigation — mesh, bake, links | yes | **heavy** — 18 000 polys, rebaked per chunk crossing | **yes** — three defects found (#30/#31/#33), swim/jump chain validated end to end | **tiled navmesh with dirty-tile rebuild** — a one-block edit currently costs a whole bake | **maybe** — the game debounces around it today; a second game with frequent geometry changes decides it |
| Navigation — agents | yes | yes — 58 agents | **yes** — found #36 | **failed-replan backoff**: no wait between failures, so N agents on an unreachable goal cost N searches per frame. Policy questions (how long? does an opened route wake it? per agent type? engine or game?) make this a design problem, not a constant | no — pick the policy with a second game's evidence |
| Physics (rigid bodies, colliders, queries) | yes | **thin** — L1 uses `RigidBodyComponent` (6 refs); **no game has ever added a `ColliderComponent`** | synthetic only (`PhysicsBroadphase`, `PhysicsDeterminism`) | CCD; the voxel game rolled its own collision because per-block colliders were the wrong shape | — |
| Animation / skinning | yes | **none** — zero `animators` / `skinnedMeshes` in any of the three games | synthetic only | unknown until a game drives it | — |
| Audio | yes | **none** — zero `audioSources` in any game | synthetic only | real-time mixer thread vs game thread ownership | — |
| Behaviors + hot reload | yes | yes — every game is a `Game.dll` | partial — reload *while runtime assets are live* untested | none known | no |
| Packaging | yes | **heavy** — every measurement ran packaged | partial — `verify()` on every build, no hostile manifest | none | no |
| Crash reporting | yes | yes | no — never deliberately crashed | none | no |
| Particles | **none, on purpose** | game-built from transforms + materials + runtime meshes | **yes** — ~4 000 live is the budget; found #35 | whether the engine should own one at all. Evidence so far says no | no |

## What the audit actually found

**Three subsystems are implemented and tested but have never been driven by a game:
animation/skinning, audio, and collision.** Not "under-tested" — *unused*. Across all three
dogfood games there is not one `ColliderComponent`, `AnimatorComponent`, `SkinnedMeshComponent`
or `AudioSourceComponent`. They have self-tests, and self-tests are written by the same person
who wrote the code, against the shape the code already has.

That is the gap this audit exists to surface, and it is not a missing feature — it is missing
*evidence*. Every one of those three is exactly what a small combat game uses on its first
day: a projectile needs a collider, a hit needs a sound, a swing needs a clip.

**One column is worth acting on independently:** generational entity ids. An `Entity` is a
bare integer today, and it is written into save files and game data. If a second game shows
that stale handles matter, widening the id *later* means migrating every artifact that stored
one. That is the "widen now to avoid a rewrite" case; nothing else in the matrix is.

**Two named-and-deferred decisions stay deferred:** tiled navmesh rebuild, and failed-replan
backoff. Both have measurements; neither has a workload that forces the policy choice.

## Conclusion

No critical missing seam. The next move is the orthogonal game, not more engine work — a
small combat arena is the shortest path to evidence about the three unused subsystems, and it
tests the real question underneath M4: **can SuGar support a materially different game
without being rewritten for it?**

That arena is **another L3 game**, not a new tier. L3 is the tier for core game *mechanics*;
it ends when the mechanics stop producing new answers, and this audit says three of them have
not produced any yet. L4 — rendering quality, high-DPI/4K, vendor features (FreeSync, DLSS),
GPU-driven culling and the deferred optimizations above — begins after that, and this matrix
is what will decide when.
