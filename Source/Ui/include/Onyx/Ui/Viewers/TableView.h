#pragma once

#include <Onyx/Core/Domain/Table.h>
#include <Onyx/Ui/Panel.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::Ui {

// A sortable, read-only table. Sorting reorders an index list; the table itself is never copied.
class TableView : public IDocumentView {
public:
    TableView(Modules::DocumentId doc, Modules::NodePath node, std::string name, Domain::Table table);

    std::string_view Id() const override { return m_id; }
    std::string_view Title() const override { return m_name; }
    void Draw(UiContext& ui) override;
    Modules::DocumentId Document() const override { return m_doc; }
    Modules::NodePath Node() const override { return m_node; }

    const std::vector<std::string>& Columns() const { return m_table.columns; }
    size_t RowCount() const { return m_order.size(); }
    // The cell at a displayed row; empty for an index out of range.
    std::string_view Cell(size_t displayRow, size_t column) const;
    // Cells that both read as numbers compare numerically, the rest as text.
    void SortBy(size_t column, bool ascending);
    void ClearSort();

private:
    Modules::DocumentId m_doc;
    Modules::NodePath m_node;
    std::string m_name;
    std::string m_id;
    Domain::Table m_table;
    std::vector<size_t> m_order;
};

}
