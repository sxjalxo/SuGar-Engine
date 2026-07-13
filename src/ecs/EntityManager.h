#pragma once

#include <vector>
#include "ecs/Entity.h"

class EntityManager {
public:
    Entity createEntity();

    // Recreates a specific entity id (Phase 14B). Used when a destroyed subtree is
    // restored (delete-undo / duplicate-redo) so it comes back with its *original*
    // ids — which is what lets editor commands keep raw ids without a remap layer.
    // The id must be free (previously destroyed, or beyond the allocated range);
    // any lower ids skipped to reach it are banked on the free list.
    Entity createEntityWithId(Entity id);

    void destroyEntity(Entity entity);
    void reset();

private:
    Entity nextEntity = 1;
    std::vector<Entity> freeList;
};
