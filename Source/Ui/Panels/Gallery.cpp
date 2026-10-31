#include "Onyx/Ui/Panels/Gallery.h"

#include <Onyx/Ui/Fonts.h>
#include <Onyx/Ui/Icons.h>
#include <Onyx/Ui/SfSymbols.h>
#include <Onyx/Ui/Theme.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Widgets.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>

namespace Onyx::Ui {

namespace {

constexpr float kMinContrast = 4.5f;

constexpr std::array<const char*, Gallery::kPageCount> kPageNames = {
    ICON_SF_SQUARE_GRID_3X3 " Widgets", ICON_SF_PAINTBRUSH " Theme", ICON_SF_TEXTFORMAT " Typography",
    ICON_SF_SPARKLES " Icons", ICON_SF_WRENCH_AND_SCREWDRIVER " Style"};
constexpr std::array<const char*, 3> kComboItems = {"First", "Second", "Third"};

float Linear(float channel) {
    return channel <= 0.04045f ? channel / 12.92f : std::pow((channel + 0.055f) / 1.055f, 2.4f);
}

float Luminance(const ImVec4& c) {
    return 0.2126f * Linear(c.x) + 0.7152f * Linear(c.y) + 0.0722f * Linear(c.z);
}

// WCAG contrast of `fg` composited over the opaque `bg`, against `bg`.
float ContrastRatio(const ImVec4& fg, const ImVec4& bg) {
    const float a = std::clamp(fg.w, 0.0f, 1.0f);
    const ImVec4 shown((1.0f - a) * bg.x + a * fg.x, (1.0f - a) * bg.y + a * fg.y, (1.0f - a) * bg.z + a * fg.z, 1.0f);
    const float l1 = Luminance(shown);
    const float l2 = Luminance(bg);
    return (std::max(l1, l2) + 0.05f) / (std::min(l1, l2) + 0.05f);
}

void RowLabel(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", label);
    ImGui::TableNextColumn();
}

bool BeginRows(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 110.0f * ImGui::GetStyle().FontScaleMain);
    ImGui::TableSetupColumn("##sample", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

}

void Gallery::Draw(UiContext& ui) {
    if (!ImGui::BeginTabBar("##gallery_pages")) return;
    for (size_t page = 0; page < kPageCount; ++page) {
        if (ImGui::BeginTabItem(kPageNames[page])) {
            if (ImGui::BeginChild("##gallery_body")) DrawPage(page, ui);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();
}

void Gallery::DrawPage(size_t index, UiContext& ui) {
    switch (index) {
    case 0: DrawWidgets(); break;
    case 1: DrawTheme(ui); break;
    case 2: DrawTypography(ui); break;
    case 3: DrawIcons(); break;
    case 4: DrawStyle(ui); break;
    default: break;
    }
}

const std::vector<uint32_t>& Gallery::IconMatches(std::string_view filter) {
    if (m_iconMatchesBuilt && m_iconMatchesFor == filter) return m_iconMatches;
    std::string needle(filter);
    for (char& c : needle) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const std::span<const IconEntry> table = IconTable();
    m_iconMatches.clear();
    for (size_t i = 0; i < table.size(); ++i) {
        if (needle.empty() || std::strstr(table[i].name, needle.c_str())) {
            m_iconMatches.push_back(static_cast<uint32_t>(i));
        }
    }
    m_iconMatchesFor = filter;
    m_iconMatchesBuilt = true;
    return m_iconMatches;
}

void Gallery::DrawWidgets() {
    const float scale = ImGui::GetStyle().FontScaleMain;

    ImGui::SeparatorText("Buttons");
    if (BeginRows("##buttons")) {
        RowLabel("Icon buttons");
        Widgets::IconButtonOptions plain;
        plain.tooltip = "Plain";
        Widgets::IconButton("##gear", ICON_SF_GEAR, plain);
        ImGui::SameLine();
        Widgets::IconButtonOptions selected;
        selected.tooltip = "Selected; click to flip";
        selected.selected = m_toggle;
        if (Widgets::IconButton("##star", ICON_SF_STAR_FILL, selected)) m_toggle = !m_toggle;
        ImGui::SameLine();
        Widgets::IconButtonOptions disabled;
        disabled.tooltip = "Disabled";
        disabled.disabled = true;
        Widgets::IconButton("##trash", ICON_SF_TRASH, disabled);

        RowLabel("Toolbar");
        Widgets::ToolbarButton("Normal");
        ImGui::SameLine();
        Widgets::ToolbarButton("Selected", true);

        RowLabel("ImGui");
        ImGui::Button("Button");
        ImGui::SameLine();
        ImGui::SmallButton("SmallButton");
        ImGui::SameLine();
        ImGui::BeginDisabled();
        ImGui::Button("Disabled");
        ImGui::EndDisabled();
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Rows and text");
    if (BeginRows("##rows")) {
        RowLabel("Tree rows");
        const ImGuiTreeNodeFlags leaf = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        Widgets::TreeRow("##tree_a", "Mesh", ICON_SF_CUBE_FILL, glm::vec4(0.45f, 0.80f, 1.00f, 1.0f), leaf, false);
        Widgets::TreeRow("##tree_b", "Texture, selected", ICON_SF_PHOTO, glm::vec4(1.00f, 0.72f, 0.35f, 1.0f), leaf,
                         true);

        RowLabel("Status");
        Widgets::StatusText(Services::Severity::Info, "Info");
        ImGui::SameLine();
        Widgets::StatusText(Services::Severity::Warning, "Warning");
        ImGui::SameLine();
        Widgets::StatusText(Services::Severity::Error, "Error");

        RowLabel("Hint");
        Widgets::Hint("A hint reads quieter than body text.");

        RowLabel("Spinner");
        Widgets::Spinner("##spinner", 8.0f * scale, 2.0f);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("ImGui controls");
    if (BeginRows("##inputs")) {
        ImGui::PushItemWidth(240.0f * scale);
        RowLabel("Slider");
        ImGui::SliderFloat("##slider", &m_slider, 0.0f, 1.0f, "%.2f");
        RowLabel("Text");
        ImGui::InputText("##text", m_text.data(), m_text.size());
        RowLabel("Combo");
        ImGui::Combo("##combo", &m_combo, kComboItems.data(), static_cast<int>(kComboItems.size()));
        RowLabel("Toggles");
        ImGui::Checkbox("Checkbox", &m_check);
        ImGui::SameLine();
        ImGui::RadioButton("A", &m_radio, 0);
        ImGui::SameLine();
        ImGui::RadioButton("B", &m_radio, 1);
        RowLabel("Progress");
        m_progress = std::fmod(m_progress + ImGui::GetIO().DeltaTime * 0.25f, 1.0f);
        ImGui::ProgressBar(m_progress, ImVec2(240.0f * scale, 0));
        ImGui::PopItemWidth();
        ImGui::EndTable();
    }
}

void Gallery::DrawTheme(UiContext& ui) {
    const ImGuiStyle& applied = ui.theme.Applied().style;
    const ImVec4 background = applied.Colors[ImGuiCol_WindowBg];
    ImGui::TextDisabled("Contrast over the window background; rows under %.1f are marked.", kMinContrast);

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("##palette", 3, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Colour", ImGuiTableColumnFlags_WidthFixed, 56.0f * applied.FontScaleMain);
    ImGui::TableSetupColumn("Token");
    ImGui::TableSetupColumn("Contrast");
    ImGui::TableHeadersRow();
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        const ImVec4& color = applied.Colors[i];
        const float ratio = ContrastRatio(color, background);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID(i);
        ImGui::ColorButton("##swatch", color, ImGuiColorEditFlags_NoTooltip,
                           ImVec2(ImGui::GetFrameHeight() * 2.0f, ImGui::GetFrameHeight() * 0.7f));
        ImGui::PopID();
        ImGui::TableNextColumn();
        const std::string_view token = ColorToken(i);
        ImGui::TextUnformatted(token.data(), token.data() + token.size());
        ImGui::TableNextColumn();
        if (ratio < kMinContrast) {
            char text[32];
            std::snprintf(text, sizeof text, "%.2f  low", ratio);
            Widgets::StatusText(Services::Severity::Warning, text);
        } else {
            ImGui::Text("%.2f", ratio);
        }
    }
    ImGui::EndTable();
}

void Gallery::DrawTypography(UiContext& ui) {
    if (!ui.fonts) {
        Widgets::Hint("The host uses ImGui's default font; there is no font set to show.");
        return;
    }
    struct Sample {
        const char* label;
        ImFont* font;
        const char* text;
    };
    const Sample samples[] = {
        {"Text", ui.fonts->text, "The quick brown fox jumps over the lazy dog 0123456789"},
        {"Mono", ui.fonts->mono, "The quick brown fox jumps over the lazy dog 0123456789"},
        {"Icons", ui.fonts->icons, ICON_SF_GEAR " " ICON_SF_PHOTO " " ICON_SF_CUBE_FILL " " ICON_SF_WAVEFORM},
    };
    for (const Sample& sample : samples) {
        ImGui::SeparatorText(sample.label);
        if (!sample.font) {
            Widgets::Hint("Not loaded.");
            continue;
        }
        ScopedFont font(sample.font);
        ImGui::TextWrapped("%s", sample.text);
        ImGui::TextDisabled("%.1f px", ImGui::GetStyle().FontSizeBase);
    }
}

void Gallery::DrawIcons() {
    const float scale = ImGui::GetStyle().FontScaleMain;
    ImGui::SetNextItemWidth(240.0f * scale);
    ImGui::InputTextWithHint("##icon_filter", ICON_SF_MAGNIFYINGGLASS " filter by name", m_iconFilter.data(),
                             m_iconFilter.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f * scale);
    ImGui::SliderFloat("Size", &m_iconScale, 1.0f, 4.0f, "%.1fx");

    const std::span<const IconEntry> table = IconTable();
    const std::vector<uint32_t>& matches = IconMatches(m_iconFilter.data());
    ImGui::SameLine();
    ImGui::TextDisabled("%zu of %zu", matches.size(), table.size());
    if (!m_copied.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("copied %s", m_copied.c_str());
    }
    ImGui::Separator();

    if (ImGui::BeginChild("##icon_grid")) {
        if (matches.empty()) {
            ImGui::TextDisabled("No icon matches.");
        } else {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float glyphSize = style.FontSizeBase * m_iconScale;
            const float cell = glyphSize + style.FramePadding.x * 4.0f;
            const size_t perRow = std::max<size_t>(
                1, static_cast<size_t>((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) /
                                       (cell + style.ItemSpacing.x)));
            const size_t rows = (matches.size() + perRow - 1) / perRow;
            Widgets::ForEachVisible(rows, cell + style.ItemSpacing.y, [&](size_t row) {
                for (size_t col = 0; col < perRow; ++col) {
                    const size_t slot = row * perRow + col;
                    if (slot >= matches.size()) break;
                    if (col > 0) ImGui::SameLine();
                    const IconEntry& icon = table[matches[slot]];
                    ImGui::PushID(static_cast<int>(slot));
                    Widgets::IconButtonOptions options;
                    options.size = ImVec2(cell, cell);
                    ImGui::PushFont(nullptr, glyphSize);
                    const bool hit = Widgets::IconButton("##icon", icon.glyph, options);
                    ImGui::PopFont();
                    if (hit) {
                        ImGui::SetClipboardText(icon.name);
                        m_copied = icon.name;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        ImGui::SetTooltip("%s\nClick to copy the name", icon.name);
                    }
                    ImGui::PopID();
                }
            });
        }
    }
    ImGui::EndChild();
}

void Gallery::DrawStyle(UiContext& ui) {
    const ImGuiStyle& s = ui.theme.Applied().style;
    struct Scalar {
        const char* name;
        float value;
    };
    const Scalar scalars[] = {
        {"Alpha", s.Alpha},
        {"DisabledAlpha", s.DisabledAlpha},
        {"FontSizeBase", s.FontSizeBase},
        {"FontScaleMain", s.FontScaleMain},
        {"FontScaleDpi", s.FontScaleDpi},
        {"WindowPadding.x", s.WindowPadding.x},
        {"WindowPadding.y", s.WindowPadding.y},
        {"WindowRounding", s.WindowRounding},
        {"WindowBorderSize", s.WindowBorderSize},
        {"ChildRounding", s.ChildRounding},
        {"ChildBorderSize", s.ChildBorderSize},
        {"PopupRounding", s.PopupRounding},
        {"PopupBorderSize", s.PopupBorderSize},
        {"FramePadding.x", s.FramePadding.x},
        {"FramePadding.y", s.FramePadding.y},
        {"FrameRounding", s.FrameRounding},
        {"FrameBorderSize", s.FrameBorderSize},
        {"ItemSpacing.x", s.ItemSpacing.x},
        {"ItemSpacing.y", s.ItemSpacing.y},
        {"ItemInnerSpacing.x", s.ItemInnerSpacing.x},
        {"ItemInnerSpacing.y", s.ItemInnerSpacing.y},
        {"CellPadding.x", s.CellPadding.x},
        {"CellPadding.y", s.CellPadding.y},
        {"IndentSpacing", s.IndentSpacing},
        {"ScrollbarSize", s.ScrollbarSize},
        {"ScrollbarRounding", s.ScrollbarRounding},
        {"GrabMinSize", s.GrabMinSize},
        {"GrabRounding", s.GrabRounding},
        {"TabRounding", s.TabRounding},
        {"TabBorderSize", s.TabBorderSize},
        {"HoverDelayShort", s.HoverDelayShort},
        {"HoverDelayNormal", s.HoverDelayNormal},
    };
    ImGui::TextDisabled("The style the theme applied; read only.");
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("##style", 2, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Field");
    ImGui::TableSetupColumn("Value");
    ImGui::TableHeadersRow();
    for (const Scalar& scalar : scalars) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(scalar.name);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", scalar.value);
    }
    ImGui::EndTable();
}

}
