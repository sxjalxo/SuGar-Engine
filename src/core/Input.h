#pragma once

#include <glm/vec2.hpp>
#include <string>
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

    // Text typed this frame (UTF-8), from the character callback. Cleared each
    // beginFrame, so it is strictly "what was typed since the last frame".
    static void pushText(const std::string& utf8);
    static const std::string& textThisFrame();

private:
    static std::unordered_map<int, bool> keys;
    static std::unordered_map<int, bool> pressedKeys;
    static glm::vec2 lastMousePos;
    static glm::vec2 mouseDelta;
    static bool firstMouse;
    static std::string frameText;
};
