#include <Onyx/App/Widgets.h>
#include "imgui_internal.h"
#include <Onyx/Services/ThemeManager.h>
#include <stdio.h>
#include <unordered_map>

namespace Onyx::App::Widgets {

namespace {

// Predict the rect the next item will occupy. Mirrors what ImGui internally
// does for Button/Selectable/etc. (CalcItemSize + cursor). Used to test mouse
// hover BEFORE the native widget draws so we can pick the right text colour.
struct PredictedRect {
    ImVec2 min;
    ImVec2 max;
};

inline PredictedRect PredictItemRect(const ImVec2& contentSize,
                                     const ImVec2& explicitSize,
                                     const ImVec2& framePadding) {
    const ImGuiStyle& s = ImGui::GetStyle();
    (void)s;
    ImVec2 size = ImGui::CalcItemSize(
        explicitSize,
        contentSize.x + framePadding.x * 2.0f,
        contentSize.y + framePadding.y * 2.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    return { pos, ImVec2(pos.x + size.x, pos.y + size.y) };
}

inline bool IsRectHovered(const PredictedRect& r) {
    // Only count hover if mouse is within the rect AND the parent window is
    // accepting hover input (covers popups/modals etc.).
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                                ImGuiHoveredFlags_ChildWindows |
                                ImGuiHoveredFlags_RootWindow))
        return false;
    return ImGui::IsMouseHoveringRect(r.min, r.max);
}

// Push a Text colour matching the surface the widget will paint. Returns true
// if a colour was pushed (caller must Pop matching count).
inline bool PushTextForSurface(ImGuiCol surfaceCol) {
    ImVec4 bg = ImGui::GetStyleColorVec4(surfaceCol);
    ImVec4 textCol = Onyx::Theme::TextForSurface(bg);
    ImGui::PushStyleColor(ImGuiCol_Text, textCol);
    return true;
}

} // anonymous

bool IconButton(const char* id, const char* icon, const IconButtonOpts& opts) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    const ImGuiStyle& style = ImGui::GetStyle();

    ImVec2 btnSize = opts.size;
    if (btnSize.x <= 0.0f || btnSize.y <= 0.0f) {
        const float h = ImGui::GetFrameHeight(); // square, current row height
        btnSize = ImVec2(h, h);
    }

    if (opts.disabled)
        ImGui::BeginDisabled();

    ImGui::PushID(id);
    const ImGuiID itemId = window->GetID("##icon");

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + btnSize.x, pos.y + btnSize.y));
    ImGui::ItemSize(btnSize, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, itemId)) {
        ImGui::PopID();
        if (opts.disabled) ImGui::EndDisabled();
        return false;
    }

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, itemId, &hovered, &held);

    // Toggle look: a selected icon button paints with the Header palette so it
    // matches a selected tree row under the same accent.
    ImGuiCol surface;
    if (opts.selected)
        surface = (held && hovered) ? ImGuiCol_HeaderActive
                : hovered           ? ImGuiCol_HeaderHovered
                                    : ImGuiCol_Header;
    else
        surface = (held && hovered) ? ImGuiCol_ButtonActive
                : hovered           ? ImGuiCol_ButtonHovered
                                    : ImGuiCol_Button;

    ImGui::RenderNavCursor(bb, itemId);
    ImGui::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(surface), true, style.FrameRounding);

    // ── Centre on the glyph's ink, not on its advance ─────────────────────
    // ImGui::Button would centre the *text box*, whose width is the glyph's
    // AdvanceX. SF Symbols carry asymmetric side bearings, so an icon centred
    // that way lands visibly off (measured ~3px right on a 40px button). Place
    // the pen so the glyph's ink rect lands dead centre instead, and fall back
    // to advance-centring only if the glyph is missing from the atlas.
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const ImU32 textCol = ImGui::GetColorU32(Theme::TextForSurface(ImGui::GetStyleColorVec4(surface)));

    unsigned int codepoint = 0;
    ImTextCharFromUtf8(&codepoint, icon, nullptr);
    ImFontBaked* baked = font ? font->GetFontBaked(fontSize) : nullptr;
    const ImFontGlyph* glyph = baked ? baked->FindGlyph((ImWchar)codepoint) : nullptr;

    const ImVec2 centre((bb.Min.x + bb.Max.x) * 0.5f, (bb.Min.y + bb.Max.y) * 0.5f);
    ImVec2 pen;
    if (glyph && glyph->Visible) {
        pen.x = centre.x - (glyph->X0 + glyph->X1) * 0.5f;
        pen.y = centre.y - (glyph->Y0 + glyph->Y1) * 0.5f;
    } else {
        const ImVec2 sz = ImGui::CalcTextSize(icon);
        pen = ImVec2(centre.x - sz.x * 0.5f, centre.y - sz.y * 0.5f);
    }
    pen.x = IM_ROUND(pen.x);
    pen.y = IM_ROUND(pen.y);
    window->DrawList->AddText(font, fontSize, pen, textCol, icon);

    ImGui::PopID();
    if (opts.disabled)
        ImGui::EndDisabled();

    if (opts.tooltip && opts.tooltip[0] != '\0' &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", opts.tooltip);
    }

    return pressed;
}

bool IconButton(const char* id, const char* icon, const ImVec2& size) {
    IconButtonOpts opts;
    opts.size = size;
    return IconButton(id, icon, opts);
}

bool ColoredTreeNode(const char* id, const char* label, const char* icon, ImVec4 defaultColor, ImGuiTreeNodeFlags flags, bool isSelected) {
    // Taller rows for better hit-targets and visual breathing room.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
    flags |= ImGuiTreeNodeFlags_FramePadding;

    // Predict hover before drawing the node so we can flip text color in the
    // same frame the row first gets hovered.
    const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    const ImVec2 size      = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    const bool isHovered   = ImGui::IsMouseHoveringRect(cursorPos,
                                                       ImVec2(cursorPos.x + size.x, cursorPos.y + size.y));

    ImVec4 textColor = defaultColor;
    if (isHovered || isSelected) {
        const ImVec4 activeBg = isHovered
            ? ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered)
            : ImGui::GetStyleColorVec4(ImGuiCol_Header);
        // TextForSurface picks the current text colour when contrast is fine,
        // and only flips to the opposite endpoint when the surface luminance
        // would otherwise clash with the row label.
        textColor = Theme::TextForSurface(activeBg);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, textColor);

    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s  %s###%s", icon, label, id);

    const bool open = ImGui::TreeNodeEx(buffer, flags);

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    return open;
}

// ── Auto-contrast wrappers ────────────────────────────────────────────────

// Pick Button vs ButtonHovered vs ButtonActive based on predicted hover and
// the global mouse-down state. Active fires while the user is holding the
// left button over the (hovered) widget rect.
static ImGuiCol PickButtonSurface(bool hovered) {
    if (!hovered) return ImGuiCol_Button;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) return ImGuiCol_ButtonActive;
    return ImGuiCol_ButtonHovered;
}

bool Button(const char* label, const ImVec2& size) {
    const ImGuiStyle& s = ImGui::GetStyle();
    const ImVec2 textSz = ImGui::CalcTextSize(label, nullptr, true);
    const PredictedRect r = PredictItemRect(textSz, size, s.FramePadding);
    const bool hovered = IsRectHovered(r);
    PushTextForSurface(PickButtonSurface(hovered));
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor();
    return clicked;
}

bool SmallButton(const char* label) {
    // ImGui::SmallButton zeroes the vertical FramePadding, which with Onyx's
    // 1px frame border and 5px rounding leaves the label sitting on the edge.
    // Keep a real inset -- still shorter than Button, but not cramped. Derived
    // from the live FramePadding so it follows the UI scale.
    const ImGuiStyle& s = ImGui::GetStyle();
    // Half the vertical inset (floored so it never collapses at 1.0x) keeps it
    // reading as the compact variant; a touch more horizontal inset stops the
    // label from crowding the rounded corners.
    const ImVec2 padding(s.FramePadding.x * 1.25f, ImMax(s.FramePadding.y * 0.5f, 2.0f));

    const ImVec2 textSz = ImGui::CalcTextSize(label, nullptr, true);
    const PredictedRect r = PredictItemRect(textSz, ImVec2(0, 0), padding);
    const bool hovered = IsRectHovered(r);
    PushTextForSurface(PickButtonSurface(hovered));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);
    const bool clicked = ImGui::Button(label);   // Button honours FramePadding.y
    ImGui::PopStyleVar();

    ImGui::PopStyleColor();
    return clicked;
}

bool Selectable(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size) {
    // Selectable spans content-region-width when size.x == 0 and uses Header
    // colours for hover/selected states (just like TreeNode).
    const float availW = ImGui::GetContentRegionAvail().x;
    const ImVec2 textSz = ImGui::CalcTextSize(label, nullptr, true);
    const ImVec2 effSize(
        size.x != 0.0f ? size.x : availW,
        size.y != 0.0f ? size.y : (textSz.y));
    PredictedRect r;
    r.min = ImGui::GetCursorScreenPos();
    r.max = ImVec2(r.min.x + effSize.x, r.min.y + effSize.y);
    const bool hovered = IsRectHovered(r);

    ImGuiCol surface = ImGuiCol_Header;
    if (hovered)      surface = ImGuiCol_HeaderHovered;
    else if (selected) surface = ImGuiCol_Header;
    // If neither hovered nor selected, the row paints over WindowBg directly
    // — no extra Text push is needed, but we still align on Header to keep
    // behaviour consistent. ImGui only renders a coloured rect when
    // hovered/selected; with no rect, Text colour push is harmless.
    PushTextForSurface(surface);
    const bool clicked = ImGui::Selectable(label, selected, flags, size);
    ImGui::PopStyleColor();
    return clicked;
}

bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags) {
    const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    const ImVec2 size      = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    const bool hovered     = IsRectHovered({cursorPos, ImVec2(cursorPos.x + size.x, cursorPos.y + size.y)});

    PushTextForSurface(hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header);
    const bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopStyleColor();
    return open;
}

bool MenuItem(const char* label, const char* shortcut, bool selected, bool enabled) {
    const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    const ImVec2 size      = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    const bool hovered     = IsRectHovered({cursorPos, ImVec2(cursorPos.x + size.x, cursorPos.y + size.y)});

    PushTextForSurface(hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header);
    const bool clicked = ImGui::MenuItem(label, shortcut, selected, enabled);
    ImGui::PopStyleColor();
    return clicked;
}

bool BeginTabItem(const char* label, bool* p_open, ImGuiTabItemFlags flags) {
    // Cache each tab's previous-frame screen rect + selected state per-ID so
    // we can hit-test the current mouse against the cached rect to predict
    // THIS frame's hover before BeginTabItem draws. Tab rects only move on
    // resize / scroll — one frame of staleness on those edges is invisible.
    struct TabCache {
        ImVec2 rectMin, rectMax;
        bool   selected;
        bool   hasRect;
    };
    static std::unordered_map<ImGuiID, TabCache> s_tabCache;

    const ImGuiID id = ImGui::GetID(label);
    auto it = s_tabCache.find(id);

    bool predHovered = false;
    bool predSelected = false;
    if (it != s_tabCache.end() && it->second.hasRect) {
        predHovered = ImGui::IsMouseHoveringRect(it->second.rectMin, it->second.rectMax);
        predSelected = it->second.selected;
    }

    ImGuiCol surface = ImGuiCol_Tab;
    if (predHovered)       surface = ImGuiCol_TabHovered;
    else if (predSelected) surface = ImGuiCol_TabSelected;

    PushTextForSurface(surface);
    const bool open = ImGui::BeginTabItem(label, p_open, flags);
    ImGui::PopStyleColor();

    TabCache &entry = s_tabCache[id];
    entry.rectMin  = ImGui::GetItemRectMin();
    entry.rectMax  = ImGui::GetItemRectMax();
    entry.selected = open; // BeginTabItem returns true on the active tab
    entry.hasRect  = true;
    return open;
}

} // namespace Onyx::App::Widgets
