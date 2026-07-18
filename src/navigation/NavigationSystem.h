#pragma once

class Registry;

// Phase 18A — the ECS-facing navigation system. Each fixed step it plans a route for
// any agent whose destination has changed, then walks every following agent along
// its path by writing TransformComponent.
//
// It owns no state of its own: the plan and the progress along it live in
// components, navmeshes in NavMeshRegistry, and steering is recomputed — so
// `Route = f(navmesh, start, goal)` holds for planning while *following* a route
// stays authoritative, and snapshot restore / time travel / hot reload continue an
// agent's journey rather than re-deciding it.
//
// The fixed step is the only clock. See docs/DESIGN_NAVIGATION.md.
namespace NavigationSystem {

// Plans and advances every NavAgentComponent. `dt` is the fixed step.
void update(Registry& registry, float dt);

} // namespace NavigationSystem
