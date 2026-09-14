#include "animation/Skinning.h"

#include "animation/Skin.h"
#include "ecs/Registry.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <string>
#include <utility>
#include <vector>

namespace Skinning {
namespace {

// DESIGN_RENDER_CPU_TIMING.md §13/§14 -- both helpers below hoist work that is invariant
// for the whole call out of the per-joint loop. The scope is deliberate: WITHIN one call
// the registry cannot change, so neither is a cache with an invalidation story to get
// wrong. A cache whose invalidation nobody can state is a bug with a performance benefit;
// these are pure local transformations.
//
// Both are deliberately FLAT (a small vector, linear scan) rather than hash maps. §14
// measured the hash-map version: it cut node visits 5.2x and made the region 8 % SLOWER,
// because two heap-allocating maps per skinned entity cost more than the visits they
// saved. At ~10 subtree nodes and ~4 chain levels a linear scan wins outright, and the
// counters that said otherwise were counting the cheap thing.

// Resolve every joint name in ONE subtree walk, replacing one findDescendantByName call
// (and one heap allocation) PER JOINT.
//
// Visit order is character-for-character findDescendantByName's -- children pushed
// reversed so they pop in declared order -- and a slot is filled only if still empty, so
// FIRST MATCH WINS exactly as before. That is not incidental: picking the other match for
// an ambiguous name would pose characters from the wrong bone with nothing visibly
// broken. SelfTests.h's ambiguous-name case guards it, and was break-tested by reversing
// this very order (61/62).
//
// A skin naming the same joint twice fills both slots from the same first match, which is
// what two findDescendantByName calls did. Hence no `break` on a hit.
//
// Counters are incremented here with the same meaning as in findDescendantByName -- one
// search per walk, one visit per node popped -- so §12's before/after numbers measure the
// same physical work. A rewrite that silently stopped counting would report a total win
// and have measured nothing.
void resolveJoints(const Registry& registry, Entity root, const std::vector<std::string>& names,
                   std::vector<Entity>& resolved) {
    resolved.assign(names.size(), INVALID_ENTITY);
    if (root == INVALID_ENTITY || names.empty()) {
        return;
    }

    registryWorkCounters().subtreeSearches++;

    std::size_t unresolved = names.size();
    std::vector<Entity> pending{ root };
    while (!pending.empty() && unresolved > 0) {
        const Entity entity = pending.back();
        pending.pop_back();
        registryWorkCounters().nodeVisits++;

        if (registry.names.has(entity)) {
            const std::string& name = registry.names.get(entity).name;
            for (std::size_t i = 0; i < names.size(); i++) {
                if (resolved[i] == INVALID_ENTITY && names[i] == name) {
                    resolved[i] = entity;
                    unresolved--;
                }
            }
        }
        if (registry.hierarchy.has(entity)) {
            const auto& children = registry.hierarchy.get(entity).children;
            pending.insert(pending.end(), children.rbegin(), children.rend());
        }
    }
}

// getWorldMatrix, memoised THROUGH THE RECURSION for the duration of one call.
//
// §14: memoising only the top-level call was a memo that could not fire -- each joint is
// asked for exactly once, so it had zero hits by construction and `matrixHops` came back
// bit-identical at 54 598. The sharing between joints is not at the top of the chain, it
// is *inside* it: joints of one skeleton have almost all their ancestors in common. So the
// memo has to be consulted at every level, which means this cannot delegate to
// Registry.h's getWorldMatrix and must walk the chain itself.
//
// Same product of the same matrices in the same order as getWorldMatrix, including its
// two fallbacks (no hierarchy, or a parent that is missing or transform-less, yields the
// local matrix). This changes how often a level is computed, never what it computes.
//
// Returns BY VALUE: the memo is a vector and a recursive call can reallocate it, so a
// reference into it would dangle. 64 bytes copied beats an aliasing bug.
//
// A memo HIT is not a hop and is deliberately not counted -- hops avoided are the exact
// quantity §13.3 predicted a reduction in.
glm::mat4 memoWorldMatrix(const Registry& registry, Entity entity,
                          std::vector<std::pair<Entity, glm::mat4>>& memo) {
    for (const auto& entry : memo) {
        if (entry.first == entity) {
            return entry.second;
        }
    }

    registryWorkCounters().matrixHops++;
    const glm::mat4 local = registry.transforms.get(entity).transform.getLocalMatrix();

    glm::mat4 world = local;
    if (registry.hierarchy.has(entity)) {
        const Entity parent = registry.hierarchy.get(entity).parent;
        if (parent != INVALID_ENTITY && registry.transforms.has(parent)) {
            world = memoWorldMatrix(registry, parent, memo) * local;
        }
    }

    memo.emplace_back(entity, world);
    return world;
}

} // namespace

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

    // §13.1 — the two invariants, resolved once for the whole call.
    std::vector<Entity> jointEntities;
    resolveJoints(registry, root, skin.joints, jointEntities);

    std::vector<std::pair<Entity, glm::mat4>> worldMatrices;
    worldMatrices.reserve(skin.joints.size() + 4); // joints + their shared chain, typically

    const glm::mat4 inverseModel =
        glm::inverse(memoWorldMatrix(registry, skinnedEntity, worldMatrices));

    out.reserve(skin.joints.size());
    bool anyResolved = false;

    for (size_t i = 0; i < skin.joints.size(); i++) {
        const Entity joint = jointEntities[i];
        if (joint == INVALID_ENTITY || !registry.transforms.has(joint)) {
            // Identity, not skip: `out` must stay parallel to the skin's joint
            // order, because JOINTS_0 indexes into it. Dropping an entry would
            // silently re-map every joint after it.
            out.push_back(glm::mat4(1.0f));
            continue;
        }
        out.push_back(inverseModel * memoWorldMatrix(registry, joint, worldMatrices) *
                      skin.inverseBindMatrices[i]);
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
