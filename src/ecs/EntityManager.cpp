#include "ecs/EntityManager.h"

#include <algorithm>

namespace {

// Advances a slot's generation, skipping 0 so a zeroed handle never matches.
//
// On wrap (after 4 095 reuses of the SAME slot) aliasing becomes possible again for
// that one slot: a handle from 4 096 tenants ago would match the new one. That is
// deliberate. The alternative — retiring the slot permanently — trades a rare
// aliasing window for a guaranteed leak of index space, and the wrap degrades to the
// pre-generational behaviour rather than to something worse. The measured worst case
// in a dogfood game was ~250 reuses of a hot slot in 90 seconds.
Entity nextGeneration(Entity generation) {
    return generation >= MAX_ENTITY_GENERATION ? Entity{1} : generation + 1;
}

} // namespace

Entity EntityManager::createEntity() {
    Entity index;
    if (!freeIndices.empty()) {
        index = freeIndices.back();
        freeIndices.pop_back();
    } else {
        if (nextIndex > MAX_ENTITY_INDEX) {
            return INVALID_ENTITY; // index space exhausted; see the header
        }
        index = nextIndex++;
        slots.resize(index + 1);
    }

    Slot& slot = slots[index];
    slot.generation = nextGeneration(slot.generation);
    slot.alive = true;
    return makeEntity(index, slot.generation);
}

Entity EntityManager::createEntityWithId(Entity id) {
    if (id == INVALID_ENTITY) {
        return INVALID_ENTITY;
    }

    const Entity index = entityIndex(id);
    const Entity generation = entityGeneration(id);
    if (index == 0 || generation == 0) {
        // Both are reserved, so a handle carrying either is not one this manager ever
        // issued — a corrupt undo record or a hostile prefab-with-ids list. Refuse it
        // rather than seating a tenant in the reserved slot.
        return INVALID_ENTITY;
    }

    if (index < nextIndex) {
        // Within the allocated range: claim it from the free list if it was destroyed.
        // If it is NOT on the free list the slot is currently live, and honoring the
        // request re-seats it under the requested generation — the caller asked for a
        // specific identity and gets it. That matches what the pre-generational code
        // did (it returned the id regardless), and the restore paths never hit it
        // because they destroy before they restore.
        const auto it = std::find(freeIndices.begin(), freeIndices.end(), index);
        if (it != freeIndices.end()) {
            freeIndices.erase(it);
        }
    } else {
        // Beyond the allocated range: advance the counter to it, banking every index
        // we skip so they can be handed out later.
        //
        // Guard the gap. Banking one entry per skipped index turns a single call with
        // an absurd id into a million push_backs plus an O(n) std::find over the free
        // list on every later call. The index space is now capped at 2^20, so this can
        // no longer OOM the way a 32-bit id gap could — but the quadratic free list is
        // still real, and a legitimate restore index is always within a handful of
        // nextIndex (indices are dense and sequential). Skip past a gap this large
        // without banking; the leaped indices become permanently unused (they were
        // never live entities), which is a bounded, correct outcome. The requested id
        // is still honored so the restore/prefab path gets the entity it asked for.
        constexpr Entity kMaxBankGap = 1u << 16; // 65 536 indices
        if ((index - nextIndex) > kMaxBankGap) {
            nextIndex = index + 1;
        } else {
            while (nextIndex < index) {
                freeIndices.push_back(nextIndex++);
            }
            nextIndex = index + 1;
        }
        slots.resize(index + 1);
    }

    Slot& slot = slots[index];
    slot.generation = generation; // resurrect the exact identity, generation included
    slot.alive = true;
    return makeEntity(index, generation);
}

void EntityManager::destroyEntity(Entity entity) {
    if (entity == INVALID_ENTITY) {
        return;
    }

    const Entity index = entityIndex(entity);
    if (index >= slots.size()) {
        return;
    }

    Slot& slot = slots[index];
    if (!slot.alive || slot.generation != entityGeneration(entity)) {
        // A stale handle or a double destroy. The bare counter could not tell either
        // from a real destruction and pushed the index onto the free list anyway,
        // which handed the same index out twice. Ignoring it is the fix.
        return;
    }

    slot.alive = false;
    freeIndices.push_back(index);
}

bool EntityManager::isAlive(Entity entity) const {
    if (entity == INVALID_ENTITY) {
        return false;
    }
    const Entity index = entityIndex(entity);
    if (index >= slots.size()) {
        return false;
    }
    const Slot& slot = slots[index];
    return slot.alive && slot.generation == entityGeneration(entity);
}

void EntityManager::reset() {
    // A full wipe, generations included: reset() is a world teardown (scene load, test
    // setup), and loading the same scene twice must produce the same handles or every
    // run-to-run comparison keyed on an entity id stops reproducing (Rule 10).
    // Carrying generations across a reset would buy stale-handle detection for the one
    // case of a handle held across a scene load, at the cost of determinism everywhere
    // — the wrong trade for this engine. See DESIGN_GENERATIONAL_IDS.md.
    nextIndex = 1;
    slots.assign(1, Slot{});
    freeIndices.clear();
}
