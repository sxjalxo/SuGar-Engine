#pragma once

#include <deque>
#include <glm/glm.hpp>
#include <vector>
#include "scene/GameObject.h"
#include "scene/Light.h"

inline glm::mat4 getWorldMatrix(const GameObject* object) {
    if (object == nullptr) {
        return glm::mat4(1.0f);
    }

    if (object->parent != nullptr) {
        return getWorldMatrix(object->parent) * object->transform.getLocalMatrix();
    }

    return object->transform.getLocalMatrix();
}

class Scene {
public:
    GameObject& createObject() {
        objects.emplace_back();
        return objects.back();
    }

    std::deque<GameObject> objects;
    std::vector<Light> lights;
};
