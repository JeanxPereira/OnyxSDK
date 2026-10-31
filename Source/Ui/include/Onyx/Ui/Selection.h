#pragma once

#include <Onyx/Core/Modules/Document.h>
#include <Onyx/Core/Modules/Selection.h>
#include <Onyx/Core/Services/EventBus.h>

namespace Onyx::Ui {

// The entry a panel or viewer acts on: a document and a node path inside it.
struct Selection {
    Modules::DocumentId doc = 0;
    Modules::NodePath node;

    bool Empty() const { return doc == 0 && node.indices.empty(); }
    bool operator==(const Selection& other) const {
        return doc == other.doc && node.indices == other.node.indices;
    }
};

// Holds the current selection. Set posts Modules::SelectionChanged on the bus when the value
// changes.
class SelectionModel {
public:
    void Set(Selection selection, Services::EventBus& bus);
    const Selection& Current() const { return m_current; }

private:
    Selection m_current;
};

}
