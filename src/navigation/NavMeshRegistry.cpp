#include "navigation/NavMeshRegistry.h"

#include <algorithm>
#include <utility>

std::unordered_map<std::string, NavMesh>& NavMeshRegistry::table() {
    static std::unordered_map<std::string, NavMesh> instance;
    return instance;
}

void NavMeshRegistry::registerNavMesh(const std::string& name, NavMesh mesh) {
    // Rebuilt, not trusted: adjacency is a pure function of the geometry, so
    // deriving it at the one place meshes enter the engine means no caller can
    // register a mesh whose neighbor table disagrees with its polygons.
    mesh.buildAdjacency();
    table()[name] = std::move(mesh);
}

const NavMesh* NavMeshRegistry::get(const std::string& name) {
    const auto it = table().find(name);
    return it == table().end() ? nullptr : &it->second;
}

bool NavMeshRegistry::has(const std::string& name) {
    return table().find(name) != table().end();
}

std::vector<std::string> NavMeshRegistry::names() {
    std::vector<std::string> result;
    result.reserve(table().size());
    for (const auto& [name, mesh] : table()) {
        (void)mesh;
        result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

void NavMeshRegistry::clear() {
    table().clear();
}
