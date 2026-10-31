#include "Onyx/Ui/Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui_internal.h>

#include "Onyx/Ui/ThemeState.h"

namespace Onyx::Ui::Widgets {

namespace {

constexpr float kSplitterWidth = 4.0f;

ImVec4 ReadColor(ImGuiCol col) { return ImGui::GetStyleColorVec4(col); }

ImVec4 TextOn(ImGuiCol surface) {
    return TextForSurface(ReadColor(surface), ReadColor(ImGuiCol_WindowBg),
                          ReadColor(ImGuiCol_Text));
}

}

bool IconButton(const char* id, const char* glyph, const IconButtonOptions& options) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }

    ImVec2 size = options.size;
    if (size.x <= 0.0f || size.y <= 0.0f) {
        const float h = ImGui::GetFrameHeight();
        size = ImVec2(h, h);
    }

    if (options.disabled) {
        ImGui::BeginDisabled();
    }
    ImGui::PushID(id);
    const ImGuiID itemId = window->GetID("##icon");
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(size);
    bool pressed = false;
    if (ImGui::ItemAdd(bb, itemId)) {
        bool hovered = false;
        bool held = false;
        pressed = ImGui::ButtonBehavior(bb, itemId, &hovered, &held);

        ImGuiCol surface;
        if (options.selected) {
            surface = (held && hovered) ? ImGuiCol_HeaderActive
                      : hovered         ? ImGuiCol_HeaderHovered
                                        : ImGuiCol_Header;
        } else {
            surface = (held && hovered) ? ImGuiCol_ButtonActive
                      : hovered         ? ImGuiCol_ButtonHovered
                                        : ImGuiCol_Button;
        }

        ImGui::RenderNavCursor(bb, itemId);
        ImGui::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(surface), true,
                           ImGui::GetCurrentContext()->Style.FrameRounding);

        ImFont* font = ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize();
        const ImU32 textColor = ImGui::GetColorU32(TextOn(surface));

        unsigned int codepoint = 0;
        ImTextCharFromUtf8(&codepoint, glyph, nullptr);
        ImFontBaked* baked = font ? font->GetFontBaked(fontSize) : nullptr;
        const ImFontGlyph* g = baked ? baked->FindGlyph(static_cast<ImWchar>(codepoint)) : nullptr;

        const ImVec2 centre((bb.Min.x + bb.Max.x) * 0.5f, (bb.Min.y + bb.Max.y) * 0.5f);
        ImVec2 pen;
        if (g && g->Visible) {
            pen = ImVec2(centre.x - (g->X0 + g->X1) * 0.5f, centre.y - (g->Y0 + g->Y1) * 0.5f);
        } else {
            const ImVec2 extent = ImGui::CalcTextSize(glyph);
            pen = ImVec2(centre.x - extent.x * 0.5f, centre.y - extent.y * 0.5f);
        }
        pen.x = IM_ROUND(pen.x);
        pen.y = IM_ROUND(pen.y);
        window->DrawList->AddText(font, fontSize, pen, textColor, glyph);
    }
    ImGui::PopID();
    if (options.disabled) {
        ImGui::EndDisabled();
    }

    if (options.tooltip && options.tooltip[0] != '\0' &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", options.tooltip);
    }
    return pressed;
}

bool TreeRow(const char* id, const char* label, const char* glyph, const glm::vec4& color,
             ImGuiTreeNodeFlags flags, bool selected) {
    const float pad = std::max(ImGui::GetFrameHeight() - ImGui::GetFontSize(), 8.0f) * 0.5f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad, pad));
    flags |= ImGuiTreeNodeFlags_FramePadding;

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 extent(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    const bool hovered =
        ImGui::IsMouseHoveringRect(cursor, ImVec2(cursor.x + extent.x, cursor.y + extent.y));

    ImVec4 textColor(color.x, color.y, color.z, color.w);
    if (hovered || selected) {
        const ImGuiCol surface = hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header;
        textColor = TextForSurface(ReadColor(surface), ReadColor(ImGuiCol_WindowBg), textColor);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);

    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "%s  %s###%s", glyph, label, id);
    const bool open = ImGui::TreeNodeEx(buffer, flags);

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    return open;
}

bool ToolbarButton(const char* label, bool selected) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ReadColor(ImGuiCol_Header));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ReadColor(ImGuiCol_HeaderHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ReadColor(ImGuiCol_HeaderActive));
        ImGui::PushStyleColor(ImGuiCol_Text, TextOn(ImGuiCol_Header));
    }
    const bool clicked = ImGui::Button(label);
    if (selected) {
        ImGui::PopStyleColor(4);
    }
    return clicked;
}

void Hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ReadColor(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void Spinner(const char* id, float radius, float thickness) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return;
    }
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + radius * 2.0f, pos.y + radius * 2.0f));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, window->GetID(id))) {
        return;
    }

    const float t = static_cast<float>(ImGui::GetTime());
    const float start = std::fmod(t * 4.0f, IM_PI * 2.0f);
    const float sweep = IM_PI * 1.5f;
    const float r = std::max(radius - thickness * 0.5f, 1.0f);
    const ImVec2 centre(pos.x + radius, pos.y + radius);

    constexpr int segments = 24;
    ImVec2 points[segments + 1];
    for (int i = 0; i <= segments; ++i) {
        const float a = start + sweep * static_cast<float>(i) / static_cast<float>(segments);
        points[i] = ImVec2(centre.x + std::cos(a) * r, centre.y + std::sin(a) * r);
    }
    window->DrawList->AddPolyline(points, segments + 1, ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                                  thickness, ImDrawFlags_None);
}

bool SplitterH(const char* id, float* left, float* right, float minLeft, float minRight) {
    const float total = *left + *right;
    const float height = ImGui::GetContentRegionAvail().y;

    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = kSplitterWidth * style.FontScaleMain * style.FontScaleDpi;

    ImGui::PushID(id);
    ImGui::InvisibleButton("##split", ImVec2(width, std::max(height, 1.0f)));
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    if (hovered || active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    bool changed = false;
    if (active) {
        const float upper = std::max(total - minRight, minLeft);
        const float next = std::clamp(*left + ImGui::GetIO().MouseDelta.x, minLeft, upper);
        if (next != *left) {
            *left = next;
            *right = total - next;
            changed = true;
        }
    }

    const ImVec2 lo = ImGui::GetItemRectMin();
    const ImVec2 hi = ImGui::GetItemRectMax();
    const ImGuiCol col = active    ? ImGuiCol_SeparatorActive
                         : hovered ? ImGuiCol_SeparatorHovered
                                   : ImGuiCol_Separator;
    const float mid = (lo.x + hi.x) * 0.5f;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(mid, lo.y), ImVec2(mid, hi.y),
                                        ImGui::GetColorU32(col), 1.0f);
    ImGui::PopID();
    return changed;
}

void StatusText(Services::Severity severity, const char* text) {
    ImVec4 color = ReadColor(ImGuiCol_Text);
    const ImVec4 bg = ReadColor(ImGuiCol_WindowBg);
    const bool darkBackground = (bg.x + bg.y + bg.z) < 1.5f;
    switch (severity) {
    case Services::Severity::Info:
        break;
    case Services::Severity::Warning:
        color = darkBackground ? ImVec4(0.95f, 0.75f, 0.30f, color.w)
                               : ImVec4(0.65f, 0.42f, 0.00f, color.w);
        break;
    case Services::Severity::Error:
        color = darkBackground ? ImVec4(0.95f, 0.38f, 0.36f, color.w)
                               : ImVec4(0.75f, 0.12f, 0.12f, color.w);
        break;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

Metrics MetricsFor(const Theme& theme) {
    const ThemeResolved& applied = theme.Applied();
    const float font = applied.style.FontSizeBase > 0.0f ? applied.style.FontSizeBase
                                                          : 13.0f * applied.globalScale;
    const float padY = applied.style.FramePadding.y;
    Metrics m;
    m.smallButtonInset = std::max(padY * 0.5f, 2.0f);
    m.iconButtonSize = font + padY * 2.0f;
    m.rowHeight = font + std::max(padY, 4.0f * applied.globalScale) * 2.0f;
    return m;
}

}
