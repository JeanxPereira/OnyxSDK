#pragma once

#include <Onyx/Core/Modules/Document.h>
#include <Onyx/Core/Modules/Selection.h>

namespace Onyx::Ui {

// The entry a panel or viewer acts on: a document and a node path inside it.
struct Selection {
    Modules::DocumentId doc = 0;
    Modules::NodePath node;

    bool Empty() const { return doc == 0 && node.indices.empty(); }
};

}
