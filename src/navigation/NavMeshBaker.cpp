#include "navigation/NavMeshBaker.h"

#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <set>

#include "assets/ResourceManager.h"
#include "ecs/Registry.h"
#include "navigation/NavMesh.h"
#include "navigation/NavMeshRegistry.h"
#include "rendering/Mesh.h"

namespace NavMeshBaker {

std::vector<NavTriangle> harvestTriangles(const Registry& registry, const std::string& name) {
    std::vector<NavTriangle> triangles;
    if (name.empty() || !ResourceManager::isInitialized()) {
        // No upload context means no mesh geometry to read. Returning empty rather
        // than throwing keeps a headless run (self-tests, CI) a supported state
        // instead of an error — the lesson the scene-load path taught.
        return triangles;
    }

    for (const auto& [entity, source] : registry.navMeshSources.getAll()) {
        if (source.navMesh != name || !registry.meshes.has(entity) ||
            !registry.transforms.has(entity)) {
            continue;
        }

        const std::shared_ptr<Mesh> mesh = ResourceManager::getMesh(registry.meshes.get(entity).mesh);
        if (!mesh || mesh->indices.size() < 3) {
            continue;
        }

        // World space, not local: the navmesh spans the whole scene, so a platform's
        // own transform has to be baked in here rather than remembered afterwards.
        const glm::mat4 world = getWorldMatrix(entity, registry);

        const auto position = [&](uint32_t index) {
            const Vertex& vertex = mesh->vertices[index];
            return glm::vec3(world * glm::vec4(vertex.pos[0], vertex.pos[1], vertex.pos[2], 1.0f));
        };

        for (std::size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
            const uint32_t i0 = mesh->indices[i];
            const uint32_t i1 = mesh->indices[i + 1];
            const uint32_t i2 = mesh->indices[i + 2];
            if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() ||
                i2 >= mesh->vertices.size()) {
                continue;
            }
            triangles.push_back({ position(i0), position(i1), position(i2) });
        }
    }

    return triangles;
}

namespace {

// Last bake result per navmesh name. Diagnostic only — see lastStats() in the header
// for why this is legitimately outside ECS.
std::map<std::string, NavBakeStats>& statsTable() {
    static std::map<std::string, NavBakeStats> instance;
    return instance;
}

} // namespace

const NavBakeStats* lastStats(const std::string& name) {
    const auto it = statsTable().find(name);
    return it == statsTable().end() ? nullptr : &it->second;
}

std::vector<std::string> sourceNavMeshNames(const Registry& registry) {
    std::set<std::string> unique;
    for (const auto& [entity, source] : registry.navMeshSources.getAll()) {
        (void)entity;
        if (!source.navMesh.empty()) {
            unique.insert(source.navMesh);
        }
    }
    return std::vector<std::string>(unique.begin(), unique.end());
}

NavBakeStats bake(Registry& registry, const std::string& name, const NavBakeParams& params) {
    NavBakeStats stats;
    if (name.empty()) {
        return stats;
    }

    const std::vector<NavTriangle> triangles = harvestTriangles(registry, name);
    NavMesh mesh = buildNavMesh(triangles, params, &stats);

    // Recorded even when the bake produced nothing — a failed bake is precisely the
    // one whose statistics a developer needs to read.
    statsTable()[name] = stats;

    if (mesh.empty()) {
        // Not registered — see the header. An empty registration is a cached
        // failure that would stop every later attempt from running.
        return stats;
    }

    NavMeshRegistry::registerNavMesh(name, std::move(mesh));
    return stats;
}

void ensureBaked(Registry& registry, const std::string& name) {
    if (name.empty() || NavMeshRegistry::has(name)) {
        return;
    }
    const NavBakeStats stats = bake(registry, name);
    if (stats.polygons > 0) {
        std::cout << "[nav] " << name << ": " << stats.describe() << "\n";
    }
}

void ensureSceneNavMeshes(Registry& registry) {
    // std::set, so the same scene bakes its navmeshes in the same order every run —
    // baking is deterministic, and an order that depended on entity iteration would
    // quietly make it not.
    std::set<std::string> names;
    for (const auto& [entity, source] : registry.navMeshSources.getAll()) {
        (void)entity;
        if (!source.navMesh.empty()) {
            names.insert(source.navMesh);
        }
    }

    for (const std::string& name : names) {
        ensureBaked(registry, name);
    }
}

} // namespace NavMeshBaker
