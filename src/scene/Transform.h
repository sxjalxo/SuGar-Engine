#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // identity (w, x, y, z)
    glm::vec3 scale{1.0f};

    glm::mat4 getLocalMatrix() const {
        glm::mat4 matrix = glm::translate(glm::mat4(1.0f), position);
        matrix *= glm::mat4_cast(rotation);
        matrix = glm::scale(matrix, scale);

        return matrix;
    }
};
