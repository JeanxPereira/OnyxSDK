#pragma once

#include <Onyx/Ui/Panel.h>
#include <Onyx/Ui/Selection.h>

#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Onyx::Domain {
struct AssetEntry;
}

namespace Onyx::Ui {

// One tree per open document. The visible nodes are flattened into a row list that is rebuilt
// only when a document becomes ready or closes, or a node expands or collapses; a frame draws
// just the rows inside the clipper's range.
class DocumentBrowser final : public IPanel {
public:
    std::string_view Id() const override { return "onyx.browser"; }
    std::string_view Title() const override { return "Documents"; }
    void Draw(UiContext& ui) override;

    // Called on double-click or Enter over a selection. Unset: nothing happens.
    std::function<void(UiContext&, const Onyx::Ui::Selection&)> onOpen;

    const SelectionModel& Selection() const { return m_selection; }

    // Applies pending closes and rebuilds the row list when the documents changed.
    void Refresh(UiContext& ui);

    size_t VisibleRows() const { return m_rows.size(); }
    size_t RowsDrawn() const { return m_rowsDrawn; }
    // The problem count shown next to a document row; empty when the document has none.
    std::string_view BadgeOf(size_t row) const;
    // The UTF-8 file name shown on a document row; empty for a node row.
    std::string_view LabelOf(size_t row) const;

    // An empty path addresses the document row itself.
    void SetExpanded(UiContext& ui, Modules::DocumentId doc, const Modules::NodePath& path, bool expanded);

    // Expands the ancestors of the node so its row is visible, then selects it.
    void Select(UiContext& ui, const Onyx::Ui::Selection& selection);
    void Activate(UiContext& ui, size_t row);
    void SelectRow(UiContext& ui, size_t row);
    void RequestClose(Modules::DocumentId doc) { m_closeQueue.push_back(doc); }

private:
    struct Row {
        Modules::DocumentId doc = 0;
        const Domain::AssetEntry* entry = nullptr;
        uint32_t depth = 0;
        uint32_t index = 0;
        uint32_t parent = 0;
        bool hasChildren = false;
        bool expanded = false;
        bool failed = false;
        const char* glyph = "";
        glm::vec4 color{1.0f};
        std::string docLabel;
        std::string badge;
    };

    using Key = std::pair<Modules::DocumentId, const Domain::AssetEntry*>;

    // Drops the expansion state and the selection of a document that closed or vanished.
    void Forget(UiContext& ui, Modules::DocumentId id);
    void Rebuild(UiContext& ui);
    void Append(UiContext& ui, Modules::DocumentId doc, const std::vector<Domain::AssetEntry>& entries,
                uint32_t depth, uint32_t parentRow);
    Modules::NodePath PathOf(size_t row) const;
    const Domain::AssetEntry* EntryFor(UiContext& ui, const Onyx::Ui::Selection& selection) const;
    void DrawRow(UiContext& ui, size_t index);

    std::vector<Row> m_rows;
    struct Signature {
        Modules::DocumentId id = 0;
        bool ready = false;
        size_t diags = 0;
        bool operator==(const Signature&) const = default;
    };

    std::vector<Signature> m_signature;
    std::set<Key> m_expanded;
    std::set<Modules::DocumentId> m_known;
    std::vector<Modules::DocumentId> m_closeQueue;
    SelectionModel m_selection;
    const Domain::AssetEntry* m_selectedEntry = nullptr;
    bool m_dirty = true;
    size_t m_rowsDrawn = 0;
};

}
