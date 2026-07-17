#include "ui/RuntimeUISystem.h"

#include "ecs/Registry.h"
#include "ui/UIIntent.h"

#include <algorithm>

namespace {

size_t clampCaret(const TextInputComponent& text) {
    return std::min(static_cast<size_t>(std::max(text.caret, 0)), text.buffer.size());
}

// Text goes to the field whose element matches the focused element. Routing is
// decided from ECS (FocusComponent + TextInputComponent::element) — never by asking
// RmlUi which widget it thinks has the caret. Returns null when nothing is focused,
// so typing with no field focused is simply ignored.
TextInputComponent* focusedTextField(Registry& registry) {
    std::string focused;
    for (const auto& [entity, focus] : registry.focus.getAll()) {
        (void)entity;
        focused = focus.focusedElement;
        break;
    }
    if (focused.empty()) {
        return nullptr;
    }
    for (auto& [entity, text] : registry.textInputs.getAll()) {
        (void)entity;
        if (text.element == focused) {
            return &text;
        }
    }
    return nullptr;
}

} // namespace

namespace RuntimeUISystem {

void update(Registry& registry, UIIntentQueue& intents) {
    // Apply intents in queue order (deterministic). Screen intents mutate every
    // UIScreenComponent, focus intents every FocusComponent — in practice these
    // live on the single UIRoot entity, but nothing here assumes exactly one.
    for (const UIIntent& intent : intents.pending()) {
        switch (intent.type) {
            case UIIntent::Type::OpenScreen:
                for (auto& [entity, screen] : registry.uiScreens.getAll()) {
                    (void)entity;
                    screen.screenStack.push_back(intent.arg);
                }
                break;

            case UIIntent::Type::PopScreen:
                for (auto& [entity, screen] : registry.uiScreens.getAll()) {
                    (void)entity;
                    // The root screen (the HUD) is not poppable: backing out of it
                    // would leave the game with no UI at all, which is never what
                    // "back" means. Only screens opened on top of it can close.
                    if (screen.screenStack.size() > 1) {
                        screen.screenStack.pop_back();
                    }
                }
                break;

            case UIIntent::Type::SetFocus:
                for (auto& [entity, focus] : registry.focus.getAll()) {
                    (void)entity;
                    focus.focusedElement = intent.arg;
                }
                break;

            case UIIntent::Type::ClearFocus:
                for (auto& [entity, focus] : registry.focus.getAll()) {
                    (void)entity;
                    focus.focusedElement.clear();
                }
                break;

            case UIIntent::Type::AppendText:
                if (TextInputComponent* text = focusedTextField(registry)) {
                    const size_t caret = clampCaret(*text);
                    text->buffer.insert(caret, intent.arg);
                    text->caret = static_cast<int>(caret + intent.arg.size());
                }
                break;

            case UIIntent::Type::BackspaceText:
                if (TextInputComponent* text = focusedTextField(registry)) {
                    const size_t caret = clampCaret(*text);
                    if (caret > 0) {
                        text->buffer.erase(caret - 1, 1);
                        text->caret = static_cast<int>(caret - 1);
                    }
                }
                break;

            case UIIntent::Type::CaretLeft:
                if (TextInputComponent* text = focusedTextField(registry)) {
                    const size_t caret = clampCaret(*text);
                    text->caret = caret > 0 ? static_cast<int>(caret - 1) : 0;
                }
                break;

            case UIIntent::Type::CaretRight:
                if (TextInputComponent* text = focusedTextField(registry)) {
                    const size_t caret = clampCaret(*text);
                    text->caret = static_cast<int>(std::min(caret + 1, text->buffer.size()));
                }
                break;
        }
    }

    intents.clear();
}

} // namespace RuntimeUISystem
