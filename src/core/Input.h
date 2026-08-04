#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <unordered_map>

// Cursor as a world-space ray for the current frame. The ray is the fundamental
// thing (camera-convention-independent); a world *point* is derived from it against
// whatever plane the game cares about — see Input::getMouseWorldOnPlane. Storing the
// ray, not a point, keeps Input independent of which plane is "the ground" (Rule 21b:
// the point is recomputable from the ray, so it isn't state).
struct MouseRay {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

class Input {
public:
    static void init();
    static void beginFrame();

    static void setKey(int key, bool pressed);
    static bool isKeyDown(int key);
    static bool isKeyPressed(int key);

    static void setMousePosition(double x, double y);
    static glm::vec2 getMouseDelta();

    // Absolute cursor position, in the window/pixel space GLFW reports.
    static glm::vec2 getMousePosition();

    // Mouse buttons mirror the key state (held vs pressed-this-frame). GLFW button
    // codes: 0 left, 1 right, 2 middle.
    static void setMouseButton(int button, bool pressed);
    static bool isMouseButtonDown(int button);
    static bool isMouseButtonPressed(int button);

    // Cursor ray in world space for the current frame. Core only stores it; the
    // renderer (which owns the camera + viewport) computes and writes it each frame,
    // the same Core-stores/Engine-populates split as SaveData and the UI labels.
    // Behaviours read it to aim/pick without ever touching the camera.
    static void setMouseRay(const MouseRay& ray);
    static MouseRay getMouseRay();

    // Intersect the current cursor ray with an arbitrary plane (point + normal) and
    // write the world hit to `outPoint`. Returns false when the ray is parallel to the
    // plane (or points away). Pure math, no camera/GLFW — a Z=0 game passes
    // ({0,0,0},{0,0,1}); a Y=0 game passes ({0,0,0},{0,1,0}); any plane works.
    static bool getMouseWorldOnPlane(const glm::vec3& planePoint,
                                     const glm::vec3& planeNormal,
                                     glm::vec3& outPoint);

    // Text typed this frame (UTF-8), from the character callback. Cleared each
    // beginFrame, so it is strictly "what was typed since the last frame".
    static void pushText(const std::string& utf8);
    static const std::string& textThisFrame();

private:
    static std::unordered_map<int, bool> keys;
    static std::unordered_map<int, bool> pressedKeys;
    static std::unordered_map<int, bool> mouseButtons;
    static std::unordered_map<int, bool> pressedMouseButtons;
    static glm::vec2 lastMousePos;
    static glm::vec2 mouseDelta;
    static MouseRay mouseRay;
    static bool firstMouse;
    static std::string frameText;
};
