#pragma once

class Registry;
class UIIntentQueue;

// The ECS-facing runtime UI system (Phase 16A): a pure-ish function over
// (World, intents). It drains the intent queue and applies each intent to the
// authoritative UI components (screen stack, focus), then clears the queue. It owns
// no state of its own — all UI state lives in components — so it round-trips and
// hot-reloads like every other system, and its effects are deterministic (intents
// apply in queue order on the fixed step).
//
// This is the *model* side of UI = f(ECS, input). The *view* (RmlUi) reads these
// components and renders them; it never lives here. See docs/DESIGN_RUNTIME_UI.md.
namespace RuntimeUISystem {

// Applies every queued intent to the UI components, in order, then clears the queue.
void update(Registry& registry, UIIntentQueue& intents);

} // namespace RuntimeUISystem
