#include "ui/RuntimeUIView.h"
#include "ui/RmlSystemInterface.h"

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
    const char* candidates[] = {
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

} // namespace

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
            ok = ok && document->GetElementById("probe") != nullptr;
        }
    }

    Rml::Shutdown();
    std::cout << "[uitest] RmlUi integration " << (ok ? "PASS" : "FAIL")
              << " (font engine: FreeType)\n";
    return ok;
}