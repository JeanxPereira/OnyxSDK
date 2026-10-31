#include "Onyx/Ui/Viewers/TableView.h"

#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Widgets.h>

#include <imgui.h>

#include <algorithm>
#include <charconv>
#include <numeric>
#include <utility>

namespace Onyx::Ui {

namespace {

constexpr size_t kMaxColumns = 64;

bool ParseNumber(std::string_view text, double& out) {
    if (text.empty()) return false;
    const char* first = text.data();
    const char* last = first + text.size();
    const auto [end, ec] = std::from_chars(first, last, out);
    return ec == std::errc() && end == last;
}

// Numbers sort before text; numbers by value, text bytewise.
bool Less(std::string_view a, std::string_view b) {
    double x = 0;
    double y = 0;
    const bool xn = ParseNumber(a, x);
    const bool yn = ParseNumber(b, y);
    if (xn != yn) return xn;
    return xn ? x < y : a < b;
}

}

TableView::TableView(Modules::DocumentId doc, Modules::NodePath node, std::string name, Domain::Table table)
    : m_doc(doc),
      m_node(std::move(node)),
      m_name(std::move(name)),
      m_id("onyx.table." + std::to_string(doc) + "." + m_name),
      m_table(std::move(table)),
      m_order(m_table.rows.size()) {
    std::iota(m_order.begin(), m_order.end(), size_t{0});
}

std::string_view TableView::Cell(size_t displayRow, size_t column) const {
    if (displayRow >= m_order.size()) return {};
    const std::vector<std::string>& row = m_table.rows[m_order[displayRow]];
    return column < row.size() ? std::string_view(row[column]) : std::string_view();
}

void TableView::SortBy(size_t column, bool ascending) {
    const auto cell = [&](size_t row) {
        const std::vector<std::string>& r = m_table.rows[row];
        return column < r.size() ? std::string_view(r[column]) : std::string_view();
    };
    std::stable_sort(m_order.begin(), m_order.end(), [&](size_t a, size_t b) {
        return ascending ? Less(cell(a), cell(b)) : Less(cell(b), cell(a));
    });
}

void TableView::ClearSort() {
    std::iota(m_order.begin(), m_order.end(), size_t{0});
}

void TableView::Draw(UiContext&) {
    const size_t columns = std::min(m_table.columns.size(), kMaxColumns);
    ImGui::Text("%zu rows, %zu columns", m_table.rows.size(), m_table.columns.size());
    if (m_table.columns.size() > columns) {
        ImGui::SameLine();
        Widgets::Hint("only the first columns are shown");
    }
    ImGui::Separator();
    if (columns == 0) {
        Widgets::Hint("The table has no columns");
        return;
    }

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable(m_id.c_str(), static_cast<int>(columns), flags)) return;

    ImGui::TableSetupScrollFreeze(0, 1);
    for (size_t c = 0; c < columns; ++c) ImGui::TableSetupColumn(m_table.columns[c].c_str());
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
        if (specs->SpecsCount > 0) {
            const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
            SortBy(static_cast<size_t>(spec.ColumnIndex), spec.SortDirection == ImGuiSortDirection_Ascending);
        } else {
            ClearSort();
        }
        specs->SpecsDirty = false;
    }

    Widgets::ForEachVisible(m_order.size(), ImGui::GetTextLineHeightWithSpacing(), [&](size_t row) {
        ImGui::TableNextRow();
        for (size_t c = 0; c < columns; ++c) {
            ImGui::TableSetColumnIndex(static_cast<int>(c));
            ImGui::PushID(static_cast<int>(row * columns + c));
            const std::string_view text = Cell(row, c);
            ImGui::Selectable("##cell", false, ImGuiSelectableFlags_None);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                ImGui::SetClipboardText(std::string(text).c_str());
            }
            if (ImGui::BeginPopupContextItem("##menu")) {
                if (ImGui::MenuItem("Copy cell")) ImGui::SetClipboardText(std::string(text).c_str());
                if (ImGui::MenuItem("Copy row")) {
                    std::string line;
                    for (size_t k = 0; k < columns; ++k) {
                        if (k > 0) line += '\t';
                        line += Cell(row, k);
                    }
                    ImGui::SetClipboardText(line.c_str());
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
            ImGui::PopID();
        }
    });
    ImGui::EndTable();
}

}
