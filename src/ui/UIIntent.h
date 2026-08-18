#pragma once

#include <string>
#include <vector>

#include "ui/UIComponents.h"

// Phase 16A — the render-rate → fixed-step boundary for UI. UI event handlers (and,
// later, RmlUi callbacks) never mutate UI state directly; they enqueue a UIIntent.
// The RuntimeUISystem drains the queue on the fixed step and applies the changes to
// ECS, so authoritative UI-state mutations stay deterministic (the same reason raw
// input is sampled into the fixed step). See DevDocs/DESIGN_RUNTIME_UI.md.

struct UIIntent {
    enum class Type {
        OpenScreen,   // push `arg` onto the screen stack
        PopScreen,    // pop the top screen (back navigation); root is not poppable
        SetFocus,     // set keyboard/gamepad focus to `arg`
        ClearFocus,   // clear focus
        AppendText,   // insert `arg` at the caret of the focused text field
        BackspaceText,// delete the character before the caret
        CaretLeft,    // move the caret one character left
        CaretRight    // move the caret one character right
    };

    Type type;
    std::string arg; // screen id, element id, or text; unused for Pop/Clear/Backspace

    static UIIntent openScreen(ScreenId id) { return { Type::OpenScreen, std::move(id) }; }
    static UIIntent popScreen() { return { Type::PopScreen, {} }; }
    static UIIntent setFocus(ElementId id) { return { Type::SetFocus, std::move(id) }; }
    static UIIntent clearFocus() { return { Type::ClearFocus, {} }; }
    static UIIntent appendText(std::string text) { return { Type::AppendText, std::move(text) }; }
    static UIIntent backspaceText() { return { Type::BackspaceText, {} }; }
    static UIIntent caretLeft() { return { Type::CaretLeft, {} }; }
    static UIIntent caretRight() { return { Type::CaretRight, {} }; }
};

// Parses a document's `data-intent` attribute into an intent
// (DevDocs/DESIGN_UI_INTENT_BINDING.md). Content declares what a button does, in the
// engine's own vocabulary, instead of the engine compiling in a list of element ids —
// which is what made the interactive half of the runtime UI unreachable by any game.
//
// Pure string handling and Core-side on purpose: the parse is the part that can be
// silently wrong, and this way it is testable with no RmlUi and no device.
//
//   open:<screen>  pop  focus:<element>  unfocus
//   text:<string>  backspace  caretleft  caretright
//
// Returns false for an unknown verb or a missing argument, leaving `out` untouched:
// a typo in markup must produce a REPORTED dead button, never a wrong action.
inline bool parseUIIntent(const std::string& spec, UIIntent& out) {
    const size_t colon = spec.find(':');
    const std::string verb = spec.substr(0, colon);
    const bool hasArgument = colon != std::string::npos;
    // Everything after the first colon, verbatim — a label may contain colons.
    const std::string argument = hasArgument ? spec.substr(colon + 1) : std::string();

    if (verb == "pop" && !hasArgument)        { out = UIIntent::popScreen();     return true; }
    if (verb == "unfocus" && !hasArgument)    { out = UIIntent::clearFocus();    return true; }
    if (verb == "backspace" && !hasArgument)  { out = UIIntent::backspaceText(); return true; }
    if (verb == "caretleft" && !hasArgument)  { out = UIIntent::caretLeft();     return true; }
    if (verb == "caretright" && !hasArgument) { out = UIIntent::caretRight();    return true; }

    // The argument-taking forms. An empty argument is a typo, not an empty screen id.
    if (!hasArgument || argument.empty()) {
        return false;
    }
    if (verb == "open")  { out = UIIntent::openScreen(argument); return true; }
    if (verb == "focus") { out = UIIntent::setFocus(argument);   return true; }
    if (verb == "text")  { out = UIIntent::appendText(argument); return true; }
    return false;
}

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
