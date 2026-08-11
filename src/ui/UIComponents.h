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

// In-progress text entry (a save name, a chat line). The buffer and caret are
// **authoritative**: scrub back and the half-typed text must still be there, so they
// live here rather than inside an RmlUi text field. The caret *blink phase* is
// derived and stays in the view. See docs/DESIGN_RUNTIME_UI.md.
//
// `element` ties this field to the document element that displays it, which is how
// typing is routed: text intents only apply to the field whose `element` matches
// FocusComponent::focusedElement. Focus and text are both authoritative ECS state,
// so routing is decided in ECS — not by asking RmlUi which widget has the caret.
struct TextInputComponent {
    ElementId element;
    std::string buffer;
    int caret = 0; // insertion index into `buffer`
};

// A read-only text label bound to a document element by id. Unlike TextInputComponent
// (editable, focusable, caret-bearing), this is pure *output*: gameplay writes `text`, the
// UI view copies it verbatim into the element's inner text. The HUD primitive a game needs
// for a score / timer / health readout, and the general "push text to a UI element" hook
// that supersedes the demo HUD's hardcoded name/tag rendering.
struct UILabelComponent {
    ElementId element;
    std::string text;
};

// Presentation state for one document element: which CSS classes it carries and any
// inline style overrides. The other half of UILabelComponent — that one answers "what
// does this element say", this one answers "how does it currently look".
//
// M4 L3 forced it: a hotbar's selected slot, a health bar's fill width and an inventory
// panel's visibility are all *state the game owns* that no amount of text-setting can
// express. Without it a game either hardcodes markup strings into gameplay code (styling
// leaks out of the RCSS) or the engine grows a bespoke widget per HUD.
//
//   classes - space-separated, e.g. "slot selected". The view syncs the element to
//             exactly this set: classes it applied last frame and no longer sees are
//             removed, so a game never has to "unset" anything.
//   style   - inline declarations, e.g. "width: 62%; opacity: 0.5". Same shape as HTML's
//             style attribute, and the escape hatch for continuous values a class cannot
//             express. Properties dropped from the string are cleared.
//
// Authoritative (Rule 21b): "which slot is selected" is a decision the player made, not
// something recomputable from the present, so it serializes and survives a snapshot like
// any other gameplay state.
struct UIElementStateComponent {
    ElementId element;
    std::string classes;
    std::string style;
};

// Text anchored to a point in the WORLD rather than to the screen — a mob nameplate, an
// interaction hint, a damage number (M4 L3, see the addendum in docs/DESIGN_RUNTIME_UI.md).
//
// The anchor is the entity's own transform plus `offsetY`; there is no second position to
// desync. Everything about *where it lands on screen* — pixel position, scale, whether it
// is visible at all — is derived every frame from the camera and never stored.
struct WorldLabelComponent {
    std::string text;

    // Metres above the entity's origin. A nameplate sits over the head, not in the chest.
    float offsetY = 2.0f;

    // Past this distance the label is dropped entirely. A world full of labelled mobs is
    // unreadable long before it is slow, so the cull is a legibility decision first.
    float maxDistance = 24.0f;
};
