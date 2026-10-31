#pragma once

#include <Onyx/Ui/Panel.h>

#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::Ui {

class PanelHost {
public:
    // Replaces a panel with the same id, keeping its position. Add and Remove called while DrawAll
    // runs (from a panel's Draw) take effect when DrawAll finishes.
    void Add(std::unique_ptr<IPanel> panel);
    void Remove(std::string_view id);
    IPanel* Find(std::string_view id);
    std::span<const std::unique_ptr<IPanel>> All() const { return m_panels; }

    // Draws each visible panel in its own ImGui window. A panel whose Draw throws is hidden and
    // logged once per panel id.
    void DrawAll(UiContext& ui);

private:
    struct Pending {
        std::unique_ptr<IPanel> add;
        std::string remove;
    };

    std::vector<std::unique_ptr<IPanel>> m_panels;
    std::vector<Pending> m_pending;
    bool m_drawing = false;
    std::set<std::string, std::less<>> m_reported;
};

}
