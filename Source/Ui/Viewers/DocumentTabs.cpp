#include "Onyx/Ui/Viewers/DocumentTabs.h"

#include <imgui.h>

#include <Onyx/Ui/Widgets.h>

#include <string>
#include <utility>

namespace Onyx::Ui {

namespace {

struct TabBarScope {
    bool open;
    explicit TabBarScope(const char* id, ImGuiTabBarFlags flags) : open(ImGui::BeginTabBar(id, flags)) {}
    ~TabBarScope() {
        if (open) ImGui::EndTabBar();
    }
};

struct TabItemScope {
    bool open;
    TabItemScope(const char* label, bool* closable, ImGuiTabItemFlags flags)
        : open(ImGui::BeginTabItem(label, closable, flags)) {}
    ~TabItemScope() {
        if (open) ImGui::EndTabItem();
    }
};

}

DocumentTabs::DocumentTabs(Modules::Workspace& workspace)
    : m_closed(workspace.Events().On<Modules::DocumentClosed>(
          [this](const Modules::DocumentClosed& e) { CloseForDocument(e.id); })) {}

void DocumentTabs::Add(std::unique_ptr<IDocumentView> view) {
    if (!view) return;
    m_tabs.push_back(std::move(view));
    m_active = m_tabs.size() - 1;
    m_select = true;
}

void DocumentTabs::Retire(size_t index) {
    m_retired.push_back(std::move(m_tabs[index]));
    m_tabs.erase(m_tabs.begin() + static_cast<std::ptrdiff_t>(index));
    if (index < m_active) --m_active;
    if (m_active >= m_tabs.size()) m_active = m_tabs.empty() ? 0 : m_tabs.size() - 1;
    m_select = true;
}

void DocumentTabs::CloseActive() {
    if (m_active < m_tabs.size()) Retire(m_active);
}

void DocumentTabs::CloseAll() {
    for (auto& tab : m_tabs) m_retired.push_back(std::move(tab));
    m_tabs.clear();
    m_active = 0;
}

void DocumentTabs::CloseForDocument(Modules::DocumentId doc) {
    for (size_t i = m_tabs.size(); i-- > 0;) {
        if (m_tabs[i]->Document() == doc) Retire(i);
    }
}

IDocumentView* DocumentTabs::Active() {
    return m_active < m_tabs.size() ? m_tabs[m_active].get() : nullptr;
}

void DocumentTabs::Draw(UiContext& ui) {
    m_dying.clear();
    m_dying.swap(m_retired);

    if (m_tabs.empty()) {
        Widgets::Hint("Open an asset to view it");
        return;
    }

    TabBarScope bar("##documents", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs |
                                       ImGuiTabBarFlags_FittingPolicyScroll);
    if (!bar.open) return;

    const bool select = m_select;
    size_t closing = m_tabs.size();
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        IDocumentView& view = *m_tabs[i];
        const std::string label = std::string(view.Title()) + "###" + std::to_string(reinterpret_cast<uintptr_t>(&view));
        bool keep = true;
        const ImGuiTabItemFlags flags = select && i == m_active ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        TabItemScope item(label.c_str(), &keep, flags);
        if (item.open) {
            if (!select) m_active = i;
            view.Draw(ui);
        }
        if (!keep) closing = i;
    }
    m_select = false;
    if (closing < m_tabs.size()) Retire(closing);
}

}
