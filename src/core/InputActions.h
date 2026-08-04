#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// Gameplay-facing input layer over raw Input. Behaviors query named actions and
// axes ("Jump", "MoveForward") instead of hardcoding GLFW key codes, so bindings
// can be remapped without touching gameplay logic. Reads the static Input state;
// no per-frame bookkeeping of its own.
class InputActions {
public:
    // Input codes are one flat space so an action never cares where it came from:
    // keyboard keys occupy the GLFW range (< MouseButtonBase); mouse buttons are
    // offset above it. bindAction("Fire", MouseLeft) and bindAction("Fire", Space)
    // then compose with no special-casing. GLFW_KEY_LAST is 348, so 512 leaves room.
    static constexpr int MouseButtonBase = 512;
    static constexpr int MouseLeft = MouseButtonBase + 0;   // GLFW_MOUSE_BUTTON_LEFT
    static constexpr int MouseRight = MouseButtonBase + 1;  // GLFW_MOUSE_BUTTON_RIGHT
    static constexpr int MouseMiddle = MouseButtonBase + 2; // GLFW_MOUSE_BUTTON_MIDDLE

    // Bind an additional key (or mouse button, via the codes above) to a digital
    // action (held / pressed-this-frame).
    static void bindAction(const std::string& action, int key);

    // Bind an analog axis: positiveKey contributes +1, negativeKey contributes -1.
    static void bindAxis(const std::string& axis, int positiveKey, int negativeKey);

    static bool isActionDown(const std::string& action);
    static bool isActionPressed(const std::string& action);

    // Returns the axis value in [-1, 1] (positive minus negative contribution).
    static float getAxis(const std::string& axis);

    // Installs the engine's default bindings (arrow keys for player movement,
    // Space for Jump). Idempotent; safe to call more than once.
    static void registerDefaults();

    static void clear();

private:
    struct AxisBinding {
        std::vector<int> positiveKeys;
        std::vector<int> negativeKeys;
    };

    static std::unordered_map<std::string, std::vector<int>>& actionTable();
    static std::unordered_map<std::string, AxisBinding>& axisTable();
};
