#pragma once

#include <cstdint>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum class CameraMode : uint8_t {
    FREE,
    ORBIT,
    FOLLOW
};

struct Camera {
    CameraMode mode = CameraMode::FREE;
    glm::vec3 position = {0.0f, 0.0f, 2.5f};
    glm::vec3 worldUp = {0.0f, 1.0f, 0.0f};
    glm::vec3 target = {0.0f, 0.0f, 0.0f};
    glm::vec3 followTargetPosition = {0.0f, 0.0f, 0.0f};
    float distance = 5.0f;
    bool hasFollowTarget = false;

    float yaw = -90.0f;
    float pitch = 0.0f;

    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float speed = 2.5f;
    float mouseSensitivity = 0.10f;

    glm::vec3 getForward() const {
        glm::vec3 front;
        front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        front.y = sin(glm::radians(pitch));
        front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        return glm::normalize(front);
    }

    glm::vec3 getRight() const {
        return glm::normalize(glm::cross(getForward(), worldUp));
    }

    void moveForward(float deltaTime) {
        if (mode != CameraMode::FREE) {
            return;
        }

        position += getForward() * speed * deltaTime;
    }

    void moveBackward(float deltaTime) {
        if (mode != CameraMode::FREE) {
            return;
        }

        position -= getForward() * speed * deltaTime;
    }

    void moveLeft(float deltaTime) {
        if (mode != CameraMode::FREE) {
            return;
        }

        position -= getRight() * speed * deltaTime;
    }

    void moveRight(float deltaTime) {
        if (mode != CameraMode::FREE) {
            return;
        }

        position += getRight() * speed * deltaTime;
    }

    void rotate(float xOffset, float yOffset) {
        if (mode == CameraMode::FOLLOW) {
            return;
        }

        yaw += xOffset * mouseSensitivity;
        pitch += yOffset * mouseSensitivity;

        pitch = glm::clamp(pitch, -89.0f, 89.0f);
    }

    glm::mat4 getViewMatrix() {
        if (mode == CameraMode::FREE) {
            return glm::lookAt(position, position + getForward(), worldUp);
        }

        if (mode == CameraMode::ORBIT) {
            glm::vec3 offset;
            offset.x = distance * cos(glm::radians(pitch)) * sin(glm::radians(yaw));
            offset.y = distance * sin(glm::radians(pitch));
            offset.z = distance * cos(glm::radians(pitch)) * cos(glm::radians(yaw));

            position = target + offset;
            return glm::lookAt(position, target, worldUp);
        }

        if (mode == CameraMode::FOLLOW && hasFollowTarget) {
            glm::vec3 targetPosition = followTargetPosition;
            glm::vec3 offset = {0.0f, 2.0f, -5.0f};

            position = targetPosition + offset;
            return glm::lookAt(position, targetPosition, worldUp);
        }

        return glm::mat4(1.0f);
    }

    glm::mat4 getProjectionMatrix(float aspectRatio) const {
        glm::mat4 projection = glm::perspective(
            glm::radians(fov),
            aspectRatio,
            nearPlane,
            farPlane
        );

        projection[1][1] *= -1.0f;
        return projection;
    }
};
