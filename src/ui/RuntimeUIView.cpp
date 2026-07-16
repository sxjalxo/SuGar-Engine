#include "ui/RuntimeUIView.h"
#include "ui/RmlSystemInterface.h"
#include "ui/RmlVulkanRenderer.h"
#include "ui/UIIntent.h"

#include "ecs/Registry.h"

#include <RmlUi/Core/EventListener.h>

// Bridges an RmlUi callback to the intent queue. This is the *only* thing a UI
// callback is allowed to do: emit an intent. It never touches UI state, never
// hides a document, never writes ECS — the fixed-step RuntimeUI system does that.
// See RULES.md Rule 21 and docs/DESIGN_RUNTIME_UI.md.
class IntentEmitter : public Rml::EventListener {
public:
    IntentEmitter(UIIntentQueue* queue, UIIntent intent) : queue(queue), intent(std::move(intent)) {}

    void ProcessEvent(Rml::Event& /*event*/) override {
        if (queue != nullptr) {
            queue->push(intent);
        }
    }

private:
    UIIntentQueue* queue = nullptr;
    UIIntent intent;
};

#include <RmlUi/Core.h>
#include <RmlUi/Core/RenderInterface.h>

#include <iostream>

namespace {

// Placeholder render interface: satisfies RmlUi's eight pure virtuals with no-ops so
// the DOM + layout path runs headlessly (no GPU). The real Vulkan RenderInterface,
// translating RmlUi geometry/textures into SuGar's Vulkan draw calls, is Phase
// 16B.2 and requires visual verification in the running editor.
class NullRenderInterface : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

bool loadSmokeFont() {
    // Engine-owned asset first; the RmlUi sample copy remains a fallback for
    // headless runs from a build directory.
    const char* candidates[] = {
        "assets/fonts/LatoLatin-Regular.ttf",
        "../assets/fonts/LatoLatin-Regular.ttf",
        "external/RmlUi/Samples/assets/LatoLatin-Regular.ttf",
        "../external/RmlUi/Samples/assets/LatoLatin-Regular.ttf",
        "../../external/RmlUi/Samples/assets/LatoLatin-Regular.ttf"
    };

    for (const char* path : candidates) {
        if (Rml::LoadFontFace(path, "LatoLatin", Rml::Style::FontStyle::Normal)) {
            std::cout << "[uitest] loaded font: " << path << "\n";
            return true;
        }
    }

    std::cout << "[uitest] failed to load RmlUi smoke-test font\n";
    return false;
}

// The system interface must outlive every RmlUi call, including Shutdown().
RmlSystemInterface& sharedSystemInterface() {
    static RmlSystemInterface instance;
    return instance;
}

// A placeholder HUD until the ECS -> document sync lands (16B.3): enough real
// geometry + text to prove the Vulkan render interface draws correctly.
const char* const kDemoDocument = R"(
<rml>
<head><style>
body { font-family: LatoLatin; font-size: 16px; width: 100%; height: 100%; }
/* RmlUi has no HTML defaults: elements are inline unless told otherwise, so the
   rows below must be explicit blocks or they run together on one line. */
#hud {
    display: block;
    position: absolute; left: 24px; top: 24px; width: 260px; height: 92px;
    background-color: #1e2430ee; border: 2px #4fc3f7ff; padding: 12px;
}
#title { display: block; font-size: 20px; color: #4fc3f7ff; }
#body { display: block; font-size: 14px; color: #e0e0e0ff; }
</style></head>
<body>
    <div id="hud">
        <div id="title">SuGar Runtime UI</div>
        <div id="body">RmlUi rendering through Vulkan.</div>
    </div>
</body>
</rml>
)";

} // namespace

RuntimeUIView::RuntimeUIView() = default;
RuntimeUIView::~RuntimeUIView() = default;

void RuntimeUIView::init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool,
                         VkQueue graphicsQueue, VkRenderPass renderPass, VkExtent2D extent,
                         UIIntentQueue* intents) {
    if (initialised) {
        return;
    }

    try {
        renderer = std::make_unique<RmlVulkanRenderer>();
        renderer->init(device, physicalDevice, commandPool, graphicsQueue, renderPass);
    } catch (const std::exception& e) {
        std::cerr << "[RuntimeUI] render interface init failed: " << e.what() << "\n";
        renderer.reset();
        return;
    }

    Rml::SetSystemInterface(&sharedSystemInterface());
    Rml::SetRenderInterface(renderer.get());

    if (!Rml::Initialise()) {
        std::cerr << "[RuntimeUI] Rml::Initialise failed\n";
        renderer->shutdown();
        renderer.reset();
        return;
    }
    initialised = true;

    if (!loadSmokeFont()) {
        std::cerr << "[RuntimeUI] no font loaded; text will not render\n";
    }

    context = Rml::CreateContext("sugar", Rml::Vector2i(static_cast<int>(extent.width),
                                                        static_cast<int>(extent.height)));
    if (context == nullptr) {
        std::cerr << "[RuntimeUI] failed to create context\n";
        return;
    }

    // Engine-owned UI asset. Falls back to the in-source demo only if the file is
    // missing, so a broken asset path is visible rather than silent.
    document = context->LoadDocument("assets/ui/hud.rml");
    if (document == nullptr) {
        std::cerr << "[RuntimeUI] assets/ui/hud.rml not found; using built-in fallback\n";
        document = context->LoadDocumentFromMemory(kDemoDocument);
    }
    if (document == nullptr) {
        std::cerr << "[RuntimeUI] failed to load any document\n";
        return;
    }

    // Buttons emit intents only — the hard rule from the design record.
    if (intents != nullptr) {
        if (Rml::Element* open = document->GetElementById("open")) {
            openListener = std::make_unique<IntentEmitter>(intents, UIIntent::openScreen("Inventory"));
            open->AddEventListener("click", openListener.get());
        }
        if (Rml::Element* back = document->GetElementById("back")) {
            backListener = std::make_unique<IntentEmitter>(intents, UIIntent::popScreen());
            back->AddEventListener("click", backListener.get());
        }
    }

    document->Show();
    std::cout << "[RuntimeUI] RmlUi ready (Vulkan render interface)\n";
}

void RuntimeUIView::processMouse(float x, float y, bool leftDown) {
    if (context == nullptr) {
        return;
    }
    context->ProcessMouseMove(static_cast<int>(x), static_cast<int>(y), 0);
    if (leftDown && !lastLeftDown) {
        context->ProcessMouseButtonDown(0, 0);
    } else if (!leftDown && lastLeftDown) {
        context->ProcessMouseButtonUp(0, 0); // click fires here -> IntentEmitter
    }
    lastLeftDown = leftDown;
}

void RuntimeUIView::syncFromEcs(const Registry* registry) {
    if (registry == nullptr || document == nullptr) {
        return;
    }

    // Poll the authoritative model. The UIRoot singleton owns it, but nothing here
    // assumes exactly one entity carries the component.
    std::string activeScreen;
    for (const auto& [entity, screen] : registry->uiScreens.getAll()) {
        (void)entity;
        activeScreen = screen.active();
        break;
    }

    // Only push when the model actually changed — RmlUi rebuilds layout on SetInnerRML.
    if (activeScreen == lastScreen) {
        return;
    }
    lastScreen = activeScreen;

    if (Rml::Element* body = document->GetElementById("body")) {
        body->SetInnerRML(activeScreen.empty() ? "No screen" : ("Screen: " + activeScreen));
    }
}

void RuntimeUIView::render(VkCommandBuffer cmd, VkExtent2D extent, const Registry* registry) {
    if (context == nullptr || !renderer || !renderer->isReady()) {
        return;
    }
    syncFromEcs(registry); // ECS is the model; the document is a projection of it
    context->SetDimensions(Rml::Vector2i(static_cast<int>(extent.width), static_cast<int>(extent.height)));
    renderer->beginFrame(cmd, extent);
    context->Update();
    context->Render();
}

void RuntimeUIView::shutdown() {
    if (initialised) {
        // Shutdown releases documents/geometry/textures through the render
        // interface, so it must still be alive at this point.
        Rml::Shutdown(); // destroys documents, which reference the listeners below
        initialised = false;
    }
    openListener.reset();
    backListener.reset();
    context = nullptr;
    document = nullptr;
    if (renderer) {
        renderer->shutdown();
        renderer.reset();
    }
}

bool RuntimeUIView::smokeTest() {
    static RmlSystemInterface systemInterface;
    static NullRenderInterface renderInterface;
    Rml::SetSystemInterface(&systemInterface);
    Rml::SetRenderInterface(&renderInterface);

    if (!Rml::Initialise()) {
        std::cout << "[uitest] RmlUi Initialise failed. Expected FreeType-backed "
                     "RmlUi build; check RMLUI_FONT_ENGINE=freetype and "
                     "external/freetype wiring.\n";
        return false;
    }

    bool ok = loadSmokeFont();

    Rml::Context* context = Rml::CreateContext("smoke", Rml::Vector2i(200, 200));
    ok = ok && context != nullptr;

    if (ok && context != nullptr) {
        const Rml::String documentRml =
            "<rml><head><style>body{width:100%;height:100%;font-family:LatoLatin;}</style></head>"
            "<body><div id=\"probe\">hello</div></body></rml>";
        Rml::ElementDocument* document = context->LoadDocumentFromMemory(documentRml);
        ok = ok && document != nullptr;
        if (document != nullptr) {
            document->Show();
            context->Update();
            context->Render();

            Rml::Element* probe = document->GetElementById("probe");
            ok = ok && probe != nullptr;
            if (probe != nullptr) {
                // Non-zero height proves FreeType actually measured/rasterized the
                // text: layout produced a real line box, not an empty element. This
                // is what distinguishes a working font engine from a loaded library.
                const bool textMeasured = probe->GetClientHeight() > 0.0f;
                if (!textMeasured) {
                    std::cout << "[uitest] probe height is 0 - font not measuring text\n";
                }
                ok = ok && textMeasured;
            }
        }
    }

    Rml::Shutdown();
    std::cout << "[uitest] RmlUi integration " << (ok ? "PASS" : "FAIL")
              << " (font engine: FreeType)\n";
    return ok;
}