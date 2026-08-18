#pragma once

#include <cstdint>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum class CameraMode : uint8_t {
    FREE,
    ORBIT,
    FOLLOW,
    // Driven by a game's CameraComponent: position + look direction come straight
    // from an entity's world transform each frame (first-person, chase rigs, etc).
    // The engine's own input never moves it — the game owns the pose.
    SCRIPTED
};

struct Camera {
    CameraMode mode = CameraMode::FREE;
    glm::vec3 position = {0.0f, 0.0f, 2.5f};
    glm::vec3 worldUp = {0.0f, 1.0f, 0.0f};
    glm::vec3 target = {0.0f, 0.0f, 0.0f};
    glm::vec3 followTargetPosition = {0.0f, 0.0f, 0.0f};
    // SCRIPTED mode: the eye's look direction and up, set from the game camera
    // entity's world transform each frame.
    glm::vec3 scriptedForward = {0.0f, 0.0f, -1.0f};
    glm::vec3 scriptedUp = {0.0f, 1.0f, 0.0f};
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
        if (mode == CameraMode::FOLLOW || mode == CameraMode::SCRIPTED) {
            return; // the game (or the follow target) owns the orientation
        }

        yaw += xOffset * mouseSensitivity;
        pitch += yOffset * mouseSensitivity;

        pitch = glm::clamp(pitch, -89.0f, 89.0f);
    }

    glm::mat4 getViewMatrix() {
        if (mode == CameraMode::FREE) {
            return glm::lookAt(position, position + getForward(), worldUp);
        }

        if (mode == CameraMode::SCRIPTED) {
            return glm::lookAt(position, position + scriptedForward, scriptedUp);
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
        // **_ZO, not glm::perspective.** GLM defaults to OpenGL's clip space, where NDC z
        // runs -1..1; Vulkan clips z to 0..1, so half of an OpenGL-convention frustum's
        // depth range is thrown away by the rasterizer. For a perspective camera the lost
        // half sits between the near plane and ~2*near, which is why nothing noticed --
        // but the same default applied to the shadow map's ORTHOGRAPHIC projection
        // discards the near half of the whole scene, and that half of the floor then
        // renders unshadowed with a hard straight edge across it (found by the L3 arena).
        // Every projection this engine builds states its depth convention explicitly.
        glm::mat4 projection = glm::perspectiveRH_ZO(
            glm::radians(fov),
            aspectRatio,
            nearPlane,
            farPlane
        );

        projection[1][1] *= -1.0f; // Vulkan's Y points down
        return projection;
    }
};
