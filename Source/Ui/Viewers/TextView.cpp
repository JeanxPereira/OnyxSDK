#include "Onyx/Ui/Viewers/TextView.h"

#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Widgets.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace Onyx::Ui {

namespace {

bool ContainsNoCase(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return false;
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           }) != haystack.end();
}

size_t Digits(size_t value) {
    size_t digits = 1;
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

}

TextView::TextView(Modules::DocumentId doc, Modules::NodePath node, std::string name, Domain::Text text)
    : m_doc(doc),
      m_node(std::move(node)),
      m_name(std::move(name)),
      m_id("onyx.text." + std::to_string(doc) + "." + m_name),
      m_text(std::move(text)) {
    const std::string& s = m_text.text;
    size_t pos = 0;
    while (pos < s.size()) {
        m_starts.push_back(pos);
        const size_t newline = s.find('\n', pos);
        if (newline == std::string::npos) break;
        pos = newline + 1;
    }
}

std::string_view TextView::Line(size_t index) const {
    if (index >= m_starts.size()) return {};
    const size_t begin = m_starts[index];
    size_t end = index + 1 < m_starts.size() ? m_starts[index + 1] : m_text.text.size();
    if (end > begin && m_text.text[end - 1] == '\n') --end;
    if (end > begin && m_text.text[end - 1] == '\r') --end;
    return std::string_view(m_text.text).substr(begin, end - begin);
}

size_t TextView::FindNext(std::string_view needle, size_t from) const {
    for (size_t i = from; i < m_starts.size(); ++i) {
        if (ContainsNoCase(Line(i), needle)) return i;
    }
    return m_starts.size();
}

float TextView::LineTop(size_t line, float rowHeight) const {
    if (wrap && line < m_wrapTop.size()) return m_wrapTop[line];
    return static_cast<float>(line) * rowHeight;
}

void TextView::LayoutWrap(float width) {
    if (m_wrapTop.size() == m_starts.size() + 1 && std::abs(width - m_wrapWidth) < 0.5f) return;
    m_wrapWidth = width;
    const float lineHeight = ImGui::GetTextLineHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    m_wrapTop.assign(m_starts.size() + 1, 0.0f);
    for (size_t i = 0; i < m_starts.size(); ++i) {
        const std::string_view line = Line(i);
        const float height = ImGui::CalcTextSize(line.data(), line.data() + line.size(), false, width).y;
        m_wrapTop[i + 1] = m_wrapTop[i] + std::max(height, lineHeight) + spacing;
    }
}

void TextView::Draw(UiContext&) {
    if (Widgets::ToolbarButton("Wrap", wrap)) wrap = !wrap;
    ImGui::SameLine();
    if (Widgets::ToolbarButton("Copy all")) ImGui::SetClipboardText(m_text.text.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    const bool submitted = ImGui::InputTextWithHint("##find", "Find", m_query, sizeof m_query, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool next = Widgets::ToolbarButton("Next");
    const std::string_view needle(m_query);
    if ((submitted || next) && !needle.empty()) {
        size_t found = FindNext(needle, m_hasMatch ? m_match + 1 : 0);
        if (found == Lines()) found = FindNext(needle, 0);
        m_hasMatch = found != Lines();
        if (m_hasMatch) {
            m_match = found;
            m_scrollTo = found;
            m_scrollPending = true;
        }
    }
    ImGui::SameLine();
    ImGui::Text("%zu lines", Lines());
    if (!needle.empty() && !m_hasMatch && (submitted || next)) {
        ImGui::SameLine();
        Widgets::Hint("no match");
    }
    ImGui::Separator();

    const size_t digits = Digits(Lines());
    const float gutter = ImGui::CalcTextSize("0").x * static_cast<float>(digits + 2);
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const ImU32 matchColor = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);

    if (!ImGui::BeginChild("##lines", ImVec2(0, 0), ImGuiChildFlags_None, wrap ? ImGuiWindowFlags_None : ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return;
    }

    const auto drawLine = [&](size_t i) {
        const std::string_view line = Line(i);
        if (!needle.empty() && ContainsNoCase(line, needle)) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + ImGui::GetTextLineHeight()), matchColor);
        }
        ImGui::TextDisabled("%*zu", static_cast<int>(digits), i + 1);
        ImGui::SameLine(gutter);
        if (wrap) ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + m_wrapWidth);
        ImGui::TextUnformatted(line.data(), line.data() + line.size());
        if (wrap) ImGui::PopTextWrapPos();
    };

    if (!wrap) {
        if (m_scrollPending) {
            ImGui::SetScrollY(LineTop(m_scrollTo, rowHeight));
            m_scrollPending = false;
        }
        Widgets::ForEachVisible(Lines(), rowHeight, drawLine);
    } else {
        LayoutWrap(std::max(ImGui::GetContentRegionAvail().x - gutter - ImGui::GetStyle().ScrollbarSize, 40.0f));
        if (m_scrollPending) {
            ImGui::SetScrollY(LineTop(m_scrollTo, rowHeight));
            m_scrollPending = false;
        }
        const float top = ImGui::GetScrollY();
        const float bottom = top + ImGui::GetWindowHeight();
        const float origin = ImGui::GetCursorPosY();
        size_t i = static_cast<size_t>(std::upper_bound(m_wrapTop.begin(), m_wrapTop.end(), top - origin) - m_wrapTop.begin());
        i = i > 0 ? i - 1 : 0;
        for (; i < Lines() && m_wrapTop[i] + origin < bottom; ++i) {
            ImGui::SetCursorPosY(origin + m_wrapTop[i]);
            drawLine(i);
        }
        ImGui::SetCursorPosY(origin + m_wrapTop.back());
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
    ImGui::EndChild();
}

}
