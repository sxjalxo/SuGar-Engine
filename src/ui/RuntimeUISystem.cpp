#include "ui/RuntimeUISystem.h"

#include "ecs/Registry.h"
#include "ui/UIIntent.h"

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
                    if (!screen.screenStack.empty()) {
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
        }
    }

    intents.clear();
}

} // namespace RuntimeUISystem
