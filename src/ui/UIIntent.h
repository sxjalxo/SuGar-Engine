#pragma once

#include <string>
#include <vector>

#include "ui/UIComponents.h"

// Phase 16A — the render-rate → fixed-step boundary for UI. UI event handlers (and,
// later, RmlUi callbacks) never mutate UI state directly; they enqueue a UIIntent.
// The RuntimeUISystem drains the queue on the fixed step and applies the changes to
// ECS, so authoritative UI-state mutations stay deterministic (the same reason raw
// input is sampled into the fixed step). See docs/DESIGN_RUNTIME_UI.md.

struct UIIntent {
    enum class Type {
        OpenScreen,   // push `arg` onto the screen stack
        PopScreen,    // pop the top screen (back navigation); root is not poppable
        SetFocus,     // set keyboard/gamepad focus to `arg`
        ClearFocus,   // clear focus
        AppendText,   // insert `arg` at the caret of the text buffer
        BackspaceText // delete the character before the caret
    };

    Type type;
    std::string arg; // screen id, element id, or text; unused for Pop/Clear/Backspace

    static UIIntent openScreen(ScreenId id) { return { Type::OpenScreen, std::move(id) }; }
    static UIIntent popScreen() { return { Type::PopScreen, {} }; }
    static UIIntent setFocus(ElementId id) { return { Type::SetFocus, std::move(id) }; }
    static UIIntent clearFocus() { return { Type::ClearFocus, {} }; }
    static UIIntent appendText(std::string text) { return { Type::AppendText, std::move(text) }; }
    static UIIntent backspaceText() { return { Type::BackspaceText, {} }; }
};

// A queue of pending UI intents, filled at render rate and drained by the
// RuntimeUISystem on the fixed step. Owns no ECS state itself — it's a transient
// mailbox, deliberately not serialized (in-flight intents are not authoritative;
// they resolve within one step). Mirrors PhysicsWorld's collision-event buffer.
class UIIntentQueue {
public:
    void push(const UIIntent& intent) { intents.push_back(intent); }
    const std::vector<UIIntent>& pending() const { return intents; }
    bool empty() const { return intents.empty(); }
    void clear() { intents.clear(); }

private:
    std::vector<UIIntent> intents;
};
