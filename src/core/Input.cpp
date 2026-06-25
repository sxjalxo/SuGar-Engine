#include "core/Input.h"

std::unordered_map<int, bool> Input::keys;
std::unordered_map<int, bool> Input::pressedKeys;
glm::vec2 Input::lastMousePos = {0.0f, 0.0f};
glm::vec2 Input::mouseDelta = {0.0f, 0.0f};
bool Input::firstMouse = true;

void Input::init() {
    keys.clear();
    pressedKeys.clear();
    lastMousePos = {0.0f, 0.0f};
    mouseDelta = {0.0f, 0.0f};
    firstMouse = true;
}

void Input::beginFrame() {
    pressedKeys.clear();
    mouseDelta = {0.0f, 0.0f};
}

void Input::setKey(int key, bool pressed) {
    const bool wasPressed = isKeyDown(key);
    keys[key] = pressed;

    if (pressed && !wasPressed) {
        pressedKeys[key] = true;
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
