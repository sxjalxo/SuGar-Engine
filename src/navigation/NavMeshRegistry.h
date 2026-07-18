#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "navigation/NavMesh.h"

// Maps navmesh names -> immutable navmesh data, deliberately mirroring
// AnimationClipRegistry (which mirrors BehaviorRegistry). NavAgentComponent stores
// only the name, so the indirection survives serialization and lets the table be
// repopulated (a re-bake, asset hot reload) underneath running agents without
// invalidating any component.
//
// Navmeshes are assets, not state: nothing here is snapshotted. Core owns only the
// registry *mechanism*; the Engine layer will fill it from a bake (Phase 18B), and
// headless tests fill it with synthetic meshes — which is what keeps the whole
// planning path testable without a GPU. See docs/DESIGN_NAVIGATION.md.
class NavMeshRegistry {
public:
    // Registering an existing name replaces the mesh (that is what a re-bake is).
    // Adjacency is rebuilt here rather than trusted from the caller, so no
    // registered mesh can carry a stale neighbor table.
    //
    // Any pointer previously handed out by get() is invalidated, which is why
    // callers must resolve by name per step and never cache the pointer.
    static void registerNavMesh(const std::string& name, NavMesh mesh);

    // Returns nullptr if no navmesh is registered under that name. Borrowed for the
    // duration of the call site only — see registerNavMesh.
    static const NavMesh* get(const std::string& name);

    static bool has(const std::string& name);

    // Every registered name, sorted — so the editor's navmesh list has a stable
    // order rather than one that shuffles with the hash table between sessions.
    static std::vector<std::string> names();

    static void clear();

private:
    // In a .cpp, never a header inline: the engine exe, Core, and the game DLL must
    // share exactly one table, for the same reason BehaviorRegistry's does.
    static std::unordered_map<std::string, NavMesh>& table();
};
