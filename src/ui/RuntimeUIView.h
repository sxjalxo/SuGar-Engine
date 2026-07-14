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
class RuntimeUIView {
public:
    // Headless integration check: initialise RmlUi with a no-op renderer, load a
    // real FreeType font, create a context, load a document from memory, verify the
    // DOM built, render through the no-op interface, then shut down. Proves the
    // RmlUi + FreeType build/link/init path end-to-end without a GPU. Returns true
    // on pass. Safe to call before any Vulkan setup.
    static bool smokeTest();
};