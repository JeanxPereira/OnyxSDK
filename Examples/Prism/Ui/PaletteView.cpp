#include <PrismUi/PaletteView.h>

#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Widgets.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace Prism::Ui {

PaletteView::PaletteView(Onyx::Modules::DocumentId doc, Onyx::Modules::NodePath node, std::string name,
                         std::shared_ptr<const Prism::Palette> palette)
    : m_doc(doc),
      m_node(std::move(node)),
      m_name(std::move(name)),
      m_id("prism.palette." + std::to_string(doc) + "." + m_name),
      m_palette(std::move(palette)) {}

void PaletteView::Draw(Onyx::Ui::UiContext& ui) {
    if (!m_palette || m_palette->colors.empty()) {
        Onyx::Ui::Widgets::Hint("The palette is empty");
        return;
    }
    ImGui::Text("%zu colours", m_palette->colors.size());
    ImGui::Separator();

    const float swatch = Onyx::Ui::Widgets::MetricsFor(ui.theme).rowHeight * 2.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float cell = std::max(swatch, ImGui::CalcTextSize("#00000000").x);
    const int perRow = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (cell + spacing)));

    for (size_t i = 0; i < m_palette->colors.size(); ++i) {
        const glm::u8vec4 c = m_palette->colors[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginGroup();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##swatch", ImVec2(cell, swatch));
        const ImVec2 min(origin.x + (cell - swatch) * 0.5f, origin.y);
        const ImVec2 max(min.x + swatch, min.y + swatch);
        ImDrawList* list = ImGui::GetWindowDrawList();
        list->AddRectFilled(min, max, IM_COL32(c.r, c.g, c.b, c.a));
        list->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border));
        char hex[16];
        std::snprintf(hex, sizeof hex, "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
        ImGui::Text("%zu", i);
        ImGui::TextUnformatted(hex);
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("RGBA %u, %u, %u, %u", c.r, c.g, c.b, c.a);
        }
        ImGui::PopID();
        if ((static_cast<int>(i) + 1) % perRow != 0 && i + 1 < m_palette->colors.size()) {
            ImGui::SameLine();
        }
    }
}

}
