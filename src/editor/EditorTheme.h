#pragma once

#include <imgui.h>

// SuGar editor look. A single flat, cool blue-gray dark theme, applied in place of
// ImGui::StyleColorsDark(). Inspired by laivy's imgui#707 theme: airy spacing, subtle
// borders, soft rounding, and a single bright-blue accent for selection/active state.
// Not a copy -- one coherent palette derived from that reference.
namespace SugarEditorTheme {

// 0xRRGGBB -> ImVec4, so the palette below reads as hex like a design tool.
inline ImVec4 rgb(unsigned int hex, float alpha = 1.0f) {
    return ImVec4(((hex >> 16) & 0xFF) / 255.0f,
                  ((hex >> 8) & 0xFF) / 255.0f,
                  (hex & 0xFF) / 255.0f,
                  alpha);
}

inline void apply() {
    // --- palette (cool blue-gray neutrals + one blue accent) ---------------------
    const ImVec4 bg0     = rgb(0x0A0A0B); // window / deepest -- near-black, neutral
    const ImVec4 bg1     = rgb(0x100F10); // child panels, title bars, menu bar
    const ImVec4 bg2     = rgb(0x161516); // frames, buttons, headers (rest)
    const ImVec4 bg3     = rgb(0x201F21); // hovered
    const ImVec4 bg4     = rgb(0x2A292C); // active / pressed
    const ImVec4 text    = rgb(0xCFCFD2); // primary text
    const ImVec4 textDim = rgb(0x646469); // disabled / secondary
    const ImVec4 border  = rgb(0x1C1B1D); // hairline separators
    const ImVec4 accent  = rgb(0x4C8DFF); // blue: selection, active (the one splash of colour)
    const ImVec4 accentD = rgb(0x3A4A63); // neutral blue-gray: grabs, separators-active

    ImVec4* c = ImGui::GetStyle().Colors;
    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = textDim;
    c[ImGuiCol_WindowBg]              = bg0;
    c[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]               = bg1;
    c[ImGuiCol_Border]                = border;
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = bg2;
    c[ImGuiCol_FrameBgHovered]        = bg3;
    c[ImGuiCol_FrameBgActive]         = bg4;
    c[ImGuiCol_TitleBg]               = bg1;
    c[ImGuiCol_TitleBgActive]         = bg1;
    c[ImGuiCol_TitleBgCollapsed]      = bg0;
    c[ImGuiCol_MenuBarBg]             = bg1;
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = bg3;
    c[ImGuiCol_ScrollbarGrabHovered]  = bg4;
    c[ImGuiCol_ScrollbarGrabActive]   = accentD;
    c[ImGuiCol_CheckMark]             = accent;
    c[ImGuiCol_SliderGrab]            = accentD;
    c[ImGuiCol_SliderGrabActive]      = accent;
    c[ImGuiCol_Button]                = bg2;
    c[ImGuiCol_ButtonHovered]         = bg3;
    c[ImGuiCol_ButtonActive]          = bg4;
    // Selection reads as a soft blue fill (an approximation of the reference's blue
    // outline; a filled tint survives every widget without custom per-item drawing).
    c[ImGuiCol_Header]                = rgb(0x4C8DFF, 0.22f);
    c[ImGuiCol_HeaderHovered]         = rgb(0x4C8DFF, 0.30f);
    c[ImGuiCol_HeaderActive]          = rgb(0x4C8DFF, 0.40f);
    c[ImGuiCol_Separator]             = border;
    c[ImGuiCol_SeparatorHovered]      = accentD;
    c[ImGuiCol_SeparatorActive]       = accent;
    c[ImGuiCol_ResizeGrip]            = rgb(0xFFFFFF, 0.05f);
    c[ImGuiCol_ResizeGripHovered]     = accentD;
    c[ImGuiCol_ResizeGripActive]      = accent;
    c[ImGuiCol_Tab]                   = bg1;
    c[ImGuiCol_TabHovered]            = bg3;
    c[ImGuiCol_TabActive]             = bg2;
    c[ImGuiCol_TabUnfocused]          = bg1;
    c[ImGuiCol_TabUnfocusedActive]    = bg2;
    c[ImGuiCol_DockingPreview]        = rgb(0x4C8DFF, 0.40f);
    c[ImGuiCol_DockingEmptyBg]        = bg0;
    c[ImGuiCol_PlotLines]             = accentD;
    c[ImGuiCol_PlotLinesHovered]      = accent;
    c[ImGuiCol_PlotHistogram]         = accentD;
    c[ImGuiCol_PlotHistogramHovered]  = accent;
    c[ImGuiCol_TableHeaderBg]         = bg2;
    c[ImGuiCol_TableBorderStrong]     = border;
    c[ImGuiCol_TableBorderLight]      = rgb(0x2B3039, 0.5f);
    c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = rgb(0xFFFFFF, 0.02f);
    c[ImGuiCol_TextSelectedBg]        = rgb(0x4C8DFF, 0.35f);
    c[ImGuiCol_NavHighlight]          = accent;
    c[ImGuiCol_DragDropTarget]        = accent;
    c[ImGuiCol_ModalWindowDimBg]      = rgb(0x000000, 0.45f);

    // --- geometry (airy, softly rounded, thin borders) ---------------------------
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding     = ImVec2(10, 8);
    s.FramePadding      = ImVec2(9, 5);
    s.CellPadding       = ImVec2(7, 4);
    s.ItemSpacing       = ImVec2(8, 6);
    s.ItemInnerSpacing  = ImVec2(6, 5);
    s.IndentSpacing     = 18.0f;
    s.ScrollbarSize     = 11.0f;
    s.GrabMinSize       = 9.0f;

    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.TabBorderSize     = 0.0f;

    s.WindowRounding    = 5.0f;
    s.ChildRounding     = 5.0f;
    s.FrameRounding     = 4.0f;
    s.PopupRounding     = 5.0f;
    s.GrabRounding      = 3.0f;
    s.TabRounding       = 5.0f;
    s.ScrollbarRounding = 6.0f;

    s.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None; // no collapse arrow crowding the title
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextPadding    = ImVec2(18, 4);
}

} // namespace SugarEditorTheme
