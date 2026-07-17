#include "scene/DrawList.h"
#include "animation/Skin.h"
#include "animation/SkinRegistry.h"
#include "animation/Skinning.h"
#include "assets/ResourceManager.h"
#include "ecs/Registry.h"
#include <algorithm>
#include <tuple>
#include <vector>

void buildDrawListFromECS(const Registry& registry, const std::vector<Light>& lights, DrawList& out) {
    out.items.clear();
    out.items.reserve(registry.transforms.getAll().size());

    std::vector<Entity> orderedEntities;
    orderedEntities.reserve(registry.transforms.getAll().size());

    for (const auto& [entity, transformComponent] : registry.transforms.getAll()) {
        (void)transformComponent;
        orderedEntities.push_back(entity);
    }

    std::sort(orderedEntities.begin(), orderedEntities.end());

    for (Entity entity : orderedEntities) {
        if (!registry.meshes.has(entity) || !registry.materials.has(entity)) {
            continue;
        }

        const auto& transformComponent = registry.transforms.get(entity);
        const auto& meshComponent = registry.meshes.get(entity);
        const auto& materialComponent = registry.materials.get(entity);
        const auto mesh = ResourceManager::getMesh(meshComponent.mesh);
        const auto texture = ResourceManager::getTexture(materialComponent.material.albedo);
        if (!mesh || !texture) {
            continue;
        }

        RenderItem item{};
        item.mesh = mesh;
        item.meshHandle = meshComponent.mesh;
        item.material = materialComponent.material;
        item.model = getWorldMatrix(entity, registry);

        // The pose is resolved here, while we still have the ECS in hand, and handed
        // to the renderer as plain matrices. computeJointMatrices fails (leaving the
        // list empty) when nothing binds, which degrades to an unskinned draw rather
        // than a mesh collapsed onto the origin.
        if (registry.skinnedMeshes.has(entity)) {
            if (const Skin* skin = SkinRegistry::get(registry.skinnedMeshes.get(entity).skin)) {
                Skinning::computeJointMatrices(registry, entity, *skin, item.jointMatrices);
            }
        }

        out.items.push_back(std::move(item));
    }

    std::sort(
        out.items.begin(),
        out.items.end(),
        [](const RenderItem& a, const RenderItem& b) {
            return std::tie(a.material.albedo, a.material.metallic, a.material.roughness, a.material.ao, a.meshHandle) <
                   std::tie(b.material.albedo, b.material.metallic, b.material.roughness, b.material.ao, b.meshHandle);
        }
    );

    out.lights = lights;
}
