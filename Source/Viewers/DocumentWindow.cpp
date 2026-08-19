#include <Onyx/Viewers/DocumentWindow.h>
#include "imgui.h"
#include <Onyx/Services/Events.h>
#include <Onyx/App/Widgets.h>

namespace Onyx::Viewers {

// M3b Task 6: DocumentWindow used to subscribe to EventAssetSelected (track
// selection for a future preview policy) and EventWadClosed/EventAllClosed
// (close every tab as a safe default when a WAD closed) -- all three raw-
// pointer asset events are gone along with AssetDatabase/IAssetProfile.
// Nothing has replaced the "close my tabs when my source document closes"
// policy yet: viewer tabs opened through the Workspace path (see
// DocumentBrowser/OpenSelection) don't currently track which Document
// produced them, so there is nothing here to subscribe *to* until that
// association exists. App's "Close All" menu item now closes both the
// Workspace's documents and every DocumentWindow tab directly instead.
DocumentWindow::DocumentWindow() {}

DocumentWindow::~DocumentWindow() {}

void DocumentWindow::AddTab(std::shared_ptr<IDocumentContent> tab) {
    if (tab) {
        m_tabs.push_back(tab);
        EventDocumentOpened::post(tab.get());
    }
}

void DocumentWindow::Shutdown() {
    // Order doesn't matter between the two -- both are just shared_ptr
    // vectors going to zero refcount, destroying every IDocumentContent
    // they hold right here, synchronously, on this call.
    m_pendingDelete.clear();
    m_tabs.clear();
    m_activeTabIndex = -1;
}

void DocumentWindow::CloseAll() {
    // Defer destruction to the next Draw() so GL resources (FBO textures
    // referenced by ImGui::Image in the current frame) live until after
    // ImGui::Render submits them.
    for (auto& t : m_tabs) m_pendingDelete.push_back(std::move(t));
    m_tabs.clear();
    m_activeTabIndex = -1;
}

void DocumentWindow::CloseActiveTab() {
    if (m_activeTabIndex >= 0 && m_activeTabIndex < (int)m_tabs.size()) {
        m_pendingDelete.push_back(std::move(m_tabs[m_activeTabIndex]));
        m_tabs.erase(m_tabs.begin() + m_activeTabIndex);
        m_activeTabIndex = -1; // Reset until next draw loop updates it
    }
}

bool DocumentWindow::HasActiveDocument() const {
    return m_activeTabIndex >= 0 && m_activeTabIndex < (int)m_tabs.size();
}

std::shared_ptr<IDocumentContent> DocumentWindow::GetActiveDocument() const {
    if (HasActiveDocument()) return m_tabs[m_activeTabIndex];
    return nullptr;
}

void DocumentWindow::Draw() {
    // Release viewers queued last frame — the ImGui draw list that referenced
    // their textures has already been rendered and presented.
    if (!m_pendingDelete.empty()) m_pendingDelete.clear();

    if (m_tabs.empty()) {
        ImGui::Begin("Viewer");
        ImGui::TextDisabled("No documents open.");
        ImGui::End();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewer");
    ImGui::PopStyleVar();

    if (ImGui::BeginTabBar("DocumentTabBar", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {
        for (size_t i = 0; i < m_tabs.size(); ) {
            auto& tab = m_tabs[i];
            bool open = tab->IsOpen();
            
            ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
            std::string tabTitle = tab->GetName() + "###" + std::to_string(reinterpret_cast<uintptr_t>(tab.get()));
            
            if (Onyx::App::Widgets::BeginTabItem(tabTitle.c_str(), &open, flags)) {
                m_activeTabIndex = (int)i; // Track active tab
                tab->Draw();
                ImGui::EndTabItem();
            }
            
            if (!open) {
                // Hand the strong reference to m_pendingDelete so the viewer
                // (and any GL textures it owns) outlive this frame's draw
                // submission — see m_pendingDelete comment in the header.
                m_pendingDelete.push_back(std::move(m_tabs[i]));
                m_tabs.erase(m_tabs.begin() + i);
                if (m_activeTabIndex == (int)i) m_activeTabIndex = -1;
            } else {
                tab->SetOpen(open);
                ++i;
            }
        }
        ImGui::EndTabBar();
    }
    
    ImGui::End();
}

} // namespace Onyx::Viewers
