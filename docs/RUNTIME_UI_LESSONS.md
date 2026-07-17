# Runtime UI — Lessons & Rationale (Phase 16B retrospective)

> **Why this file exists.** [DESIGN_RUNTIME_UI.md](DESIGN_RUNTIME_UI.md) says *what*
> the architecture is. This says *why*, and *what it cost to find out*. Most of the
> decisions below look like extra work at a glance, and every one of them is a
> trip-wire: undo it and you quietly break snapshots, replay, hot reload, or
> determinism — months later, far from the change. If you are about to "simplify"
> one of these, read its entry first.
>
> Status: written after 16B.8, when the design was fully validated by working code
> rather than intention.

---

## The one thing to remember

Rendering was never the hard part. Neither was mouse input. The hard question was
**where runtime UI state actually lives**, and the answer is now uniform:

| State | Home |
|---|---|
| Screen stack | **ECS** (`UIScreenComponent`) |
| Keyboard/gamepad focus | **ECS** (`FocusComponent`) |
| Text buffer | **ECS** (`TextInputComponent`) |
| Caret index | **ECS** (`TextInputComponent::caret`) |
| Which field typing goes to | **ECS** (focus ↔ `TextInputComponent::element`) |
| Mouse hover | Derived (view) |
| Layout, atlases, style | Derived (RmlUi) |
| Caret blink/glyph, transitions | Derived (view) |

**There is no exception.** `UI = f(ECS, input)`. Every entry below defends that.

---

## Why RmlUi `<input>` was not used

The obvious move for a text field is RmlUi's `<input>`. We render a plain `<div>`
and own the buffer ourselves instead.

Using `<input>` would make **RmlUi the owner of authoritative text**. That single
concession cascades:

- **Snapshots** — the buffer isn't in ECS, so a snapshot doesn't contain it.
- **Time travel** — scrub back, and a half-typed line is gone or stale.
- **Hot reload** — reloading the document silently drops in-progress text.
- **Replay/determinism** — text mutates outside the fixed step.
- **Multiplayer** (later) — nothing to replicate; the state is inside a UI widget.

Instead RmlUi is strictly a renderer:

```
TextInputComponent ──► RuntimeUIView ──► <div> ──► pixels
```

**Trip-wire:** if you ever swap the `<div>` for an `<input>` "because it's easier",
you have moved authoritative state into a library and broken all five of the above
at once. The convenience is real; the cost is architectural drift you won't notice
for months.

## Why the caret is authoritative but the blink is not

People reflexively treat cursors as presentation and don't serialize them. That's
half right. The split:

- **Authoritative:** `buffer` + `caret` index. Restore a snapshot mid-sentence and
  the caret must be where the player left it, or the next keystroke lands in the
  wrong place. Caret position is *the player's choice*, not derivable from the text.
- **Derived:** blink phase, glyph, colour, shape. Pure presentation, recomputed.

The rule generalises: *would restoring without this piece produce a **wrong** result,
or merely a cosmetically mid-flight one?* Wrong → authoritative.

## Why focus lives in ECS (and hover doesn't)

This split is subtle enough to get wrong twice.

- **Mouse hover is derived** — a pure function of cursor position + layout. Recompute
  it; never store it. Storing it would mean two sources of truth for where the mouse is.
- **Keyboard/gamepad focus is authoritative** — the player *navigated* there. It is
  **not** derivable from the cursor (the cursor may be elsewhere entirely, or the
  player may be on a gamepad with no cursor at all). If a scrub makes focus jump, that
  is a real bug.

So Tab does **not** move focus directly. The view computes the next id from the DOM
tab ring (order is a view concern) and **emits a `SetFocus` intent**; the fixed-step
system writes `FocusComponent`; the view polls it and calls `Element::Focus()`.
RmlUi never decides what is focused — it renders what ECS says is focused.

The payoff showed up free in 16B.8: because focus is a component, **text routing is
an ECS lookup** (`FocusComponent.focusedElement` ↔ `TextInputComponent::element`),
not a question you ask the UI library about which widget has the caret.

## Why callbacks only emit intents

Explicitly forbidden:

```cpp
onClick() { inventoryWindow.hide(); }   // ❌ mutates UI state in a callback
onClick() { emit(CloseScreen{...}); }   // ✅ intent -> system -> ECS
```

A callback that mutates state is a second write path into the model, invisible to the
scheduler, not on the fixed step, and not replayable. One convenience like this and
state starts leaking into handlers one line at a time.

Concretely this is enforced by `IntentEmitter`, whose entire body pushes an intent.
The payoff: **Enter on a focused element calls `Element::Click()`**, which fires the
same listener the mouse would — so keyboard and mouse share *exactly one* path into
ECS. No duplicate mapping, no second behaviour to keep in sync.

## Why intents drain on the fixed step

UI events arrive at render rate (the mouse moves whenever). Authoritative ECS
mutations must happen on the deterministic fixed step or replay diverges. So intents
are **queued at render rate, applied on the fixed step** — the same treatment raw
input already gets.

**Known consequence, accepted:** the runtime UI is inert in Edit mode, because there
is no fixed step. That's a workflow wart (authoring UI in Edit is awkward), *not* an
ownership problem — don't "fix" it by letting intents apply immediately.

## Why polling beat subscriptions

`RuntimeUISystem` **reads ECS and pushes into RmlUi**. RmlUi does **not** subscribe to
component changes.

```
RuntimeUIView ──reads ECS──► updates RmlUi     ✅
Health changed ──► RmlUi callback fires        ❌
```

A reactive graph buys nothing here and costs update-ordering surprises and hidden
coupling. The polling cost is negligible (the view diffs a small signature and only
touches the document when a value actually changed). Single direction, deterministic
order, trivially debuggable.

## Why the RenderInterface was written from scratch

RmlUi ships `RmlUi_Renderer_VK`, and it looks like a free win. It isn't: **that
backend creates and owns its own Vulkan instance/device/swapchain**. It's built to
*be* the application's renderer, not to compose with one that already exists.

So `RmlVulkanRenderer` implements `Rml::RenderInterface` against **our** renderer:
our device, our command buffer, our render pass. ~8 pure virtuals, one pipeline, one
vertex format. Textures reuse `Texture::createFromPixels` — which is also exactly how
FreeType's font atlases arrive (`GenerateTexture`), so no separate upload path.

**Trip-wire:** don't "just use the official Vulkan backend." It cannot share our
device.

## Why FreeType is mandatory, not optional polish

RmlUi's only font engines are `none` and `freetype`. `none` does not merely disable
text — **`Rml::Initialise()` fails outright**:

```
[RmlUi] No font engine interface set!
```

So FreeType is a hard dependency of *initialising the library at all*. It's vendored
purely as a glyph rasterizer with its optional deps (zlib/png/harfbuzz/brotli)
disabled — solved infrastructure, same category as stb_image/miniaudio (RULES.md
Rule 4).

---

## Bugs worth remembering

### One flag, two unrelated-looking symptoms

The best debugging story of Phase 16. Two bugs that looked independent:

1. **Every F-key shortcut was dead** — F5 save, F6 play, F7 pause, F8 hot-reload. They
   were gated on `!io.WantCaptureKeyboard`.
2. **Tab was swallowed** — the editor stole it from the runtime UI's focus navigation.

One cause:

```cpp
io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
```

The editor is one big ImGui dockspace, so with keyboard nav on, ImGui claims the
keyboard whenever *any* window has focus — making `WantCaptureKeyboard` **permanently
true**. The guard on the F-keys was therefore always false, and Tab always belonged to
ImGui.

Nav is now off (the editor is mouse-driven). Note the shape of the mistake: the first
fix (ungating the F-keys) treated a *symptom* and left the second bug alive. Finding
the shared cause fixed both and corrected the mental model — that's the valuable kind.

### RmlUi has no HTML defaults

Elements are `display: inline` unless told otherwise. Rows silently ran together on one
line until every stacked element got an explicit `display: block`. Likewise a `<div>` is
**not focusable** without `tab-index: auto` — `Element::Focus()` just silently does
nothing. Don't assume browser behaviour.

### Deferring geometry destruction

> Generalised in [DEV_ENVIRONMENT.md §6](DEV_ENVIRONMENT.md#6-gpu-resources-outlive-cpu-objects)
> — this is a *renderer* lesson, not a Runtime UI one. Any subsystem that hands GPU
> resources to a library per-frame inherits it.

`ReleaseGeometry` originally destroyed buffers immediately:

```
vkDestroyBuffer(): can't be called on VkBuffer 0x... that is currently in use
```

RmlUi drops geometry **during a re-layout**, while those buffers are still referenced
by command buffers in flight. They're now retired to a queue and freed after the
frames-in-flight margin. Only a screenshot caught this — no headless test would have.

### The validation pipeline earned its keep

A scripted bulk edit silently applied the serializer's **parser** but not its
**writer**. Text was read but never written, so patch removed the component and
`.get()` threw. It was not found by using the UI — the **self-test failed
immediately**. Lesson: script-generated edits need per-hunk verification, and the
tests are what make that safe.

---

## What is *not* architectural

Everything left in 16B is enhancement, not redesign — the ownership model doesn't move:

- **Arrow keys / mid-string editing** — extends an already-correct `caret`.
- **More text fields** — already works: add an entity with a `TextInputComponent`
  whose `element` matches a document id.
- **Dialogue** — just another ECS component (`DialogueStateComponent`).
- **Edit-mode authoring** — a workflow question, not an ownership one.

If a proposed change requires moving authoritative state *out* of ECS, it is not an
enhancement — it's a redesign, and the burden of proof is on it.
