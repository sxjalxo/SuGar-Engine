#pragma once

#include <vector>
#include "ecs/Entity.h"

class EntityManager {
public:
    Entity createEntity();
    void destroyEntity(Entity entity);
    void reset();

private:
    Entity nextEntity = 1;
    std::vector<Entity> freeList;
};
