# Design record — Runtime mesh creation (Core-safe), M4 L3

Written before implementation, following the `AssetGateway` precedent. Defines the
ownership boundary, lifetime, failure behaviour, thread/device constraints, and
serialization/package semantics *first*; only then is the smallest create/upload path
the Minecraft game needs implemented.

## 1. Why (the forcing measurement, not a feature wish)

L3 measured per-block-entity rendering (Release):

| entities | draws | FPS | snapshot |
|---:|---:|---:|---:|
| 6 401 | 6 400 | 59 | 100 ms |
| 12 545 | 12 544 | 3.1 | 199 ms |
| 25 601 | 25 600 | 1.2 | 410 ms |

Draw submission is the wall (superlinear collapse ~6k–12k draws). ECS storage (A4) is
**not** the bottleneck; snapshot is linear and already policy-disabled. The workload-fit
answer is **chunk-as-entity**: one entity per chunk carrying a runtime-generated
(greedy-meshed) mesh, which reduces draws, entity count, and snapshot surface *together*.
That needs one capability the engine does not have: a Core-only game turning
game-generated vertices into a renderable `AssetHandle`. This record designs that seam.

Not built (measurements do not justify them): A4 storage rewrite, generic GPU-driven
rendering / instancing, binary/delta snapshots, a general procedural-asset pipeline,
chunk streaming. Revisit each only if the *post-chunking* numbers force it.

## 2. The authoritative / derived split (Rule 21b)

- **Authoritative:** the chunk's voxel data (owned by the game). It is what a player edit
  (break/place) changes.
- **Derived:** the chunk's triangle mesh = f(voxel data). A runtime mesh is a *derived
  GPU resource*, never source, never authored, never a function of a file.

Consequence: a runtime mesh is **not serialized and not round-tripped**. On load (or a
snapshot restore) the game re-generates the mesh from the voxel data, exactly as
navmesh reconstitution rebuilds from scene geometry (Rule 21a). See §7 for what this
means for persistence (and what it defers).

## 3. Ownership boundary (the invariant that made AssetGateway work)

Only **`std::string`, `AssetHandle`, and plain POD/glm vertex data** cross Core→Engine.
No `Mesh`, `Vertex`, `VkBuffer`, `VkDevice`, staging buffer, command pool, or renderer
object is ever handed to Core or the game DLL.

```
Core / Game DLL          owns: voxel data, RuntimeMeshData (CPU verts/indices)
                         calls: AssetGateway::createMesh(data) -> AssetHandle
                                AssetGateway::release(handle)

Engine (backend)         owns: validation, Vertex translation, Vulkan buffer
                                create+upload, ResourceManager entry, GPU lifetime
                                / deferred destruction

Renderer                 consumes: Mesh via AssetHandle (as today)
                         owns: neither voxel data nor mesh generation
```

Boundary type, defined in Core (glm is already a Core dependency; these are math/POD
types, not engine types):

```cpp
// Core: assets/RuntimeMeshData.h
struct RuntimeMeshData {
    std::vector<glm::vec3> positions;   // required
    std::vector<glm::vec3> normals;     // required, same length as positions
    std::vector<glm::vec2> uvs;         // optional; empty => (0,0) per vertex
    std::vector<uint32_t>  indices;     // required; triangle list, each < positions.size()
};
```

The engine translates `RuntimeMeshData` → its private `Vertex` (skin attributes default
to zero — a runtime mesh is never skinned). The game never sees `Vertex`.

Seam entry (added to the existing Core `AssetGateway`):

```cpp
// increfed handle to a new derived GPU mesh, or INVALID_HANDLE on failure/headless.
AssetHandle createMesh(const RuntimeMeshData& data);
// (release() already exists and balances createMesh, same as acquire*)
```

## 4. Who owns the CPU vertex data after createMesh returns

**The caller.** `createMesh` takes `const RuntimeMeshData&`; the engine **copies** what
it needs into its own `Mesh.vertices/indices` and uploads to the GPU before returning.
The engine retains **no pointer into game memory**. The game is free to discard or reuse
its `RuntimeMeshData` (and its voxel scratch) the instant `createMesh` returns.

```
game vertices --> createMesh --> engine copies + uploads --> caller may discard
```

## 5. Lifetime & deferred destruction

- `createMesh` returns a handle with **refcount 1**, owned exactly like an acquired
  asset. Attaching it to an entity's `MeshComponent` and destroying the entity releases
  it through the existing `onReleaseAsset` path — symmetric, no special-casing.
- **Re-meshing a chunk** (after an edit) creates a *new* mesh handle, swaps it onto the
  chunk entity, and releases the *old* one. The old buffer may still be referenced by a
  frame in flight, so its GPU destroy **must be deferred to a safe point**, not run
  synchronously (the same hazard `reloadAsset` handles with `vkDeviceWaitIdle`).
  - **First-slice policy:** the runtime-mesh backend `release` for a runtime mesh idles
    the device before destroying (edits are player-paced and rare; correctness over the
    micro-stall). A per-frame deferred-free queue keyed on the frame fence is the proper
    upgrade and is noted here, to be built only if the idle shows up in a measurement.

## 6. Failure behaviour, thread & device constraints

- **Validation is engine-side** (Core only passes data): reject when `positions` is
  empty, `normals.size() != positions.size()`, `uvs` non-empty but wrong length, any
  index `>= positions.size()`, or counts beyond a sane cap. On any failure `createMesh`
  returns `INVALID_HANDLE` and the game degrades (chunk simply not drawn) — never a
  crash, never a GPU OOB read.
- **Main thread + device only.** Upload uses the command pool/queue, so `createMesh` is
  main-thread-only. Game behaviours already run on the main thread inside the fixed step,
  so a chunk behaviour calling it is safe. Headless / no backend installed ⇒
  `available()` is false and `createMesh` returns `INVALID_HANDLE`.

## 7. Serialization & package semantics (explicitly: non-source)

- A runtime mesh is stored under a synthetic key `runtime://mesh/<id>` and is **never**
  scanned by `AssetDatabase`, cooked by `AssetCooker`, or shipped by `Packager` — the
  `runtime://` scheme is excluded exactly like `builtin://` already is.
- **It does not round-trip through the scene.** If an entity holding a runtime mesh is
  serialized, its `MeshComponent` records the `runtime://` key; on load that key resolves
  to nothing and falls back to `builtin://cube`. That is acceptable *only because the
  mesh is derived*: the game rebuilds it from voxel data on load.
- **Therefore, for this slice, the authoritative voxel data is what must persist — and it
  currently has nowhere to live in the ECS (A3: no game-defined components).** So the
  first slice **regenerates the world from a fixed seed at load and does not persist
  player edits**, and chunk worlds are outside time-travel (snapshots are policy-disabled
  at that scale anyway). Persisting voxel edits is a *separate* future seam (a game-data
  component / blob), deliberately **not** solved here. This keeps the runtime-mesh seam
  small and honest about its boundary.

## 8. Scope of the first implementation

Only the create/upload path above, plus the `runtime://` exclusions in
cooker/packager/database. Chunk meshing itself (greedy mesher, one entity per chunk) is
**game code**, not engine. After it lands, re-run the exact measurement table:

```
             Before (per-block)   After (chunked)
Blocks       25 600 entities      ?
Draws        25 600               ?
FPS          1.2                  ?
Snapshot     410 ms               ?
Memory       ?                    ?
```

If chunking drops 25 600 entities/draws to tens–hundreds and the frame goes healthy, the
game *forced* a chunk representation — evidence, not a decision. Only then reconsider
instancing (C24) or a non-JSON snapshot backend, driven by the new numbers.
