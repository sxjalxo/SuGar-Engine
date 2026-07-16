#include "ui/RuntimeUISystem.h"

#include "ecs/Registry.h"
#include "ui/UIIntent.h"

#include <algorithm>

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
                for (auto& [entity, text] : registry.textInputs.getAll()) {
                    (void)entity;
                    const size_t caret = std::min(static_cast<size_t>(std::max(text.caret, 0)), text.buffer.size());
                    text.buffer.insert(caret, intent.arg);
                    text.caret = static_cast<int>(caret + intent.arg.size());
                }
                break;

            case UIIntent::Type::BackspaceText:
                for (auto& [entity, text] : registry.textInputs.getAll()) {
                    (void)entity;
                    const size_t caret = std::min(static_cast<size_t>(std::max(text.caret, 0)), text.buffer.size());
                    if (caret > 0) {
                        text.buffer.erase(caret - 1, 1);
                        text.caret = static_cast<int>(caret - 1);
                    }
                }
                break;
        }
    }

    intents.clear();
}

} // namespace RuntimeUISystem
