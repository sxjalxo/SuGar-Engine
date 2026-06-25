#pragma once

#include <memory>
#include <algorithm>
#include <string>
#include <stdexcept>
#include <vector>
#include "scene/Transform.h"

struct Material;
class Mesh;

struct GameObject {
    std::string name;
    Transform transform;
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    GameObject* parent = nullptr;
    std::vector<GameObject*> children;

    void addChild(GameObject* child) {
        if (child == nullptr) {
            throw std::invalid_argument("cannot add a null child to a game object");
        }

        if (child == this) {
            throw std::logic_error("a game object cannot be parented to itself");
        }

        for (GameObject* ancestor = this; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor == child) {
                throw std::logic_error("game object hierarchy cannot contain cycles");
            }
        }

        if (child->parent == this) {
            return;
        }

        if (child->parent != nullptr) {
            auto& siblings = child->parent->children;
            siblings.erase(
                std::remove(siblings.begin(), siblings.end(), child),
                siblings.end()
            );
        }

        child->parent = this;
        children.push_back(child);
    }
};
