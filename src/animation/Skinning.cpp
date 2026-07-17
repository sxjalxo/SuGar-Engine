#include "animation/Skinning.h"

#include "animation/Skin.h"
#include "ecs/Registry.h"

#include <glm/gtc/matrix_inverse.hpp>

namespace Skinning {

bool computeJointMatrices(const Registry& registry,
                          Entity skinnedEntity,
                          const Skin& skin,
                          std::vector<glm::mat4>& out) {
    out.clear();
    if (!skin.valid() || skinnedEntity == INVALID_ENTITY || !registry.transforms.has(skinnedEntity)) {
        return false;
    }

    // Joints are named relative to the whole imported subtree, not to the mesh
    // node — a character's mesh and its skeleton are typically siblings, so
    // searching down from the mesh would find nothing.
    const Entity root = getRootAncestor(skinnedEntity, registry);
    const glm::mat4 inverseModel = glm::inverse(getWorldMatrix(skinnedEntity, registry));

    out.reserve(skin.joints.size());
    bool anyResolved = false;

    for (size_t i = 0; i < skin.joints.size(); i++) {
        const Entity joint = findDescendantByName(registry, root, skin.joints[i]);
        if (joint == INVALID_ENTITY || !registry.transforms.has(joint)) {
            // Identity, not skip: `out` must stay parallel to the skin's joint
            // order, because JOINTS_0 indexes into it. Dropping an entry would
            // silently re-map every joint after it.
            out.push_back(glm::mat4(1.0f));
            continue;
        }
        out.push_back(inverseModel * getWorldMatrix(joint, registry) * skin.inverseBindMatrices[i]);
        anyResolved = true;
    }

    if (!anyResolved) {
        // Nothing bound — report failure so the caller can draw unskinned rather
        // than collapse the mesh onto the origin.
        out.clear();
        return false;
    }
    return true;
}

} // namespace Skinning
