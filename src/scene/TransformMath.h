#pragma once

// Euler <-> quaternion conversions. The engine stores rotations as quaternions
// (matching glTF and avoiding gimbal lock), but two places still speak Euler:
// the editor inspector (humans edit XYZ angles) and legacy v2 scenes whose "rot"
// was a 3-component Euler vector. Both directions preserve Transform's historical
// rotate order (Rx * Ry * Rz) so old data and old behavior round-trip exactly.

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>

// Euler angles in radians, applied X then Y then Z (matches the old Transform).
inline glm::quat quatFromEulerXYZ(const glm::vec3& eulerRadians) {
    return glm::angleAxis(eulerRadians.x, glm::vec3(1.0f, 0.0f, 0.0f)) *
           glm::angleAxis(eulerRadians.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
           glm::angleAxis(eulerRadians.z, glm::vec3(0.0f, 0.0f, 1.0f));
}

// Inverse of quatFromEulerXYZ; returns radians. extractEulerAngleXYZ inverts the
// Rx * Ry * Rz matrix, so this is the exact counterpart of the constructor above.
inline glm::vec3 eulerXyzFromQuat(const glm::quat& q) {
    glm::vec3 eulerRadians(0.0f);
    glm::extractEulerAngleXYZ(glm::mat4_cast(q), eulerRadians.x, eulerRadians.y, eulerRadians.z);
    return eulerRadians;
}

// Decomposes an affine matrix into translation / rotation / scale (drops skew +
// perspective). Used to turn a gizmo-manipulated world matrix back into a
// Transform. Keeps the experimental glm dependency confined to this header.
inline bool decomposeMatrix(const glm::mat4& matrix, glm::vec3& translation,
                            glm::quat& rotation, glm::vec3& scale) {
    glm::vec3 skew(0.0f);
    glm::vec4 perspective(0.0f);
    return glm::decompose(matrix, scale, rotation, translation, skew, perspective);
}
