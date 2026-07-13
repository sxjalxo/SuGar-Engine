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
