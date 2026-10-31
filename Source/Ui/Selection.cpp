#include "Onyx/Ui/Selection.h"

#include <utility>

namespace Onyx::Ui {

void SelectionModel::Set(Selection selection, Services::EventBus& bus) {
    if (selection == m_current) return;
    m_current = std::move(selection);
    bus.Post(Modules::SelectionChanged{m_current.doc, m_current.node});
}

}
