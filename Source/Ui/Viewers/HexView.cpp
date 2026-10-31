#include "Onyx/Ui/Viewers/HexView.h"

#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Widgets.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace Onyx::Ui {

namespace {

constexpr char kDigits[] = "0123456789ABCDEF";

char Printable(std::byte b) {
    const unsigned v = std::to_integer<unsigned>(b);
    return v >= 0x20 && v < 0x7F ? static_cast<char>(v) : '.';
}

}

HexView::HexView(Modules::DocumentId doc, Modules::NodePath node, std::string name, Vfs::Reader reader)
    : m_doc(doc),
      m_node(std::move(node)),
      m_name(std::move(name)),
      m_id("onyx.hex." + std::to_string(doc) + "." + m_name),
      m_reader(std::move(reader)),
      m_size(m_reader->Size()) {}

HexView::HexView(Modules::DocumentId doc, Modules::NodePath node, std::string name, std::vector<std::byte> bytes)
    : m_doc(doc),
      m_node(std::move(node)),
      m_name(std::move(name)),
      m_id("onyx.hex." + std::to_string(doc) + "." + m_name),
      m_memory(std::move(bytes)),
      m_size(m_memory.size()) {}

std::span<const std::byte> HexView::Bytes(uint64_t offset, uint64_t count) {
    if (offset >= m_size) return {};
    count = std::min(count, m_size - offset);
    if (!m_reader) return std::span<const std::byte>(m_memory).subspan(offset, count);

    const uint64_t page = offset / kPageBytes;
    if (page != m_pageIndex) {
        const uint64_t start = page * kPageBytes;
        m_page.resize(std::min(kPageBytes, m_size - start));
        m_reader->Seek(start);
        m_page.resize(m_reader->Read(std::span<std::byte>(m_page)));
        m_pageIndex = page;
    }
    const uint64_t local = offset - page * kPageBytes;
    if (local >= m_page.size()) return {};
    return std::span<const std::byte>(m_page).subspan(local, std::min<uint64_t>(count, m_page.size() - local));
}

HexRow HexView::Row(uint64_t index) {
    HexRow row;
    row.offset = index * kRowBytes;
    for (const std::byte b : Bytes(row.offset, kRowBytes)) {
        const unsigned v = std::to_integer<unsigned>(b);
        if (!row.hex.empty()) row.hex += ' ';
        row.hex += kDigits[v >> 4];
        row.hex += kDigits[v & 0xF];
        row.ascii += Printable(b);
    }
    return row;
}

void HexView::GoTo(uint64_t offset) {
    m_scrollRow = std::min(offset, m_size == 0 ? 0 : m_size - 1) / kRowBytes;
    m_scrollPending = true;
}

void HexView::Draw(UiContext&) {
    ImGui::Text("%llu bytes", static_cast<unsigned long long>(m_size));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputTextWithHint("##goto", "Go to offset (0x...)", m_goto, sizeof m_goto, ImGuiInputTextFlags_EnterReturnsTrue)) {
        char* end = nullptr;
        const unsigned long long value = std::strtoull(m_goto, &end, 0);
        if (end != m_goto) GoTo(value);
    }
    ImGui::Separator();

    const int offsetDigits = m_size > 0xFFFFFFFFull ? 16 : 8;
    const float charWidth = ImGui::CalcTextSize("0").x;
    const float cell = charWidth * 3.0f;
    const float hexX = charWidth * static_cast<float>(offsetDigits + 2);
    const float asciiX = hexX + cell * static_cast<float>(kRowBytes) + charWidth;
    const float width = asciiX + charWidth * static_cast<float>(kRowBytes);
    const float lineHeight = ImGui::GetTextLineHeight();
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    if (!ImGui::BeginChild("##bytes", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return;
    }
    ImDrawList* list = ImGui::GetWindowDrawList();
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        list->AddText(p, dimColor, "Offset");
        for (uint64_t i = 0; i < kRowBytes; ++i) {
            char label[3] = {kDigits[i >> 4], kDigits[i & 0xF], 0};
            list->AddText(ImVec2(p.x + hexX + cell * static_cast<float>(i), p.y), dimColor, label);
        }
        list->AddText(ImVec2(p.x + asciiX, p.y), dimColor, "ASCII");
        ImGui::Dummy(ImVec2(width, lineHeight));
    }

    if (m_scrollPending) {
        ImGui::SetScrollY(static_cast<float>(m_scrollRow) * rowHeight);
        m_scrollPending = false;
    }
    Widgets::ForEachVisible(static_cast<size_t>(RowCount()), rowHeight, [&](size_t i) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const uint64_t offset = static_cast<uint64_t>(i) * kRowBytes;
        char head[24];
        std::snprintf(head, sizeof head, "%0*llX", offsetDigits, static_cast<unsigned long long>(offset));
        list->AddText(p, dimColor, head);

        char ascii[kRowBytes + 1] = {};
        size_t n = 0;
        for (const std::byte b : Bytes(offset, kRowBytes)) {
            const unsigned v = std::to_integer<unsigned>(b);
            const char hex[3] = {kDigits[v >> 4], kDigits[v & 0xF], 0};
            list->AddText(ImVec2(p.x + hexX + cell * static_cast<float>(n), p.y), v == 0 ? dimColor : textColor, hex);
            ascii[n++] = Printable(b);
        }
        list->AddText(ImVec2(p.x + asciiX, p.y), textColor, ascii, ascii + n);
        ImGui::Dummy(ImVec2(width, lineHeight));
    });
    ImGui::EndChild();
}

}
