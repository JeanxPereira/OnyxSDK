#pragma once

#include <Onyx/Core/Vfs/Reader.h>
#include <Onyx/Ui/Panel.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::Ui {

struct HexRow {
    uint64_t offset = 0;
    std::string hex;    // up to 16 bytes as two upper-case digits each, single spaces between
    std::string ascii;  // printable bytes as themselves, the rest as '.'
};

// A read-only hex dump, 16 bytes a row. Rows are read through the reader on demand, so the size of
// the blob does not matter.
class HexView : public IDocumentView {
public:
    static constexpr uint64_t kRowBytes = 16;

    HexView(Modules::DocumentId doc, Modules::NodePath node, std::string name, Vfs::Reader reader);
    HexView(Modules::DocumentId doc, Modules::NodePath node, std::string name, std::vector<std::byte> bytes);

    std::string_view Id() const override { return m_id; }
    std::string_view Title() const override { return m_name; }
    void Draw(UiContext& ui) override;
    Modules::DocumentId Document() const override { return m_doc; }
    Modules::NodePath Node() const override { return m_node; }

    uint64_t Size() const { return m_size; }
    uint64_t RowCount() const { return (m_size + kRowBytes - 1) / kRowBytes; }
    // An empty row for an index past the end.
    HexRow Row(uint64_t index);
    // Scrolls so the row holding `offset` is at the top on the next draw.
    void GoTo(uint64_t offset);

private:
    static constexpr uint64_t kPageBytes = 4096;

    std::span<const std::byte> Bytes(uint64_t offset, uint64_t count);

    Modules::DocumentId m_doc;
    Modules::NodePath m_node;
    std::string m_name;
    std::string m_id;
    std::optional<Vfs::Reader> m_reader;
    std::vector<std::byte> m_memory;
    uint64_t m_size = 0;
    std::vector<std::byte> m_page;
    uint64_t m_pageIndex = ~uint64_t(0);
    char m_goto[32] = {};
    uint64_t m_scrollRow = 0;
    bool m_scrollPending = false;
};

}
