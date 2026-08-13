# Design record — game-defined entity data (the A3 seam)

*Written before the code, as with DESIGN_RUNTIME_UI / DESIGN_ANIMATION / DESIGN_NAVIGATION
/ DESIGN_RUNTIME_MESH. M4 Level 3 (voxel game) forced it.*

## The forcing problem

A game module links only `SuGarCore` and **cannot add a component type to `Registry`**.
The component set is fixed at engine compile time. Every dogfood game so far worked
around that; Level 3 could not:

| What the game needed | Where it had to live | What broke |
|---|---|---|
| player vertical velocity, look pitch, selected hotbar slot | module globals / fields on the `Behavior` subclass | `Behavior`'s own contract says per-entity state must live in components; globals do not serialize, do not snapshot, and are wrong the moment there are two instances |
| mob health / kind / attack cooldown / wander timer | nowhere | a single shared `Behavior` instance ticks *every* mob — one field would be one value for all of them |
| whether a chunk has player edits | nowhere | a save file cannot record what it cannot see |

The last two are not workaroundable: mobs are the first case where the engine's rule
("state in components") and the engine's capability ("you may not define a component")
directly contradict. That contradiction is the seam.

## What the mature engines do

- **Unity** — a component *is* a user C# class; the engine's serializer reflects over its
  fields.
- **Unreal** — `UCLASS`/`UPROPERTY` plus a code generator, same idea with a build step.
- **Godot** — script variables, `@export`ed into the inspector and the scene file.

All three answer with **reflection over game-declared types**. SuGar has no reflection,
and adding one is explicitly on the "rabbit hole, do not build until forced" list — the
game forced *per-entity gameplay state*, it did not force a type system.

## The decision

Add **one** engine-owned component whose *contents* are owned by the game:

```cpp
struct GameDataComponent {           // ecs/GameData.h
    std::map<std::string, GameValue> values;   // key -> number | string
};
```

The engine stores, serializes, snapshots, inspects and destroys it. The engine never
*interprets* it: keys and their meaning are the game's namespace. This is deliberately
the same trade the asset pipeline already makes with `AssetMeta::settings` — the database
carries and hashes untyped import settings, and each importer claims the keys it
understands (`docs/DESIGN_ASSET_PIPELINE.md`, 19C).

### Why this and not the alternatives

- **Reflection / a game-registered type system.** The correct long-term answer and the
  most expensive one: it changes ECS identity, serialization, undo, the inspector and the
  editor's add-component menu at once. Nothing in Level 3 needs *types* — it needs a place
  to put six floats per mob. Rule 8: design the seam wide, build the narrow thing.
- **An opaque byte blob per entity.** Cheapest to implement, worst to live with: scenes
  stop being diffable and hand-editable, and the inspector can only show its length. The
  whole point of the JSON scene format is that a human can read it.
- **Leave it in globals / behavior fields.** Silently broken for every multi-instance case
  and invisible to snapshot restore. That is the bug this seam exists to remove.

### Authoritative, by Rule 21b

Ask the history test: can recomputing this from the present give a different valid answer?
A mob's remaining health, a cooldown mid-tick, which hotbar slot the player picked — all
of them are functions of what *happened*, not of the current scene. So it is simulation
state, it lives in ECS, and it is serialized. (Anything derived — the chunk mesh, the
camera pose — stays out, as before.)

### Value types: number and string, nothing else

The scene JSON parser already folds `true`/`false` into numbers so every consumer can read
them uniformly. Game data inherits that: a value is a **double** or a **string**, and a
flag is a number that is `0` or `1`. Teaching the parser a distinct boolean type would
touch every existing `getFloatValue` caller to make a game write `true` instead of `1`.

`std::map`, not `unordered_map`: the file must serialize in a stable order or two runs
produce different bytes for the same state — the same reason `AssetMeta::settings` is a
`map` (Rule 10, and snapshot comparison depends on it).

### Serialization shape

```json
"gameData": { "hp": 12, "kind": "cow", "cooldown": 0.75 }
```

Natural JSON, diffable, hand-editable, and the snapshot/time-travel path gets it for free
because a snapshot *is* the scene serializer. Absent block ⇒ no component (back-compat with
every existing scene).

### What it is NOT for

**World-sized data.** The voxel world is ~750 000 bytes; it belongs in the game's own save
file (`core/SaveData`), not in a scene entity, and the terrain is regenerated from
`f(seed)` so only the player's *edits* need storing. Putting a world in game data would
turn every snapshot into a world copy — the exact mistake the chunk representation just
finished undoing (Report.md).

**Cross-entity indexes.** A key holds this entity's own state. A game that wants "all mobs
near X" iterates the storage; it does not build a registry inside a component.

## Scope built now (narrow)

1. `GameDataComponent` + `GameValue` in Core (`src/ecs/GameData.h`), with typed accessors
   (`getNumber/setNumber/getString/setString/has/remove`) so game code never touches the
   map directly, and `ensureGameData(registry, entity)`.
2. `Registry::gameData` storage + `ComponentType::GameData` trait + destroy/reset.
3. `SceneSerializer` write / load / patch.
4. The Script stage's declared access set gains `GameData` (behaviors are its only writer).
5. Editor: the Inspector shows and edits the pairs (numbers and strings).
6. Self-test: round-trip, patch-restore, absent-block back-compat.

Deliberately **not** built: typed schemas, per-key change notification, an editor
"add key" UI beyond the basics, arrays/nested objects as values. Each waits for a game.
