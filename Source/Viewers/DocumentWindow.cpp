#include <Onyx/Viewers/DocumentWindow.h>
#include "imgui.h"
#include <Onyx/Services/Events.h>
#include <Onyx/App/Widgets.h>
#include <Onyx/Services/Logger.h>

namespace Onyx::Viewers {

// M3b Task 6 removed the raw-pointer asset events (EventAssetSelected,
// EventWadClosed/EventAllClosed) this class used to subscribe to along
// with AssetDatabase/IAssetProfile, leaving "close my tabs when my source
// document closes" with nothing to subscribe to -- viewer tabs opened
// through the Workspace path didn't yet track which Document produced
// them. Task 13 (M4) restores that behavior on top of the Workspace's own
// DocumentClosed event instead: AddTab now records an owning docId per
// tab (see the header), and CloseTabsForDocument(docId) closes every tab
// carrying it. This class still does not subscribe to anything itself --
// DocumentWindow stays decoupled from Onyx::Modules/EventBus, same as
// IDocumentContent.h -- the actual `ws.Events().On<DocumentClosed>(...)`
// subscription lives in Window (Source/App/Window.cpp), which already
// owns both the Workspace and (via App) this DocumentWindow, and calls
// CloseTabsForDocument(ev.id) from its handler.
DocumentWindow::DocumentWindow() {}

DocumentWindow::~DocumentWindow() {}

void DocumentWindow::AddTab(std::shared_ptr<IDocumentContent> tab, uint64_t docId) {
    if (tab) {
        EventDocumentOpened::post(tab.get());
        m_tabs.push_back(Tab{docId, std::move(tab)});
    }
}

void DocumentWindow::Shutdown() {
    // T14-review rider (b): the "a viewer's content outlives what Shutdown
    // was supposed to guarantee" lifetime bug class has recurred in this
    // codebase before this call site (TexturePool's own shutdown-order
    // guard comment, Viewport3D's destructor guard) -- make the third
    // occurrence loud instead of a silent leak/use-after-teardown waiting
    // to happen. use_count() > 1 here means something OUTSIDE these
    // containers still holds a shared_ptr to a tab's content, so clearing
    // the vector below will NOT actually destroy it (and will not run its
    // destructor's GPU-resource cleanup) -- exactly the class of bug this
    // guards against, debug-only since it never changes production
    // behavior, only surfaces the violation immediately instead of letting
    // it manifest as a mysterious later crash/leak.
    //
    // This is ONYX_LOGF_ERR, not assert(): DocumentWindow::Shutdown runs on the
    // GUI thread, and MSVC's assert() pops a MODAL dialog that blocks the
    // window's message pump. The app then just appears to freeze on its
    // last frame, Windows marks it "Not responding", and the dialog itself
    // is frequently hidden behind the main window -- the user sees an
    // unexplained hang with no way to know a guard even fired. An
    // invariant guard must never be able to deadlock the message pump, so
    // this logs the same diagnostic and lets the existing teardown below
    // continue -- the guard's job is telling us the invariant broke, not
    // stopping the process.
#ifndef NDEBUG
    for (const auto& content : m_pendingDelete) {
        if (content && content.use_count() > 1) {
            ONYX_LOGF_ERR("[DocumentWindow] Shutdown: a pending-delete tab's content (%p, "
                    "use_count=%ld) is still externally shared -- clearing "
                    "m_pendingDelete will not destroy it",
                    static_cast<const void*>(content.get()),
                    static_cast<long>(content.use_count()));
        }
    }
    for (const auto& tab : m_tabs) {
        if (tab.content && tab.content.use_count() > 1) {
            ONYX_LOGF_ERR("[DocumentWindow] Shutdown: a tab's content (%p, use_count=%ld) is "
                    "still externally shared -- clearing m_tabs will not destroy it",
                    static_cast<const void*>(tab.content.get()),
                    static_cast<long>(tab.content.use_count()));
        }
    }
#endif

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
    for (auto& t : m_tabs) m_pendingDelete.push_back(std::move(t.content));
    m_tabs.clear();
    m_activeTabIndex = -1;
}

void DocumentWindow::CloseActiveTab() {
    if (m_activeTabIndex >= 0 && m_activeTabIndex < (int)m_tabs.size()) {
        m_pendingDelete.push_back(std::move(m_tabs[m_activeTabIndex].content));
        m_tabs.erase(m_tabs.begin() + m_activeTabIndex);
        m_activeTabIndex = -1; // Reset until next draw loop updates it
    }
}

void DocumentWindow::CloseTabsForDocument(uint64_t docId) {
    if (docId == 0) return; // 0 = invalid DocumentId; never matches a real tab's association
    for (size_t i = 0; i < m_tabs.size(); ) {
        if (m_tabs[i].docId == docId) {
            m_pendingDelete.push_back(std::move(m_tabs[i].content));
            m_tabs.erase(m_tabs.begin() + i);
            if (m_activeTabIndex == (int)i) m_activeTabIndex = -1;
        } else {
            ++i;
        }
    }
}

void DocumentWindow::CloseTab(const IDocumentContent* tab) {
    if (!tab) return;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].content.get() == tab) {
            m_pendingDelete.push_back(std::move(m_tabs[i].content));
            m_tabs.erase(m_tabs.begin() + i);
            if (m_activeTabIndex == (int)i) m_activeTabIndex = -1;
            return;
        }
    }
}

bool DocumentWindow::HasActiveDocument() const {
    return m_activeTabIndex >= 0 && m_activeTabIndex < (int)m_tabs.size();
}

std::shared_ptr<IDocumentContent> DocumentWindow::GetActiveDocument() const {
    if (HasActiveDocument()) return m_tabs[m_activeTabIndex].content;
    return nullptr;
}

size_t DocumentWindow::TabCountForDocument(uint64_t docId) const {
    size_t count = 0;
    for (const auto& t : m_tabs) {
        if (t.docId == docId) ++count;
    }
    return count;
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
            auto& tab = m_tabs[i].content;
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
                m_pendingDelete.push_back(std::move(m_tabs[i].content));
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
