#pragma once

// Engine-side owner of the RmlUi runtime (Phase 16B): the view half of
// UI = f(ECS, input). It will hold the RmlUi library lifecycle + context, read the
// authoritative UI model from ECS (src/ui/UIComponents.h) each frame, and render it
// through a Vulkan RenderInterface. RmlUi is confined to the engine layer here;
// SuGarCore never sees it (RULES.md Rule 15). See docs/DESIGN_RUNTIME_UI.md.
//
// 16B.1 establishes the build/link/init path with FreeType and a headless smoke
// test. The Vulkan RenderInterface + ECS-to-document sync are Phase 16B.2+, which
// need the running editor to verify visually.
#include <vulkan/vulkan.h>

#include <memory>

#include <string>
#include <vector>

namespace Rml {
class Context;
class ElementDocument;
}
class RmlVulkanRenderer;
class Registry;
class UIIntentQueue;
class IntentEmitter;

class RuntimeUIView {
public:
    RuntimeUIView();
    ~RuntimeUIView();

    // Brings up RmlUi against the live Vulkan device: render interface, font engine,
    // context, and (for now) a built-in demo document. Call once the UI render pass
    // exists. Safe no-op on failure — the editor keeps running without runtime UI.
    // `intents` is the queue UI callbacks emit into — they never mutate UI state
    // directly (docs/DESIGN_RUNTIME_UI.md). May be null (smoke/headless paths).
    void init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool,
              VkQueue graphicsQueue, VkRenderPass renderPass, VkExtent2D extent,
              UIIntentQueue* intents);

    // Feeds pointer state to RmlUi so elements can hover/click. Called at render
    // rate; any resulting callback only *queues* an intent.
    void processMouse(float x, float y, bool leftDown);

    // Keyboard/gamepad focus navigation. Focus is *authoritative* state
    // (docs/DESIGN_RUNTIME_UI.md): this does not move focus directly — it works out
    // the next focusable element from the document and emits a SetFocus intent, which
    // the fixed-step system writes to FocusComponent. The view then applies it.
    void focusNext(bool reverse);
    // Activates the focused element, firing its normal click listener (which emits an
    // intent) — so keyboard and mouse share exactly one path into ECS.
    void activateFocused();

    // Records the runtime UI into `cmd`. Must be called inside the UI render pass.
    // Polls `registry` for the authoritative UI model first (never subscribes to it —
    // see docs/DESIGN_RUNTIME_UI.md), so the view is a pure function of ECS state.
    void render(VkCommandBuffer cmd, VkExtent2D extent, const Registry* registry);

    void shutdown();
    bool isReady() const { return context != nullptr; }

    // Headless integration check: initialise RmlUi with a no-op renderer, load a
    // real FreeType font, create a context, load a document from memory, verify the
    // DOM built, render through the no-op interface, then shut down. Proves the
    // RmlUi + FreeType build/link/init path end-to-end without a GPU. Returns true
    // on pass. Safe to call before any Vulkan setup.
    static bool smokeTest();

private:
    // Reads the authoritative UI components and pushes changed values into the
    // document. Polling, not subscription: no reactive graph, deterministic order.
    void syncFromEcs(const Registry* registry);
    // Pushes the authoritative text buffer into the document (view of ECS state).
    void syncTextFromEcs(const Registry* registry);

    std::unique_ptr<RmlVulkanRenderer> renderer;
    std::unique_ptr<IntentEmitter> openListener;
    std::unique_ptr<IntentEmitter> backListener;
    Rml::Context* context = nullptr;
    Rml::ElementDocument* document = nullptr;
    bool initialised = false;
    bool lastLeftDown = false;
    std::string lastScreen = "\xff"; // impossible value: forces the first sync
    std::string lastFocus = "\xff";  // mirrors FocusComponent, applied to the document
    std::string lastText = "\xff";   // mirrors TextInputComponent.buffer
    // Focusable element ids in document order (the tab ring). A view concern: the
    // *order* comes from the DOM, but the focused *value* lives in ECS.
    std::vector<std::string> focusables;
    UIIntentQueue* intentQueue = nullptr;
};