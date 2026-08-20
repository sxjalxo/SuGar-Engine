#include "core/Input.h"

#include <cmath>
#include <glm/geometric.hpp>

std::unordered_map<int, bool> Input::keys;
std::unordered_map<int, bool> Input::pressedKeys;
std::unordered_map<int, bool> Input::stepPressedKeys;
std::unordered_map<int, bool> Input::mouseButtons;
std::unordered_map<int, bool> Input::pressedMouseButtons;
std::unordered_map<int, bool> Input::stepPressedMouseButtons;
glm::vec2 Input::lastMousePos = {0.0f, 0.0f};
glm::vec2 Input::mouseDelta = {0.0f, 0.0f};
MouseRay Input::mouseRay;
bool Input::firstMouse = true;
bool Input::cursorCaptureRequested = false;
std::string Input::frameText;

void Input::init() {
    keys.clear();
    pressedKeys.clear();
    stepPressedKeys.clear();
    mouseButtons.clear();
    pressedMouseButtons.clear();
    stepPressedMouseButtons.clear();
    frameText.clear();
    lastMousePos = {0.0f, 0.0f};
    mouseDelta = {0.0f, 0.0f};
    mouseRay = MouseRay{};
    firstMouse = true;
}

void Input::endFixedStep() {
    // Cleared at the END of the step, not the start: the step that runs must be able to
    // see an edge latched since the last one. Clearing here gives both guarantees at
    // once -- a frame that runs no step leaves the latch standing (no press is lost),
    // and a frame that runs several has it cleared by the first (no press is doubled).
    stepPressedKeys.clear();
    stepPressedMouseButtons.clear();
}

void Input::beginFrame() {
    pressedKeys.clear();
    pressedMouseButtons.clear();
    mouseDelta = {0.0f, 0.0f};
    frameText.clear();
}

void Input::pushText(const std::string& utf8) {
    frameText += utf8;
}

const std::string& Input::textThisFrame() {
    return frameText;
}

void Input::setKey(int key, bool pressed) {
    const bool wasPressed = isKeyDown(key);
    keys[key] = pressed;

    if (pressed && !wasPressed) {
        pressedKeys[key] = true;
        // The step-domain latch is NOT cleared by beginFrame. It stands until a fixed
        // step consumes it, so a press that lands in a frame running no step survives.
        stepPressedKeys[key] = true;
    }
}

bool Input::isKeyDown(int key) {
    auto it = keys.find(key);
    return it != keys.end() && it->second;
}

bool Input::isKeyPressed(int key) {
    auto it = pressedKeys.find(key);
    return it != pressedKeys.end() && it->second;
}

bool Input::isKeyPressedThisStep(int key) {
    auto it = stepPressedKeys.find(key);
    return it != stepPressedKeys.end() && it->second;
}

void Input::setMousePosition(double x, double y) {
    const glm::vec2 current = {
        static_cast<float>(x),
        static_cast<float>(y)
    };

    if (firstMouse) {
        lastMousePos = current;
        firstMouse = false;
        return;
    }

    mouseDelta += current - lastMousePos;
    lastMousePos = current;
}

glm::vec2 Input::getMouseDelta() {
    return mouseDelta;
}

glm::vec2 Input::getMousePosition() {
    return lastMousePos;
}

void Input::setMouseButton(int button, bool pressed) {
    const bool wasPressed = isMouseButtonDown(button);
    mouseButtons[button] = pressed;

    if (pressed && !wasPressed) {
        pressedMouseButtons[button] = true;
        stepPressedMouseButtons[button] = true;
    }
}

bool Input::isMouseButtonDown(int button) {
    auto it = mouseButtons.find(button);
    return it != mouseButtons.end() && it->second;
}

bool Input::isMouseButtonPressed(int button) {
    auto it = pressedMouseButtons.find(button);
    return it != pressedMouseButtons.end() && it->second;
}

bool Input::isMouseButtonPressedThisStep(int button) {
    auto it = stepPressedMouseButtons.find(button);
    return it != stepPressedMouseButtons.end() && it->second;
}

void Input::setMouseRay(const MouseRay& ray) {
    mouseRay = ray;
}

MouseRay Input::getMouseRay() {
    return mouseRay;
}

void Input::setCursorCaptured(bool captured) {
    cursorCaptureRequested = captured;
}

bool Input::cursorCaptured() {
    return cursorCaptureRequested;
}

bool Input::getMouseWorldOnPlane(const glm::vec3& planePoint,
                                 const glm::vec3& planeNormal,
                                 glm::vec3& outPoint) {
    const float denom = glm::dot(planeNormal, mouseRay.direction);
    if (std::abs(denom) < 1e-6f) {
        return false; // ray parallel to the plane
    }
    const float t = glm::dot(planeNormal, planePoint - mouseRay.origin) / denom;
    if (t < 0.0f) {
        return false; // plane is behind the ray origin
    }
    outPoint = mouseRay.origin + mouseRay.direction * t;
    return true;
}
