#include "scene/DrawList.h"
#include "animation/Skin.h"
#include "animation/SkinRegistry.h"
#include "animation/Skinning.h"
#include "assets/ResourceManager.h"
#include "ecs/Registry.h"
#include "rendering/UniformBufferObject.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <vector>
#include <glm/geometric.hpp>

void buildDrawListFromECS(const Registry& registry, const std::vector<Light>& lights,
                          const glm::vec3& cameraPosition, DrawList& out) {
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

    // Draw order (the render-queue seam, Rule 22; mirrors Unity/Unreal):
    //   1. all opaque/masked before any translucent/additive — depth writes must land
    //      before anything blends against them;
    //   2. the blended tail sorted back-to-front (farthest first), the painter's order
    //      alpha blending needs to look right without reading depth;
    //   3. within each group, batch by material/mesh to minimise pipeline/descriptor
    //      churn (blended items keep the distance key primary; batching only breaks ties).
    // Distance must stay finite: a NaN/inf world position (a physics blow-up, or a
    // non-finite value that slipped through scene load) would make the `da > db`
    // compare below violate std::sort's strict-weak-ordering requirement, which is
    // undefined behavior — std::sort can then read out of bounds and crash. Map any
    // non-finite distance to a large finite sentinel so the ordering stays total.
    auto distanceToCamera = [&](const RenderItem& item) {
        const glm::vec3 worldPos = glm::vec3(item.model[3]);
        const float distSq = glm::dot(worldPos - cameraPosition, worldPos - cameraPosition);
        return std::isfinite(distSq) ? distSq : std::numeric_limits<float>::max();
    };
    std::sort(
        out.items.begin(),
        out.items.end(),
        [&](const RenderItem& a, const RenderItem& b) {
            const bool aBlended = isBlended(a.material.blendMode);
            const bool bBlended = isBlended(b.material.blendMode);
            if (aBlended != bBlended) {
                return !aBlended; // opaque/masked first
            }
            if (aBlended) {
                const float da = distanceToCamera(a);
                const float db = distanceToCamera(b);
                if (da != db) {
                    return da > db; // farther first (back-to-front)
                }
            }
            return std::tie(a.material.albedo, a.material.metallic, a.material.roughness, a.material.ao, a.meshHandle) <
                   std::tie(b.material.albedo, b.material.metallic, b.material.roughness, b.material.ao, b.meshHandle);
        }
    );

    // Lights: the scene-level array (authored in the scene file, the editor's default
    // lighting) plus every active LightComponent entity, whose position and direction are
    // DERIVED from its world transform — never stored twice (docs/DESIGN_LIGHTING.md).
    out.lights = lights;
    for (Entity entity : orderedEntities) {
        if (!registry.lights.has(entity)) {
            continue;
        }
        const LightComponent& source = registry.lights.get(entity);
        if (!source.active || source.intensity <= 0.0f) {
            continue;
        }
        const glm::mat4 world = getWorldMatrix(entity, registry);
        Light light;
        light.type = source.type;
        light.color = source.color;
        light.intensity = source.intensity;
        light.range = source.range;
        light.castsShadow = source.castsShadow;
        light.position = glm::vec3(world[3]);
        light.direction = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        if (!std::isfinite(light.direction.x) || !std::isfinite(light.direction.y) ||
            !std::isfinite(light.direction.z)) {
            light.direction = glm::vec3(0.0f, -1.0f, 0.0f); // degenerate scale: point down
        }
        out.lights.push_back(light);
    }

    // Selection. A forward single-pass shader costs every light on every fragment, so the
    // renderer takes at most MAX_LIGHTS of them: the shadow-casting directional light
    // first (the shadow pass indexes light 0), then the rest by *influence at the camera* —
    // ambient and directional lights reach everywhere, points fall off with distance.
    // Derived per frame from the camera and the light set, so it is not state (Rule 21b).
    if (out.lights.size() > static_cast<size_t>(MAX_LIGHTS)) {
        std::stable_sort(out.lights.begin(), out.lights.end(),
                         [&](const Light& a, const Light& b) {
                             const auto rank = [&](const Light& light) {
                                 if (light.type == LightType::Directional && light.castsShadow) return 0;
                                 if (light.type != LightType::Point) return 1;
                                 return 2;
                             };
                             const int ra = rank(a), rb = rank(b);
                             if (ra != rb) {
                                 return ra < rb;
                             }
                             if (ra != 2) {
                                 return false; // keep declaration order among non-points
                             }
                             return glm::distance(a.position, cameraPosition) <
                                    glm::distance(b.position, cameraPosition);
                         });
        out.lights.resize(static_cast<size_t>(MAX_LIGHTS));
    }
}
