#pragma once

#include <string>
#include <vector>

#include "navigation/NavMeshBuilder.h"

class Registry;

// Phase 18B — baking, the *scene-facing* half. This is the *only* navigation code
// that knows `Mesh`, `ResourceManager`, or Vulkan exist; it harvests world-space
// triangles out of the scene and hands them to Core's buildNavMesh, which knows none
// of that. Same boundary GltfLoader draws for animation.
//
// See docs/DESIGN_NAVIGATION.md.
namespace NavMeshBaker {

// Every entity carrying NavMeshSourceComponent{navMesh == name} plus a MeshComponent,
// with its geometry transformed into world space by the entity's world matrix.
// Exposed separately from bake() so tests and the editor can inspect the input rather
// than only the result — "the navmesh is empty" and "nothing was harvested" are
// different problems and should be distinguishable.
std::vector<NavTriangle> harvestTriangles(const Registry& registry, const std::string& name);

// Harvest + build + register under `name`. Returns the bake statistics.
//
// A bake producing **zero polygons is not registered.** Registering an empty mesh
// would make NavMeshRegistry::has(name) true, which would then convince ensureBaked
// that the work was already done — a cached failure, and the kind that outlives the
// thing that caused it. Better to leave the name unresolved so agents stay Idle and
// the next attempt actually runs (see NavAgentStatus).
NavBakeStats bake(Registry& registry, const std::string& name, const NavBakeParams& params = {});

// Bakes `name` only if it is not already registered. This is the RULES.md Rule 21a
// obligation for navigation: a scene loaded from disk holds agents naming a navmesh
// that nothing has built yet, and without this they resolve to nothing — the 17C.2
// failure exactly (components look right in the inspector and do nothing).
void ensureBaked(Registry& registry, const std::string& name);

// Statistics from the most recent bake of `name`, or nullptr if it has never been
// baked this session.
//
// **Editor telemetry, not state.** It is derived, never read by gameplay, never
// serialized, and rebuilt by baking — so it stays outside ECS without violating
// Rule 21. It exists because "your navmesh is empty" is a far worse thing to show a
// developer than "412 triangles, all rejected as too steep" (Rule 1).
const NavBakeStats* lastStats(const std::string& name);

// Every navmesh named by any NavMeshSourceComponent in the scene, sorted. The editor
// lists these so a name that has sources but has never baked is still visible —
// otherwise the one navmesh you most need to look at is the one that isn't there.
std::vector<std::string> sourceNavMeshNames(const Registry& registry);

// Every navmesh named by any NavMeshSourceComponent in the scene. The post-load hook:
// it must run *after* all entities exist and are parented, because a navmesh is
// derived from the whole scene rather than from one asset file — which is why this
// cannot be an inline per-entity call like ModelImporter::ensureModelAssets.
void ensureSceneNavMeshes(Registry& registry);

} // namespace NavMeshBaker
