#include "ecs/EntityManager.h"

Entity EntityManager::createEntity() {
    if (!freeList.empty()) {
        Entity entity = freeList.back();
        freeList.pop_back();
        return entity;
    }

    return nextEntity++;
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
