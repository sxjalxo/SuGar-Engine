#pragma once

#include <string>
#include <vector>

// Phase 16A — Runtime UI model layer. Authoritative UI state lives in ECS
// components (never inside RmlUi), so the rendered UI is a pure function of ECS +
// input and therefore survives snapshot restore / time travel / hot reload for
// free. See docs/DESIGN_RUNTIME_UI.md and RULES.md Rule 21.
//
// Screens and focused elements are identified by name (std::string), mirroring the
// behavior-name pattern: serializable, extensible, no compiled-in enum to keep in
// sync with content.

using ScreenId = std::string;
using ElementId = std::string;

// The navigation stack of active UI screens. The top of the stack (back()) is the
// screen currently in front; pushing opens a screen (e.g. Inventory over HUD),
// popping goes back. This is authoritative: a player relies on it, and a snapshot
// restore that lost it would show the wrong screen. Typically held by one singleton
// "UIRoot" entity.
struct UIScreenComponent {
    std::vector<ScreenId> screenStack;

    ScreenId active() const { return screenStack.empty() ? ScreenId{} : screenStack.back(); }
};

// The element currently focused by keyboard / gamepad navigation. Authoritative:
// the player navigated here and it is NOT derivable from cursor position (mouse
// hover, by contrast, is derived and lives in the UI view, not here).
struct FocusComponent {
    ElementId focusedElement;
};
