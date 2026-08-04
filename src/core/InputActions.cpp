#include "core/InputActions.h"
#include "core/Input.h"

#include <algorithm>
#include <GLFW/glfw3.h>

namespace {
// One flat code space: a mouse code (>= MouseButtonBase) routes to the mouse-button
// state, anything else to the keyboard. Callers (isActionDown/Pressed, getAxis) never
// branch on the source.
bool codeDown(int code) {
    return code >= InputActions::MouseButtonBase
               ? Input::isMouseButtonDown(code - InputActions::MouseButtonBase)
               : Input::isKeyDown(code);
}
bool codePressed(int code) {
    return code >= InputActions::MouseButtonBase
               ? Input::isMouseButtonPressed(code - InputActions::MouseButtonBase)
               : Input::isKeyPressed(code);
}
} // namespace

std::unordered_map<std::string, std::vector<int>>& InputActions::actionTable() {
    static std::unordered_map<std::string, std::vector<int>> instance;
    return instance;
}

std::unordered_map<std::string, InputActions::AxisBinding>& InputActions::axisTable() {
    static std::unordered_map<std::string, AxisBinding> instance;
    return instance;
}

void InputActions::bindAction(const std::string& action, int key) {
    auto& keys = actionTable()[action];
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
        keys.push_back(key);
    }
}

void InputActions::bindAxis(const std::string& axis, int positiveKey, int negativeKey) {
    AxisBinding& binding = axisTable()[axis];
    if (std::find(binding.positiveKeys.begin(), binding.positiveKeys.end(), positiveKey) == binding.positiveKeys.end()) {
        binding.positiveKeys.push_back(positiveKey);
    }
    if (std::find(binding.negativeKeys.begin(), binding.negativeKeys.end(), negativeKey) == binding.negativeKeys.end()) {
        binding.negativeKeys.push_back(negativeKey);
    }
}

bool InputActions::isActionDown(const std::string& action) {
    const auto it = actionTable().find(action);
    if (it == actionTable().end()) {
        return false;
    }
    for (int key : it->second) {
        if (codeDown(key)) {
            return true;
        }
    }
    return false;
}

bool InputActions::isActionPressed(const std::string& action) {
    const auto it = actionTable().find(action);
    if (it == actionTable().end()) {
        return false;
    }
    for (int key : it->second) {
        if (codePressed(key)) {
            return true;
        }
    }
    return false;
}

float InputActions::getAxis(const std::string& axis) {
    const auto it = axisTable().find(axis);
    if (it == axisTable().end()) {
        return 0.0f;
    }

    float value = 0.0f;
    for (int key : it->second.positiveKeys) {
        if (codeDown(key)) {
            value += 1.0f;
            break;
        }
    }
    for (int key : it->second.negativeKeys) {
        if (codeDown(key)) {
            value -= 1.0f;
            break;
        }
    }
    return value;
}

void InputActions::registerDefaults() {
    // Player movement on the arrow keys (camera uses WASD, so they don't clash).
    bindAxis("MoveForward", GLFW_KEY_UP, GLFW_KEY_DOWN);
    bindAxis("MoveRight", GLFW_KEY_RIGHT, GLFW_KEY_LEFT);
    bindAction("Jump", GLFW_KEY_SPACE);
}

void InputActions::clear() {
    actionTable().clear();
    axisTable().clear();
}
