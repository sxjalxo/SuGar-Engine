#pragma once

#include <glm/vec2.hpp>
#include <unordered_map>

class Input {
public:
    static void init();
    static void beginFrame();

    static void setKey(int key, bool pressed);
    static bool isKeyDown(int key);
    static bool isKeyPressed(int key);

    static void setMousePosition(double x, double y);
    static glm::vec2 getMouseDelta();

private:
    static std::unordered_map<int, bool> keys;
    static std::unordered_map<int, bool> pressedKeys;
    static glm::vec2 lastMousePos;
    static glm::vec2 mouseDelta;
    static bool firstMouse;
};
