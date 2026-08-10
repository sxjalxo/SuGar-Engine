#include "ecs/EntityManager.h"

#include <algorithm>

Entity EntityManager::createEntity() {
    if (!freeList.empty()) {
        Entity entity = freeList.back();
        freeList.pop_back();
        return entity;
    }

    return nextEntity++;
}

Entity EntityManager::createEntityWithId(Entity id) {
    if (id == INVALID_ENTITY) {
        return INVALID_ENTITY;
    }

    // Claim it from the free list if it was previously destroyed.
    const auto it = std::find(freeList.begin(), freeList.end(), id);
    if (it != freeList.end()) {
        freeList.erase(it);
        return id;
    }

    // Otherwise it must be beyond the allocated range: advance the counter to it,
    // banking every id we skip so they can be handed out later.
    //
    // Guard the gap: banking one entry per skipped id turns a single call with an
    // absurd id (a corrupt undo record or hostile prefab-with-ids list) into an OOM
    // — millions of push_backs — plus an O(n) std::find over the free list on every
    // later call. A legitimate restore id is always within a handful of nextEntity
    // (ids are dense and sequential), so a gap this large is never real. Skip past
    // it without banking; the leaped ids become permanently unused (they were never
    // live entities), which is a bounded, correct outcome. The requested id is still
    // honored so the restore/prefab path gets the entity it asked for.
    constexpr Entity kMaxBankGap = 1u << 20; // ~1M ids
    if (id > nextEntity && (id - nextEntity) > kMaxBankGap) {
        nextEntity = id + 1;
        return id;
    }
    while (nextEntity <= id) {
        const Entity skipped = nextEntity++;
        if (skipped != id) {
            freeList.push_back(skipped);
        }
    }
    return id;
}

void EntityManager::destroyEntity(Entity entity) {
    if (entity == INVALID_ENTITY) {
        return;
    }

    freeList.push_back(entity);
}

void EntityManager::reset() {
    nextEntity = 1;
    freeList.clear();
}
