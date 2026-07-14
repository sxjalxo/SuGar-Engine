# Runtime UI — Architecture Design Record

> **Status:** Model layer **implemented** (Phase 16A); RmlUi build/link/FreeType smoke path implemented (16B.1); Vulkan view pending (16B.2+).
> Runtime UI leads Milestone M3.
> **Type:** Architecture record — decided *before* code exists, because the
> decisions here are the ones every future line of UI code must optimize around.
> First of the `docs/DESIGN_*.md` family (architecture records, a level above the
> README).
>
> **Implemented so far:** 16A delivered `UIScreenComponent` + `FocusComponent`
> (ECS, `src/ui/`), `UIIntentQueue`, `RuntimeUISystem` (drains intents on the
> fixed step), and full serializer round-trip so UI state survives snapshot
> restore. The `RuntimeUI` self-test proves intents mutate the model
> deterministically and that the model survives an in-place snapshot restore with
> the entity id preserved. 16B.1 delivered the engine-only RmlUi + FreeType smoke
> path: initialise headlessly, load a bundled font, create a context, build a
> memory document, and render through a no-op interface. **Not yet built:** the
> Vulkan-backed RmlUi view (16B.2+) -- everything below about
> documents/layout/data-binding describes that pending half.
---

## The governing invariant

> ## The UI is a **view**. ECS is the **model**.
>
> ```
> UI = f(ECS, input)
> ```

The rendered UI is a **pure function of authoritative ECS state plus current
input**. Nothing the player relies on lives only inside RmlUi.

This single decision is the compass. Whenever anyone asks *"Can I store this inside
RmlUi?"* the answer is mechanical:

> Does it violate `UI = f(ECS, input)`? If yes — **don't.**

**Why this is the whole point.** SuGar already guarantees snapshots, time travel,
hot reload, stable entity IDs, deterministic replay. If the UI is a function of ECS,
those guarantees extend to the UI **for free**: restore the ECS (the existing
snapshot system already does this — UI state is just components) and re-run `f` next
frame. There is *no* UI-specific time-travel code. Break the invariant and you
reintroduce exactly the class of bug the engine spent months eliminating
(restore → UI wrong).

---

## UI Ownership

Ownership is separate from — and prior to — state classification.

```
Gameplay   ──owns──►   Health · Inventory · Dialogue · Quests · Score
                              │
Runtime UI ──reads──►   visualizes them for the Player
```

**The runtime UI never *owns* a gameplay concept. It visualizes one.** Health is
not "UI state that the HUD owns"; it is game state the HUD *reads*. The inventory is
owned by gameplay; the inventory screen is a view of it.

This sounds obvious, which is exactly why it must be written down — it's the line
that drifts first. The moment a value's home is "the widget," the invariant is
already broken.

---

## The decision test

Two equivalent formulations. The practical one is the one to reach for:

> **Restore a snapshot. Does this piece of state, if missing, make the result look
> *wrong*, or just cosmetically *mid-flight*?**
> - Wrong / confusing → **authoritative** (lives in ECS / is serialized).
> - Only cosmetically in-flight → **derived** (rebuild it; never serialize).

The generative version, for the subtle cases:

> Is this a **function of ECS + input**, or an **independent choice the player made
> that isn't already in ECS**?
> - Function of ECS/input → derived (recompute).
> - Independent player choice not yet in ECS → authoritative (store in ECS).

Developers should not need category theory. They need: *restore → does it look
wrong?*

---

## Classification

| State | Verdict | Home |
|---|---|---|
| Health, ammo, currency, score | **Authoritative** — game state that happens to be shown | ECS |
| Inventory contents, quest log | **Authoritative** | ECS |
| Active screen / menu-nav stack | **Authoritative** | ECS (`UIScreenComponent`) |
| Selected weapon / hotbar slot | **Authoritative** | ECS |
| Dialogue: current node + available choices + chosen index | **Authoritative** | ECS (`DialogueStateComponent`) |
| Text mid-edit: buffer + caret index | **Authoritative** | ECS (`TextInputComponent`) |
| Keyboard / gamepad **focus** | **Authoritative** — player navigation, not derivable from cursor | ECS (`FocusComponent`) |
| **Content scroll** (inventory, quest log, settings) | **Authoritative** — player intentionally scrolled | ECS |
| **Viewport scroll** (combat-log auto-follow, live console, profiler) | **Derived** — a view over changing data | UI system |
| Mouse **hover** | **Derived** — pure function of cursor position + layout | UI system |
| Flexbox layout, box model, computed styles | **Derived** | RmlUi, rebuilt |
| Glyph atlas, font/texture caches | **Derived** | RmlUi, rebuilt |
| Clipping rects, element transforms | **Derived** | RmlUi, rebuilt |
| The `Rml::Document` / element tree | **Derived** — a view built from the model | RmlUi, rebuilt |
| Transition / animation *progress*, dialogue typewriter *reveal* | **Derived** — target is authoritative, the tween is not | RmlUi; snap / re-target on restore |
| Caret blink phase | **Derived** | UI system |

**Nothing derived is ever stored in ECS.** Derived state lives in the
RmlUi-owning runtime UI system and is reconstructible from the model at any time.

---

## Gray areas — where this document earns its keep

These are the ones that surface as bugs two years later if not decided now.

1. **Scroll: content vs viewport.** *Content scroll* (the player scrolled an
   inventory to row 40) is **authoritative** — it's a choice not encoded in game
   state, and a scrub that loses it is a Rule-21 bug. *Viewport scroll* (a combat log
   or console that auto-follows the tail of changing data) is **derived** — it's a
   function of the data, recompute it. Classify each scrollable region explicitly.
2. **Mouse hover vs. keyboard/gamepad focus.** *Mouse hover* is **derived** — a pure
   function of cursor position + layout; recompute on restore. *Keyboard/gamepad
   focus* is **authoritative** — the player navigated there, it is not derivable from
   the cursor, and a scrub that makes focus jump is wrong. **Split them.**
3. **Text mid-edit.** The **buffer + caret index are authoritative** (scrub back, the
   half-typed save name should still be there). The **caret blink phase is derived**.
4. **Transitions / typewriter reveal.** The **target** (menu open, full dialogue
   text) is authoritative; the **interpolation progress** is derived. On restore,
   snap to target or restart the transition — never serialize the tween.

---

## Data flow

```
Input ──► Intent ──► ECS ──► UI ──► Render
```

Note that this is the **same shape as gameplay**:

```
Input ──► Behavior ──► ECS ──► Renderer
```

That consistency is a real asset: runtime UI follows the exact philosophy as
gameplay logic — disposable logic, authoritative state in components. There is one
mental model for the whole engine, not two.

**Model → View.** A `RuntimeUISystem` reads authoritative UI + game components each
frame and pushes changed values into RmlUi (via its data-binding layer — *confirm the
exact RmlUi data-model API at integration time*). The document is derived; destroy it
(UI asset hot reload) and it rebuilds from ECS.

**View → Model.** RmlUi event handlers **emit intents only**. An intent is applied
to ECS by a system, which is the sole thing that mutates state.

```
[click "Use Item"] ──► emit UseItem intent ──► InventorySystem ──► mutate InventoryComponent
```

Reuse the existing `CollisionEvent`-style dispatch for the intent queue — this is the
same one-event-primitive pattern already in the engine.

---

## Hard rules for Runtime UI

These are the lines that quietly rot if left implicit.

1. **UI callbacks emit intents only. They never mutate persistent UI state
   directly.** Explicitly forbidden:

   ```
   onClick() {
       inventoryWindow.hide();      // ❌ hides via widget state
   }
   ```

   Instead:

   ```
   onClick() {
       emit(CloseScreen{ Inventory }); // ✅ intent → system → UIScreenComponent
   }
   ```

   Without this rule, state slowly leaks into callbacks and the invariant erodes one
   convenience at a time.

2. **RmlUi does not subscribe to ECS. It is not event-driven over the model.** The
   `RuntimeUISystem` **polls** ECS and updates RmlUi:

   ```
   RuntimeUISystem ──reads ECS──► updates RmlUi        ✅
   Health changed ──► RmlUi callback fires              ❌
   ```

   The polling cost is negligible; the architecture stays deterministic and
   single-directional. No hidden reactive graph, no update ordering surprises.

3. **No authoritative state hidden in RmlUi.** Every value the player depends on has
   an ECS home. RmlUi holds only derived, reconstructible state.

---

## Determinism: fixed-step intent consumption

UI events arrive at **render rate** (the mouse moves whenever). Authoritative ECS
mutations must happen on the **deterministic fixed step**, or replay / time travel
diverge. Resolution — identical to how raw input already feeds behaviors:

```
Render frame ──► queue intent
                     │
              Fixed step ──► apply intent ──► ECS mutation
                                                   │
                              Next frame ──► UI re-syncs from ECS
```

Intents are queued at render rate and **consumed on the next fixed step**. The view
updates every render frame (it's a view, it can be smooth), but authoritative
UI-state changes land deterministically.

**Engine mode vs. UI screen.** Do not conflate the two. `Play / Pause / Edit /
Replay` are **engine modes** (already `EngineState`). `Inventory / Dialogue /
Settings / PauseMenu` are **UI screens**. Opening the pause *menu* is a UI-screen
change that *also* emits an intent to set `EngineState::Paused`; the UI does not own
a second "paused" bool. This keeps sim-pause (engine) and menu-pause (UI) as one
source of truth each.

---

## Component sketch (authoritative only)

**Global UI — a singleton `UIRoot` entity.** Clean, one home for app-wide UI state:

```
UIRoot (singleton entity)
├── UIScreenComponent   { stack: vector<ScreenId> }   // active screen = stack.back()
├── FocusComponent      { focused: ElementId }         // keyboard/gamepad nav
└── (screen-specific, when active:)
    ├── DialogueStateComponent { node; choices[]; selected }
    └── TextInputComponent     { buffer; caret }
```

**World-space UI — per entity.** UI that belongs *beside* the thing it represents
lives on that thing's entity: enemy health bars, NPC interaction prompts, quest
markers, floating damage numbers. These are naturally per-entity components, not part
of `UIRoot`.

**Naming: `UIScreenComponent`, not `UIModeComponent`.** "Mode" is reserved for
*engine* modes (Play/Pause/Edit/Replay). UI screens (Inventory/Dialogue/Settings) are
UI *state* — different concept, don't overload the word.

**Derived state has no ECS presence.** Layout, atlases, tweens, computed styles,
mouse hover, viewport scroll all live in the `RuntimeUISystem` and rebuild on demand.

---

## Behavior under the engine's guarantees

- **Snapshot restore / scrub** — restore ECS; authoritative UI components come back
  through the ordinary snapshot path; `RuntimeUISystem` re-syncs the view next frame;
  derived state (layout, transitions) recomputes. The UI is correct with zero special
  cases.
- **UI asset hot reload** (edit `.rml`/`.rcss`) — destroy and rebuild the document
  from ECS. Authoritative state is untouched. Live UI iteration — on-brand for the
  wedge.
- **Code hot reload** — UI-driving systems reload; UI state in components survives
  (the Phase 6 / Phase 12 pattern).

---

## Open questions (to resolve at integration, not before)

1. **Data-binding mechanism** — RmlUi `DataModel` push vs. full rebuild vs. diff.
   Start with the simplest correct approach; **measure before optimizing** (the M2
   `SUGAR_BENCH` lesson). Confirm the actual RmlUi API.
2. **Intent representation** — typed event structs vs. a generic intent queue. Lean
   toward reusing the existing event-dispatch primitive.
3. **World-space UI batching / culling** — a rendering concern, deferred; does not
   affect this state model.

---

## Rule linkage

This record is the UI-specific application of **[RULES.md](../RULES.md) Rule 21**
(runtime systems must not own hidden authoritative state). The one-line rule that
belongs alongside it:

> *RmlUi is a view. Authoritative UI model state lives in ECS; documents, layout,
> atlases, and animation caches are derived and reconstructible from it. UI
> interactions emit intents that mutate ECS, never UI-local state. Therefore
> `UI = f(ECS, input)`, and snapshot restore / hot reload restore the UI for free.*
